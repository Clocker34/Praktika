// Минимальный JSON-парсер и Base64-декодер для JWT (задание 3).
// Не полноценный парсер — достаточен для ответов Web-сервиса.
#pragma once

#include <string>

namespace json {

// Извлечь строковое значение по ключу из плоского JSON-объекта.
// Пример: JsonString("{\"access\":\"tok\"}", "access") → "tok"
std::string JsonString(const std::string& json, const char* key);

// Извлечь целочисленное значение по ключу. -1 при отсутствии.
long long JsonInt(const std::string& json, const char* key);

// Извлечь булево значение. false при отсутствии.
bool JsonBool(const std::string& json, const char* key);

}  // namespace json

namespace base64 {

// Декодирование Base64 / Base64url (без паддинга).
std::string Decode(const std::string& encoded);

}  // namespace base64

namespace jwt {

// Извлечь payload (вторая часть) JWT-токена и декодировать из Base64url.
// Возвращает JSON-строку payload.
std::string DecodePayload(const std::string& token);

// Извлечь поле "exp" (Unix timestamp) из JWT.
long long GetExp(const std::string& token);

// Извлечь произвольное строковое поле из payload JWT.
std::string GetClaim(const std::string& token, const char* claim);

}  // namespace jwt
