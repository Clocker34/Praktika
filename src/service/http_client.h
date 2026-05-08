// HTTP-клиент на WinHTTP для HTTPS-запросов к Web-сервису (задание 3).
#pragma once

#include <string>

// Результат HTTP-запроса.
struct HttpResponse {
  long statusCode = 0;      // HTTP status (200, 401, …) или 0 при сетевой ошибке
  std::string body;          // тело ответа (JSON)
  bool ok() const { return statusCode >= 200 && statusCode < 300; }
};

// POST JSON к HTTPS-эндпоинту.
// host      — имя хоста (PRAKTIKA_API_HOST)
// port      — порт (PRAKTIKA_API_PORT)
// path      — URI (например /api/v1/auth/login)
// jsonBody  — тело запроса JSON (UTF-8)
// bearer    — Bearer-токен (если пустой — без Authorization)
HttpResponse HttpPostJson(const wchar_t* host, int port,
                          const wchar_t* path,
                          const std::string& jsonBody,
                          const std::string& bearer = "");

// GET к HTTPS-эндпоинту.
HttpResponse HttpGet(const wchar_t* host, int port,
                     const wchar_t* path,
                     const std::string& bearer = "");
