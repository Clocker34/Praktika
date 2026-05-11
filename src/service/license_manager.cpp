// Менеджер лицензирования (задание 3).
// Лицензионный тикет хранится в оперативной памяти (п.9), не передаётся
// клиентам (п.8): RPC возвращает только isLicensed + expiryDate.
#include "license_manager.h"
#include "auth_manager.h"
#include "http_client.h"
#include "json_parser.h"
#include "../app_config.h"

#include <windows.h>
#include <string>
#include <mutex>

namespace license {

namespace {

std::mutex g_mu;

// Лицензионный тикет — только в памяти (п.9).
std::string g_ticket;         // полный JSON тикета
bool g_isLicensed = false;
std::wstring g_expiryDate;    // "YYYY-MM-DD HH:MM:SS"
long long g_expiryUnix = 0;   // Unix timestamp истечения

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

// Парсинг тикета: извлекаем isLicensed и expiryDate.
void ParseTicket(const std::string& ticketJson) {
  g_ticket = ticketJson;

  // Ожидаемый формат ответа:
  // { "licensed": true, "expiry": "2025-12-31 23:59:59", "expiry_unix": 1767225599, ... }
  g_isLicensed = json::JsonBool(ticketJson, "licensed");

  const std::string expiry = json::JsonString(ticketJson, "expiry");
  g_expiryDate = Utf8ToWide(expiry);

  g_expiryUnix = json::JsonInt(ticketJson, "expiry_unix");
}

// Рассчитать задержку обновления тикета (п.5).
// Обновляем за 10% до истечения, но не реже чем раз в час.
DWORD CalcRefreshDelay() {
  if (g_expiryUnix <= 0) return 3600000;  // 1 час по умолчанию

  FILETIME ft;
  GetSystemTimeAsFileTime(&ft);
  ULARGE_INTEGER now;
  now.LowPart = ft.dwLowDateTime;
  now.HighPart = ft.dwHighDateTime;
  const long long nowSec =
      static_cast<long long>(now.QuadPart / 10000000ULL - 11644473600ULL);

  long long remaining = g_expiryUnix - nowSec;
  if (remaining <= 0) return 60000;  // истёк — проверить через минуту

  long long delay = remaining / 10;  // 10% от оставшегося
  if (delay < 60) delay = 60;
  if (delay > 3600) delay = 3600;

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

    // Пропускаем обновление, если нет аутентификации.
    if (!auth::IsAuthenticated()) continue;

    // Обновляем тикет.
    const std::string token = auth::GetAccessToken();
    if (token.empty()) continue;

    HttpResponse resp = HttpGet(PRAKTIKA_API_HOST, PRAKTIKA_API_PORT,
                                PRAKTIKA_API_LICENSE, token);
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

  HttpResponse resp = HttpGet(PRAKTIKA_API_HOST, PRAKTIKA_API_PORT,
                              PRAKTIKA_API_LICENSE, token);
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

  std::string body = "{\"activation_code\":\"";
  body += WideToUtf8(activationCode);
  body += "\"}";

  HttpResponse resp = HttpPostJson(PRAKTIKA_API_HOST, PRAKTIKA_API_PORT,
                                   PRAKTIKA_API_ACTIVATE, body, token);
  if (!resp.ok()) {
    return CYCL_ACTIVATION_FAILED;
  }

  // П.6: если эндпоинт вернул тикет — используем его.
  const std::string licensed = json::JsonString(resp.body, "licensed");
  if (!licensed.empty()) {
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
  g_expiryUnix = 0;
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
