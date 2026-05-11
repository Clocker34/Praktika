// Общие имена для службы и RPC.
#pragma once

#define PRAKTIKA_SERVICE_NAME    L"PraktikaSvc"
#define PRAKTIKA_SERVICE_DISPLAY L"Praktika Tray Service"
#define PRAKTIKA_SERVICE_DESC \
  L"Praktika: Windows-служба + RPC (ALPC) + Win32 трей."

// Имя GUI-исполняемого, ищется рядом со службой.
#define PRAKTIKA_GUI_EXE L"Praktika.exe"

// П.10 задания 1: один процесс GUI на пользователя.
#define PRAKTIKA_SINGLETON_MUTEX L"Local\\Praktika_SingleInstance"

// ---- Задание 3: API endpoints ----

// Базовый URL Web-сервиса (Spring Boot, HTTP).
#define PRAKTIKA_API_HOST     L"localhost"
#define PRAKTIKA_API_PORT     8082
#define PRAKTIKA_API_USE_TLS  0   // 0 = HTTP, 1 = HTTPS

// Эндпоинты аутентификации (AuthJwtController).
#define PRAKTIKA_API_LOGIN    L"/auth/login"
#define PRAKTIKA_API_REFRESH  L"/auth/refresh"

// Эндпоинты лицензирования (LicenseController).
#define PRAKTIKA_API_LICENSE  L"/api/licenses/check"
#define PRAKTIKA_API_ACTIVATE L"/api/licenses/activate"

// RPC-эндпоинт для Praktika_Api (задание 3 п.10).
#define PRAKTIKA_RPC_API_ENDPOINT L"PraktikaSvcApi"

// Коды ошибок RPC-интерфейса Praktika_Api.
#define CYCL_OK               0L
#define CYCL_NOT_AUTHENTICATED 1L
#define CYCL_AUTH_FAILED       2L
#define CYCL_NO_LICENSE        3L
#define CYCL_ACTIVATION_FAILED 4L
#define CYCL_HTTP_ERROR        5L
#define CYCL_SERVER_ERROR      6L
