// av_database.cpp — Загрузка антивирусных баз в оперативную память.
// Генерирует тестовые сигнатуры (включая EICAR) для демонстрации.
// Использует SHA-256 (Windows BCrypt) для хеширования сигнатур
// и HMAC-SHA256 для ЭЦП записей.
#include "av_database.h"

#include <windows.h>
#include <bcrypt.h>
#include <cstring>
#include <ctime>
#include <algorithm>

#pragma comment(lib, "Bcrypt.lib")

namespace av {

static AvTree   g_tree;
static DbInfo   g_info;
static std::mutex g_mu;

// --- SHA-256 через Windows BCrypt ---
static std::vector<uint8_t> Sha256(const void* data, size_t len) {
  std::vector<uint8_t> hash(32, 0);
  BCRYPT_ALG_HANDLE hAlg = nullptr;
  BCRYPT_HASH_HANDLE hHash = nullptr;
  BCryptOpenAlgorithmProvider(&hAlg, BCRYPT_SHA256_ALGORITHM, nullptr, 0);
  if (!hAlg) return hash;
  BCryptCreateHash(hAlg, &hHash, nullptr, 0, nullptr, 0, 0);
  if (hHash) {
    BCryptHashData(hHash, (PUCHAR)data, (ULONG)len, 0);
    BCryptFinishHash(hHash, hash.data(), (ULONG)hash.size(), 0);
    BCryptDestroyHash(hHash);
  }
  BCryptCloseAlgorithmProvider(hAlg, 0);
  return hash;
}

// --- HMAC-SHA256 для ЭЦП записей (п.2.7) ---
static const uint8_t kHmacKey[] = "PraktikaAV-2026-SigningKey-Secret";

static std::vector<uint8_t> HmacSha256(const void* data, size_t len) {
  std::vector<uint8_t> mac(32, 0);
  BCRYPT_ALG_HANDLE hAlg = nullptr;
  BCRYPT_HASH_HANDLE hHash = nullptr;
  BCryptOpenAlgorithmProvider(&hAlg, BCRYPT_SHA256_ALGORITHM, nullptr,
                              BCRYPT_ALG_HANDLE_HMAC_FLAG);
  if (!hAlg) return mac;
  BCryptCreateHash(hAlg, &hHash, nullptr, 0,
                   (PUCHAR)kHmacKey, sizeof(kHmacKey) - 1, 0);
  if (hHash) {
    BCryptHashData(hHash, (PUCHAR)data, (ULONG)len, 0);
    BCryptFinishHash(hHash, mac.data(), (ULONG)mac.size(), 0);
    BCryptDestroyHash(hHash);
  }
  BCryptCloseAlgorithmProvider(hAlg, 0);
  return mac;
}

// Подсчитать ЭЦП записи (все поля кроме recordSignature).
static std::vector<uint8_t> SignRecord(const AvRecord& r) {
  std::vector<uint8_t> buf;
  auto push = [&](const void* p, size_t n) {
    const auto* b = reinterpret_cast<const uint8_t*>(p);
    buf.insert(buf.end(), b, b + n);
  };
  push(&r.signaturePrefix, 8);
  push(&r.signatureLength, 4);
  push(r.signatureHash.data(), r.signatureHash.size());
  push(&r.offsetBegin, 8);
  push(&r.offsetEnd, 8);
  uint8_t ot = static_cast<uint8_t>(r.objectType);
  push(&ot, 1);
  return HmacSha256(buf.data(), buf.size());
}

// Создать запись для известной сигнатуры.
static AvRecord MakeRecord(const uint8_t* sigBytes, uint32_t sigLen,
                           uint64_t offBegin, uint64_t offEnd,
                           ObjectType type, const char* name) {
  AvRecord r{};
  // Префикс — первые 8 байт (или дополненные нулями).
  r.signaturePrefix = 0;
  size_t prefixLen = (sigLen < 8) ? sigLen : 8;
  std::memcpy(&r.signaturePrefix, sigBytes, prefixLen);

  r.signatureLength = sigLen;
  r.signatureHash = Sha256(sigBytes, sigLen);
  r.offsetBegin = offBegin;
  r.offsetEnd = offEnd;
  r.objectType = type;
  r.threatName = name;
  r.recordSignature = SignRecord(r);
  return r;
}

void LoadDatabase() {
  std::lock_guard<std::mutex> lk(g_mu);
  g_tree.clear();

  // --- EICAR test file signature ---
  // "X5O!P%@AP[4\PZX54(P^)7CC)7}$EICAR"
  // Первые 8 байт: "X5O!P%@A"
  {
    const uint8_t eicar[] = "X5O!P%@AP[4\\PZX54(P^)7CC)7}$EICAR"
                            "-STANDARD-ANTIVIRUS-TEST-FILE!$H+H*";
    auto rec = MakeRecord(eicar, 68, 0, 68, ObjectType::PE_FILE,
                          "EICAR-Test-File");
    g_tree[rec.signaturePrefix].push_back(rec);
  }

  // --- Тестовая PE-сигнатура: MZ header + "This program" ---
  {
    const uint8_t mzMalware[] = { 'M','Z', 0x90, 0x00, 0x03, 0x00, 0x00, 0x00,
                                  0xDE, 0xAD, 0xBE, 0xEF, 0xCA, 0xFE, 0xBA, 0xBE };
    auto rec = MakeRecord(mzMalware, 16, 0, 1024, ObjectType::PE_FILE,
                          "Trojan.Test.MZPattern");
    g_tree[rec.signaturePrefix].push_back(rec);
  }

  // --- PowerShell malicious pattern ---
  {
    // Invoke-Expression + DownloadString — common malware pattern
    const uint8_t ps1[] = "IEX(New-";
    auto rec = MakeRecord(ps1, 8, 0, 65536, ObjectType::POWERSHELL_SCRIPT,
                          "Trojan.PowerShell.DownloadExec");
    g_tree[rec.signaturePrefix].push_back(rec);
  }

  // --- PowerShell encoded command pattern ---
  {
    const uint8_t ps2[] = "-enc JAB"; // -EncodedCommand $var (base64)
    auto rec = MakeRecord((const uint8_t*)ps2, 8, 0, 1024,
                          ObjectType::POWERSHELL_SCRIPT,
                          "Suspicious.PowerShell.EncodedCommand");
    g_tree[rec.signaturePrefix].push_back(rec);
  }

  // --- Python reverse shell pattern ---
  {
    const uint8_t pyFull[] = "import socket,subprocess,os";
    auto rec = MakeRecord(pyFull, 27, 0, 4096, ObjectType::PYTHON_SCRIPT,
                          "Backdoor.Python.ReverseShell");
    g_tree[rec.signaturePrefix].push_back(rec);
  }

  // Подсчитываем общее количество записей.
  uint32_t total = 0;
  for (const auto& [k, v] : g_tree) total += (uint32_t)v.size();

  // Метаданные базы.
  wchar_t dateBuf[32]{};
  SYSTEMTIME st;
  GetLocalTime(&st);
  _snwprintf_s(dateBuf, _TRUNCATE, L"%04d-%02d-%02d %02d:%02d",
               st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute);
  g_info.releaseDate = dateBuf;
  g_info.recordCount = total;
  g_info.loaded = true;
}

DbInfo GetDbInfo() {
  std::lock_guard<std::mutex> lk(g_mu);
  return g_info;
}

const std::vector<AvRecord>* Lookup(uint64_t prefix) {
  // Вызывающий должен держать мьютекс!
  auto it = g_tree.find(prefix);
  if (it == g_tree.end()) return nullptr;
  return &it->second;
}

bool IsLoaded() {
  std::lock_guard<std::mutex> lk(g_mu);
  return g_info.loaded;
}

std::mutex& GetMutex() { return g_mu; }
const AvTree& GetTree() { return g_tree; }

void SetTree(const AvTree& tree) {
  // Вызывающий должен держать мьютекс!
  g_tree = tree;
  uint32_t total = 0;
  for (const auto& [k, v] : g_tree) total += (uint32_t)v.size();
  wchar_t dateBuf[32]{};
  SYSTEMTIME st;
  GetLocalTime(&st);
  _snwprintf_s(dateBuf, _TRUNCATE, L"%04d-%02d-%02d %02d:%02d",
               st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute);
  g_info.releaseDate = dateBuf;
  g_info.recordCount = total;
  g_info.loaded = true;
}

}  // namespace av
