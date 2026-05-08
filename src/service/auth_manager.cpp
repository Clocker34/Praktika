// Менеджер аутентификации (задание 3).
// JWT-токены хранятся в оперативной памяти, не сохраняются на диск (п.9).
#include "auth_manager.h"
#include "http_client.h"
#include "json_parser.h"
#include "../app_config.h"

#include <windows.h>
#include <string>
#include <mutex>

namespace auth {

namespace {

std::mutex g_mu;

// JWT-токены — только в памяти (п.9), не передаются клиентам (п.8).
std::string g_accessToken;
std::string g_refreshToken;
std::wstring g_username;

// Фоновый поток обновления.
HANDLE g_refreshThread = nullptr;
HANDLE g_stopEvent = nullptr;

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

std::wstring Utf8ToWide(const std::string& s) {
  if (s.empty()) return {};
  const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(),
                                    static_cast<int>(s.size()), nullptr, 0);
  std::wstring w(static_cast<size_t>(n), L'\0');
  MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()),
                      &w[0], n);
  return w;
}

// Рассчитать через сколько мс нужно обновить токен.
// Обновляем за 60 секунд до истечения.
DWORD CalcRefreshDelay(const std::string& token) {
  const long long exp = jwt::GetExp(token);
  if (exp <= 0) return 300000;  // 5 мин по умолчанию
  FILETIME ft;
  GetSystemTimeAsFileTime(&ft);
  ULARGE_INTEGER now;
  now.LowPart = ft.dwLowDateTime;
  now.HighPart = ft.dwHighDateTime;
  const long long nowSec =
      static_cast<long long>(now.QuadPart / 10000000ULL - 11644473600ULL);
  long long delta = exp - nowSec - 60;  // за 60 с до истечения
  if (delta < 10) delta = 10;
  if (delta > 86400) delta = 86400;  // max 24 ч
  return static_cast<DWORD>(delta * 1000);
}

// Обновление токенов через refresh endpoint (п.2).
bool DoRefresh() {
  std::string refresh;
  {
    std::lock_guard<std::mutex> lk(g_mu);
    if (g_refreshToken.empty()) return false;
    refresh = g_refreshToken;
  }

  // POST /api/v1/auth/refresh с refresh-токеном в Bearer.
  std::string body = "{\"refresh_token\":\"";
  body += refresh;
  body += "\"}";

  HttpResponse resp = HttpPostJson(PRAKTIKA_API_HOST, PRAKTIKA_API_PORT,
                                   PRAKTIKA_API_REFRESH, body, refresh);
  if (!resp.ok()) return false;

  const std::string newAccess = json::JsonString(resp.body, "access_token");
  const std::string newRefresh = json::JsonString(resp.body, "refresh_token");

  if (newAccess.empty()) return false;

  {
    std::lock_guard<std::mutex> lk(g_mu);
    g_accessToken = newAccess;
    if (!newRefresh.empty()) {
      g_refreshToken = newRefresh;
    }
  }
  return true;
}

// Фоновый поток: периодическое обновление (п.2).
DWORD WINAPI RefreshThread(LPVOID) {
  while (true) {
    // Рассчитать задержку по сроку действия access-токена.
    DWORD delay;
    {
      std::lock_guard<std::mutex> lk(g_mu);
      if (g_accessToken.empty()) {
        delay = 5000;  // нет токена — ждём 5 с и проверяем снова
      } else {
        delay = CalcRefreshDelay(g_accessToken);
      }
    }

    const DWORD w = WaitForSingleObject(g_stopEvent, delay);
    if (w == WAIT_OBJECT_0) break;  // сигнал остановки

    // Проверяем, есть ли токены для обновления.
    {
      std::lock_guard<std::mutex> lk(g_mu);
      if (g_refreshToken.empty()) continue;
    }
    DoRefresh();
  }
  return 0;
}

}  // namespace

void Init() {
  g_stopEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
  g_refreshThread = CreateThread(nullptr, 0, RefreshThread, nullptr, 0, nullptr);
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
  // Очистка токенов.
  std::lock_guard<std::mutex> lk(g_mu);
  SecureZeroMemory(&g_accessToken[0], g_accessToken.size());
  SecureZeroMemory(&g_refreshToken[0], g_refreshToken.size());
  g_accessToken.clear();
  g_refreshToken.clear();
  g_username.clear();
}

long Login(const wchar_t* username, const wchar_t* password) {
  // POST /api/v1/auth/login { "username": "...", "password": "..." }
  std::string uUtf8 = WideToUtf8(username);
  std::string pUtf8 = WideToUtf8(password);

  std::string body = "{\"username\":\"";
  body += uUtf8;
  body += "\",\"password\":\"";
  body += pUtf8;
  body += "\"}";

  // Очищаем пароль из локальной строки.
  SecureZeroMemory(&pUtf8[0], pUtf8.size());

  HttpResponse resp = HttpPostJson(PRAKTIKA_API_HOST, PRAKTIKA_API_PORT,
                                   PRAKTIKA_API_LOGIN, body, "");

  // Очищаем тело запроса (содержит пароль).
  SecureZeroMemory(&body[0], body.size());

  if (!resp.ok()) {
    return CYCL_AUTH_FAILED;
  }

  const std::string access = json::JsonString(resp.body, "access_token");
  const std::string refresh = json::JsonString(resp.body, "refresh_token");

  if (access.empty()) {
    return CYCL_AUTH_FAILED;
  }

  // Извлекаем имя пользователя из JWT-payload (claim "sub" или "username").
  std::string sub = jwt::GetClaim(access, "username");
  if (sub.empty()) sub = jwt::GetClaim(access, "sub");
  if (sub.empty()) sub = uUtf8;

  {
    std::lock_guard<std::mutex> lk(g_mu);
    g_accessToken = access;
    g_refreshToken = refresh;
    g_username = Utf8ToWide(sub);
  }

  return CYCL_OK;
}

void Logout() {
  std::lock_guard<std::mutex> lk(g_mu);
  if (!g_accessToken.empty())
    SecureZeroMemory(&g_accessToken[0], g_accessToken.size());
  if (!g_refreshToken.empty())
    SecureZeroMemory(&g_refreshToken[0], g_refreshToken.size());
  g_accessToken.clear();
  g_refreshToken.clear();
  g_username.clear();
}

bool IsAuthenticated() {
  std::lock_guard<std::mutex> lk(g_mu);
  return !g_accessToken.empty();
}

std::wstring GetUsername() {
  std::lock_guard<std::mutex> lk(g_mu);
  return g_username;
}

std::string GetAccessToken() {
  std::lock_guard<std::mutex> lk(g_mu);
  return g_accessToken;
}

}  // namespace auth
