// RPC-клиент GUI: вызов методов Praktika_Api по ncalrpc/ALPC (задание 3).
// JWT-токены и лицензионные тикеты НЕ передаются клиенту (п.8).
#pragma once

#include <string>

// Получить информацию о текущем аутентифицированном пользователе.
// Возвращает код: 0 = аутентифицирован (username заполнен),
//                 1 = не аутентифицирован.
long Praktika_RpcGetAuthInfo(std::wstring& username);

// Вход в аккаунт.
// Возвращает 0 при успехе, код ошибки при неудаче.
long Praktika_RpcLogin(const wchar_t* username, const wchar_t* password);

// Выход из аккаунта.
long Praktika_RpcLogout();

// Получить информацию о лицензии.
// isLicensed: true если есть лицензия.
// expiryDate: дата истечения (если есть лицензия).
// Возвращает 0 при успехе, код ошибки при неудаче.
long Praktika_RpcGetLicenseInfo(bool& isLicensed, std::wstring& expiryDate);

// Активировать продукт по коду.
// Возвращает 0 при успехе, код ошибки при неудаче.
long Praktika_RpcActivateProduct(const wchar_t* activationCode);
