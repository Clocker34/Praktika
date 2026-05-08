// Менеджер лицензирования (задание 3).
// Лицензионный тикет хранится в оперативной памяти (п.9), не передаётся
// клиентам (п.8): RPC возвращает только isLicensed + expiryDate.
#include "license_manager.h"
#include "auth_manager.h"
#include "http_client.h"
#include "json_parser.h"
#include "../app_config.h"

#include <winsock2.h>
#include <windows.h>
#include <iphlpapi.h>
#include <string>
#include <mutex>
#include <vector>

#pragma comment(lib, "Iphlpapi.lib")

namespace license {

namespace {

std::mutex g_mu;

// Лицензионный тикет — только в памяти (п.9).
std::string g_ticket;         // полный JSON тикета
bool g_isLicensed = false;
std::wstring g_expiryDate;    // "YYYY-MM-DD"
long long g_ticketLifetime = 0;  // ticketLifetimeSeconds из тикета

// Код лицензии, MAC-адрес и имя устройства сохраняются для повторных check.
std::string g_licenseCode;
std::string g_macAddress;
std::string g_deviceName;

HANDLE g_refreshThread = nullptr;
HANDLE g_stopEvent = nullptr;

std::wstring Utf8ToWide(const std::string& s) {
  if (s.empty()) return {};
  const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(),
                                    static_cast<int>(s.size()), nullptr, 0);
  std::wstring w(static_cast<size_t>(n), L'\0');
  MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()),
                      &w[0], n);
  return w;
}

std::string WideToUtf8(const std::wstring& w) {
  if (w.empty()) return {};
  const int n = WideCharToMultiByte(CP_UTF8, 0, w.data(),
                                    static_cast<int>(w.size()), nullptr, 0,
                                    nullptr, nullptr);
  std::string s(static_cast<size_t>(n), '\0');
  WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()),
                      &s[0], n, nullptr, nullptr);
  return s;
}

// Получить первый MAC-адрес активного адаптера.
std::string GetFirstMacAddress() {
  ULONG bufLen = 0;
  GetAdaptersAddresses(AF_INET, 0, nullptr, nullptr, &bufLen);
  if (bufLen == 0) return "00:00:00:00:00:00";

  std::vector<BYTE> buf(bufLen);
  auto* addrs = reinterpret_cast<PIP_ADAPTER_ADDRESSES>(buf.data());
  if (GetAdaptersAddresses(AF_INET, 0, nullptr, addrs, &bufLen) != NO_ERROR)
    return "00:00:00:00:00:00";

  for (auto* a = addrs; a; a = a->Next) {
    if (a->PhysicalAddressLength == 6 && a->OperStatus == IfOperStatusUp) {
      char mac[18];
      sprintf_s(mac, "%02X:%02X:%02X:%02X:%02X:%02X",
                (unsigned)a->PhysicalAddress[0], (unsigned)a->PhysicalAddress[1],
                (unsigned)a->PhysicalAddress[2], (unsigned)a->PhysicalAddress[3],
                (unsigned)a->PhysicalAddress[4], (unsigned)a->PhysicalAddress[5]);
      return mac;
    }
  }
  return "00:00:00:00:00:00";
}

// Получить имя компьютера.
std::string GetMachineName() {
  char buf[MAX_COMPUTERNAME_LENGTH + 1]{};
  DWORD sz = sizeof(buf);
  if (GetComputerNameA(buf, &sz)) return buf;
  return "Unknown";
}

// Парсинг тикета из TicketResponse JSON.
// Формат: { "ticket": { "licenseExpirationDate": "2025-12-31",
//   "licenseBlocked": false, "ticketLifetimeSeconds": 300, ... },
//   "signatureBase64": "..." }
void ParseTicket(const std::string& responseJson) {
  g_ticket = responseJson;

  // Вложенный объект "ticket" — упрощённый парсинг.
  // Проверяем licenseBlocked и licenseExpirationDate.
  const std::string blocked = json::JsonString(responseJson, "licenseBlocked");
  g_isLicensed = (blocked != "true");

  const std::string expiry = json::JsonString(responseJson, "licenseExpirationDate");
  g_expiryDate = Utf8ToWide(expiry);

  g_ticketLifetime = json::JsonInt(responseJson, "ticketLifetimeSeconds");
}

// Рассчитать задержку обновления тикета (п.5).
DWORD CalcRefreshDelay() {
  if (g_ticketLifetime <= 0) return 60000;  // 1 мин по умолчанию
  // Обновляем за 10% до истечения, но не менее 30 с и не более 5 мин.
  long long delay = g_ticketLifetime * 9 / 10;
  if (delay < 30) delay = 30;
  if (delay > 300) delay = 300;
  return static_cast<DWORD>(delay * 1000);
}

// Фоновый поток обновления лицензионного тикета (п.5).
DWORD WINAPI RefreshThread(LPVOID) {
  while (true) {
    DWORD delay;
    {
      std::lock_guard<std::mutex> lk(g_mu);
      if (!g_isLicensed || g_ticket.empty()) {
        delay = 30000;  // нет лицензии — проверяем каждые 30 с
      } else {
        delay = CalcRefreshDelay();
      }
    }

    const DWORD w = WaitForSingleObject(g_stopEvent, delay);
    if (w == WAIT_OBJECT_0) break;

    // Пропускаем обновление, если нет аутентификации или нет кода лицензии.
    if (!auth::IsAuthenticated()) continue;
    
    std::string code, mac;
    {
      std::lock_guard<std::mutex> lk(g_mu);
      code = g_licenseCode;
      mac = g_macAddress;
    }
    if (code.empty()) continue;

    // Отправляем POST /api/licenses/check.
    const std::string token = auth::GetAccessToken();
    if (token.empty()) continue;

    std::string body = "{\"licenseCode\":\"";
    body += code;
    body += "\",\"macAddress\":\"";
    body += mac;
    body += "\"}";

    HttpResponse resp = HttpPostJson(PRAKTIKA_API_HOST, PRAKTIKA_API_PORT,
                                     PRAKTIKA_API_LICENSE, body, token);
    if (resp.ok()) {
      std::lock_guard<std::mutex> lk(g_mu);
      ParseTicket(resp.body);
    }
  }
  return 0;
}

}  // namespace

void Init() {
  g_stopEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
  g_refreshThread = CreateThread(nullptr, 0, RefreshThread, nullptr, 0,
                                 nullptr);
}

void Shutdown() {
  if (g_stopEvent) {
    SetEvent(g_stopEvent);
  }
  if (g_refreshThread) {
    WaitForSingleObject(g_refreshThread, 5000);
    CloseHandle(g_refreshThread);
    g_refreshThread = nullptr;
  }
  if (g_stopEvent) {
    CloseHandle(g_stopEvent);
    g_stopEvent = nullptr;
  }
  ClearTicket();
}

long FetchLicenseStatus() {
  const std::string token = auth::GetAccessToken();
  if (token.empty()) return CYCL_NOT_AUTHENTICATED;

  std::string code, mac;
  {
    std::lock_guard<std::mutex> lk(g_mu);
    code = g_licenseCode;
    mac = g_macAddress;
  }
  if (code.empty()) return CYCL_NO_LICENSE;

  std::string body = "{\"licenseCode\":\"";
  body += code;
  body += "\",\"macAddress\":\"";
  body += mac;
  body += "\"}";

  HttpResponse resp = HttpPostJson(PRAKTIKA_API_HOST, PRAKTIKA_API_PORT,
                                   PRAKTIKA_API_LICENSE, body, token);
  if (!resp.ok()) {
    return (resp.statusCode == 401) ? CYCL_NOT_AUTHENTICATED : CYCL_HTTP_ERROR;
  }

  {
    std::lock_guard<std::mutex> lk(g_mu);
    ParseTicket(resp.body);
    return g_isLicensed ? CYCL_OK : CYCL_NO_LICENSE;
  }
}

long Activate(const wchar_t* activationCode) {
  const std::string token = auth::GetAccessToken();
  if (token.empty()) return CYCL_NOT_AUTHENTICATED;

  // Получаем MAC-адрес и имя устройства.
  const std::string mac = GetFirstMacAddress();
  const std::string devName = GetMachineName();

  // Получаем userId из JWT-токена (claim "uid" — UUID пользователя).
  const std::string userId = jwt::GetClaim(token, "uid");

  std::string codeUtf8 = WideToUtf8(activationCode);

  // POST /api/licenses/activate
  // { "licenseCode": "...", "macAddress": "...", "deviceName": "...", "userId": "..." }
  std::string body = "{\"licenseCode\":\"";
  body += codeUtf8;
  body += "\",\"macAddress\":\"";
  body += mac;
  body += "\",\"deviceName\":\"";
  body += devName;
  if (!userId.empty()) {
    body += "\",\"userId\":\"";
    body += userId;
  }
  body += "\"}";

  HttpResponse resp = HttpPostJson(PRAKTIKA_API_HOST, PRAKTIKA_API_PORT,
                                   PRAKTIKA_API_ACTIVATE, body, token);
  if (!resp.ok()) {
    return CYCL_ACTIVATION_FAILED;
  }

  // Сохраняем код лицензии и MAC для повторных check.
  {
    std::lock_guard<std::mutex> lk(g_mu);
    g_licenseCode = codeUtf8;
    g_macAddress = mac;
    g_deviceName = devName;
  }

  // Если эндпоинт активации вернул тикет — используем его (п.6).
  const std::string expiryCheck = json::JsonString(resp.body, "licenseExpirationDate");
  if (!expiryCheck.empty()) {
    std::lock_guard<std::mutex> lk(g_mu);
    ParseTicket(resp.body);
    return g_isLicensed ? CYCL_OK : CYCL_ACTIVATION_FAILED;
  }

  // Если тикет не вернулся — запрашиваем статус лицензии отдельно.
  return FetchLicenseStatus();
}

void ClearTicket() {
  std::lock_guard<std::mutex> lk(g_mu);
  if (!g_ticket.empty())
    SecureZeroMemory(&g_ticket[0], g_ticket.size());
  g_ticket.clear();
  g_isLicensed = false;
  g_expiryDate.clear();
  g_ticketLifetime = 0;
  g_licenseCode.clear();
  g_macAddress.clear();
  g_deviceName.clear();
}

bool IsLicensed() {
  std::lock_guard<std::mutex> lk(g_mu);
  return g_isLicensed;
}

std::wstring GetExpiryDate() {
  std::lock_guard<std::mutex> lk(g_mu);
  return g_expiryDate;
}

}  // namespace license
