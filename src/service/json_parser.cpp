// Минимальный JSON-парсер и Base64 для JWT (задание 3).
#include "json_parser.h"

#include <cstring>
#include <cstdlib>
#include <algorithm>

// ---- JSON ----

namespace json {

// Ищем "key": "value" или "key": number в плоском JSON.
static std::string FindValue(const std::string& json, const char* key) {
  std::string needle = "\"";
  needle += key;
  needle += "\"";
  auto pos = json.find(needle);
  if (pos == std::string::npos) return {};
  pos = json.find(':', pos + needle.size());
  if (pos == std::string::npos) return {};
  ++pos;
  while (pos < json.size() && (json[pos] == ' ' || json[pos] == '\t'))
    ++pos;
  if (pos >= json.size()) return {};
  if (json[pos] == '"') {
    // Строковое значение.
    ++pos;
    auto end = pos;
    while (end < json.size() && json[end] != '"') {
      if (json[end] == '\\') ++end;  // skip escape
      ++end;
    }
    return json.substr(pos, end - pos);
  }
  // Число или bool.
  auto end = pos;
  while (end < json.size() && json[end] != ',' && json[end] != '}' &&
         json[end] != ' ' && json[end] != '\n')
    ++end;
  return json.substr(pos, end - pos);
}

std::string JsonString(const std::string& json, const char* key) {
  return FindValue(json, key);
}

long long JsonInt(const std::string& json, const char* key) {
  const std::string v = FindValue(json, key);
  if (v.empty()) return -1;
  char* endp = nullptr;
  const long long r = strtoll(v.c_str(), &endp, 10);
  return (endp && endp != v.c_str()) ? r : -1;
}

bool JsonBool(const std::string& json, const char* key) {
  const std::string v = FindValue(json, key);
  return v == "true";
}

}  // namespace json

// ---- Base64 ----

namespace base64 {

static const char kTable[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static int B64Val(char c) {
  if (c >= 'A' && c <= 'Z') return c - 'A';
  if (c >= 'a' && c <= 'z') return c - 'a' + 26;
  if (c >= '0' && c <= '9') return c - '0' + 52;
  if (c == '+' || c == '-') return 62;  // '-' = base64url
  if (c == '/' || c == '_') return 63;  // '_' = base64url
  return -1;
}

std::string Decode(const std::string& encoded) {
  std::string out;
  out.reserve(encoded.size() * 3 / 4);
  int val = 0, bits = -8;
  for (char c : encoded) {
    if (c == '=' || c == '\0') break;
    const int v = B64Val(c);
    if (v < 0) continue;
    val = (val << 6) | v;
    bits += 6;
    if (bits >= 0) {
      out.push_back(static_cast<char>((val >> bits) & 0xFF));
      bits -= 8;
    }
  }
  return out;
}

}  // namespace base64

// ---- JWT ----

namespace jwt {

std::string DecodePayload(const std::string& token) {
  // JWT = header.payload.signature
  auto first = token.find('.');
  if (first == std::string::npos) return {};
  auto second = token.find('.', first + 1);
  if (second == std::string::npos) second = token.size();
  const std::string payload = token.substr(first + 1, second - first - 1);
  return base64::Decode(payload);
}

long long GetExp(const std::string& token) {
  const std::string pl = DecodePayload(token);
  if (pl.empty()) return 0;
  return json::JsonInt(pl, "exp");
}

std::string GetClaim(const std::string& token, const char* claim) {
  const std::string pl = DecodePayload(token);
  if (pl.empty()) return {};
  return json::JsonString(pl, claim);
}

}  // namespace jwt
