// HTTP-клиент: WinHTTP (задание 3).
// Используется только службой; GUI не делает HTTP-запросов напрямую.
#include "http_client.h"

#include <windows.h>
#include <winhttp.h>

#include <vector>

#pragma comment(lib, "Winhttp.lib")

namespace {

std::wstring Utf8ToWide(const std::string& s) {
  if (s.empty()) return {};
  const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(),
                                    static_cast<int>(s.size()), nullptr, 0);
  std::wstring w(static_cast<size_t>(n), L'\0');
  MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()),
                      &w[0], n);
  return w;
}

HttpResponse DoRequest(const wchar_t* method, const wchar_t* host, int port,
                       const wchar_t* path, const std::string& body,
                       const std::string& bearer) {
  HttpResponse resp;

  HINTERNET hSession = WinHttpOpen(L"Praktika/1.0",
                                   WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                   WINHTTP_NO_PROXY_NAME,
                                   WINHTTP_NO_PROXY_BYPASS, 0);
  if (!hSession) return resp;

  HINTERNET hConnect = WinHttpConnect(hSession, host,
                                      static_cast<INTERNET_PORT>(port), 0);
  if (!hConnect) {
    WinHttpCloseHandle(hSession);
    return resp;
  }

  HINTERNET hRequest = WinHttpOpenRequest(
      hConnect, method, path, nullptr, WINHTTP_NO_REFERER,
      WINHTTP_DEFAULT_ACCEPT_TYPES,
      WINHTTP_FLAG_SECURE);
  if (!hRequest) {
    WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);
    return resp;
  }

  // Для учебного проекта: игнорируем ошибки TLS-сертификата
  // (позволяет работать с самоподписанными сертификатами).
  DWORD flags = SECURITY_FLAG_IGNORE_UNKNOWN_CA |
                SECURITY_FLAG_IGNORE_CERT_DATE_INVALID |
                SECURITY_FLAG_IGNORE_CERT_CN_INVALID |
                SECURITY_FLAG_IGNORE_CERT_WRONG_USAGE;
  WinHttpSetOption(hRequest, WINHTTP_OPTION_SECURITY_FLAGS, &flags,
                   sizeof(flags));

  // Заголовки.
  std::wstring headers = L"Content-Type: application/json\r\n";
  if (!bearer.empty()) {
    headers += L"Authorization: Bearer ";
    headers += Utf8ToWide(bearer);
    headers += L"\r\n";
  }

  BOOL ok = WinHttpSendRequest(
      hRequest, headers.c_str(),
      static_cast<DWORD>(headers.size()),
      body.empty() ? WINHTTP_NO_REQUEST_DATA
                   : const_cast<char*>(body.data()),
      static_cast<DWORD>(body.size()),
      static_cast<DWORD>(body.size()), 0);
  if (!ok) {
    WinHttpCloseHandle(hRequest);
    WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);
    return resp;
  }

  ok = WinHttpReceiveResponse(hRequest, nullptr);
  if (!ok) {
    WinHttpCloseHandle(hRequest);
    WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);
    return resp;
  }

  // HTTP status.
  DWORD statusCode = 0;
  DWORD sz = sizeof(statusCode);
  WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_STATUS_CODE |
                      WINHTTP_QUERY_FLAG_NUMBER,
                      WINHTTP_HEADER_NAME_BY_INDEX, &statusCode, &sz,
                      WINHTTP_NO_HEADER_INDEX);
  resp.statusCode = static_cast<long>(statusCode);

  // Чтение тела.
  std::string responseBody;
  DWORD available = 0;
  while (WinHttpQueryDataAvailable(hRequest, &available) && available > 0) {
    std::vector<char> buf(static_cast<size_t>(available));
    DWORD read = 0;
    if (WinHttpReadData(hRequest, buf.data(), available, &read)) {
      responseBody.append(buf.data(), static_cast<size_t>(read));
    }
  }
  resp.body = std::move(responseBody);

  WinHttpCloseHandle(hRequest);
  WinHttpCloseHandle(hConnect);
  WinHttpCloseHandle(hSession);
  return resp;
}

}  // namespace

HttpResponse HttpPostJson(const wchar_t* host, int port,
                          const wchar_t* path,
                          const std::string& jsonBody,
                          const std::string& bearer) {
  return DoRequest(L"POST", host, port, path, jsonBody, bearer);
}

HttpResponse HttpGet(const wchar_t* host, int port,
                     const wchar_t* path,
                     const std::string& bearer) {
  return DoRequest(L"GET", host, port, path, "", bearer);
}
