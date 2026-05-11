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

// ---- Задание 3: АВ-движок ----

// Получить информацию об АВ-базах.
long Praktika_RpcGetAvDbInfo(bool& loaded, long& recordCount, std::wstring& releaseDate);

// Сканировать файл. isThreat: true если угроза.
long Praktika_RpcScanFile(const wchar_t* filePath, bool& isThreat, std::wstring& threatName);

// Сканировать директорию. threatList: "path|name\n..."
long Praktika_RpcScanDir(const wchar_t* dirPath, long& totalFiles,
                         long& scannedFiles, long& threatsFound,
                         std::wstring& threatList);
