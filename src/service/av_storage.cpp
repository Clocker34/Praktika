// av_storage.cpp — Реализация бинарного хранения АВ-баз на диске.
// Задание 2.5: бинарный формат, проверка целостности (HMAC),
// резервное копирование, каскадная загрузка.
#include "av_storage.h"
#include "av_database.h"

#include <windows.h>
#include <bcrypt.h>
#include <shlwapi.h>
#include <cstring>
#include <fstream>
#include <algorithm>

#pragma comment(lib, "Bcrypt.lib")
#pragma comment(lib, "Shlwapi.lib")

namespace av {

// --- HMAC-SHA256 для манифеста ---
static const uint8_t kManifestKey[] = "PraktikaAV-2026-ManifestKey-HMAC";

static std::vector<uint8_t> HmacSha256Manifest(const void* data, size_t len) {
  std::vector<uint8_t> mac(32, 0);
  BCRYPT_ALG_HANDLE hAlg = nullptr;
  BCRYPT_HASH_HANDLE hHash = nullptr;
  BCryptOpenAlgorithmProvider(&hAlg, BCRYPT_SHA256_ALGORITHM, nullptr,
                              BCRYPT_ALG_HANDLE_HMAC_FLAG);
  if (!hAlg) return mac;
  BCryptCreateHash(hAlg, &hHash, nullptr, 0,
                   (PUCHAR)kManifestKey, sizeof(kManifestKey) - 1, 0);
  if (hHash) {
    BCryptHashData(hHash, (PUCHAR)data, (ULONG)len, 0);
    BCryptFinishHash(hHash, mac.data(), (ULONG)mac.size(), 0);
    BCryptDestroyHash(hHash);
  }
  BCryptCloseAlgorithmProvider(hAlg, 0);
  return mac;
}

// HMAC-SHA256 для ЭЦП записи (тот же ключ, что в av_database.cpp).
static const uint8_t kRecordKey[] = "PraktikaAV-2026-SigningKey-Secret";

static std::vector<uint8_t> HmacSha256Record(const void* data, size_t len) {
  std::vector<uint8_t> mac(32, 0);
  BCRYPT_ALG_HANDLE hAlg = nullptr;
  BCRYPT_HASH_HANDLE hHash = nullptr;
  BCryptOpenAlgorithmProvider(&hAlg, BCRYPT_SHA256_ALGORITHM, nullptr,
                              BCRYPT_ALG_HANDLE_HMAC_FLAG);
  if (!hAlg) return mac;
  BCryptCreateHash(hAlg, &hHash, nullptr, 0,
                   (PUCHAR)kRecordKey, sizeof(kRecordKey) - 1, 0);
  if (hHash) {
    BCryptHashData(hHash, (PUCHAR)data, (ULONG)len, 0);
    BCryptFinishHash(hHash, mac.data(), (ULONG)mac.size(), 0);
    BCryptDestroyHash(hHash);
  }
  BCryptCloseAlgorithmProvider(hAlg, 0);
  return mac;
}

// Пересчитать ЭЦП записи для верификации (п.7).
static std::vector<uint8_t> ComputeRecordSignature(const AvRecord& r) {
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
  return HmacSha256Record(buf.data(), buf.size());
}

// --- Сериализация записи в буфер ---
static void SerializeRecord(std::vector<uint8_t>& buf, const AvRecord& r) {
  auto pushRaw = [&](const void* p, size_t n) {
    const auto* b = reinterpret_cast<const uint8_t*>(p);
    buf.insert(buf.end(), b, b + n);
  };
  pushRaw(&r.signaturePrefix, 8);
  pushRaw(&r.signatureLength, 4);
  uint32_t shLen = (uint32_t)r.signatureHash.size();
  pushRaw(&shLen, 4);
  pushRaw(r.signatureHash.data(), shLen);
  pushRaw(&r.offsetBegin, 8);
  pushRaw(&r.offsetEnd, 8);
  uint8_t ot = static_cast<uint8_t>(r.objectType);
  pushRaw(&ot, 1);
  uint32_t tnLen = (uint32_t)r.threatName.size();
  pushRaw(&tnLen, 4);
  pushRaw(r.threatName.data(), tnLen);
  uint32_t rsLen = (uint32_t)r.recordSignature.size();
  pushRaw(&rsLen, 4);
  pushRaw(r.recordSignature.data(), rsLen);
}

// --- Десериализация записи из буфера ---
static bool DeserializeRecord(const uint8_t* data, size_t totalLen,
                              size_t& offset, AvRecord& r) {
  auto readRaw = [&](void* dst, size_t n) -> bool {
    if (offset + n > totalLen) return false;
    std::memcpy(dst, data + offset, n);
    offset += n;
    return true;
  };
  auto readVec = [&](std::vector<uint8_t>& v) -> bool {
    uint32_t len = 0;
    if (!readRaw(&len, 4)) return false;
    if (offset + len > totalLen) return false;
    v.assign(data + offset, data + offset + len);
    offset += len;
    return true;
  };

  if (!readRaw(&r.signaturePrefix, 8)) return false;
  if (!readRaw(&r.signatureLength, 4)) return false;
  if (!readVec(r.signatureHash)) return false;
  if (!readRaw(&r.offsetBegin, 8)) return false;
  if (!readRaw(&r.offsetEnd, 8)) return false;
  uint8_t ot = 0;
  if (!readRaw(&ot, 1)) return false;
  r.objectType = static_cast<ObjectType>(ot);
  // threatName
  uint32_t tnLen = 0;
  if (!readRaw(&tnLen, 4)) return false;
  if (offset + tnLen > totalLen) return false;
  r.threatName.assign((const char*)(data + offset), tnLen);
  offset += tnLen;
  // recordSignature
  if (!readVec(r.recordSignature)) return false;
  return true;
}

// --- Публичный API ---

bool SaveToFile(const std::wstring& path, const AvTree& tree) {
  // Сериализуем все записи.
  std::vector<uint8_t> recordsData;
  uint32_t recordCount = 0;
  for (const auto& [key, records] : tree) {
    for (const auto& r : records) {
      SerializeRecord(recordsData, r);
      ++recordCount;
    }
  }

  // Вычисляем HMAC манифеста (п.4).
  auto hmac = HmacSha256Manifest(recordsData.data(), recordsData.size());

  // Формируем заголовок.
  AvDbHeader hdr{};
  hdr.magic = AVDB_MAGIC;
  hdr.version = AVDB_VERSION;
  hdr.recordCount = recordCount;
  std::memcpy(hdr.manifestHmac, hmac.data(), 32);

  // Записываем файл.
  std::ofstream f(path, std::ios::binary | std::ios::trunc);
  if (!f) return false;
  f.write(reinterpret_cast<const char*>(&hdr), sizeof(hdr));
  f.write(reinterpret_cast<const char*>(recordsData.data()), recordsData.size());
  return f.good();
}

bool LoadFromFile(const std::wstring& path, AvTree& tree,
                  bool verifyManifest, bool verifyRecords,
                  uint32_t* skippedRecords) {
  tree.clear();
  if (skippedRecords) *skippedRecords = 0;

  // Читаем весь файл.
  std::ifstream f(path, std::ios::binary | std::ios::ate);
  if (!f) return false;
  size_t fileSize = (size_t)f.tellg();
  if (fileSize < sizeof(AvDbHeader)) return false;
  f.seekg(0);

  std::vector<uint8_t> fileData(fileSize);
  f.read(reinterpret_cast<char*>(fileData.data()), fileSize);
  if (!f) return false;

  // Разбираем заголовок.
  AvDbHeader hdr{};
  std::memcpy(&hdr, fileData.data(), sizeof(hdr));
  if (hdr.magic != AVDB_MAGIC || hdr.version != AVDB_VERSION) return false;

  const uint8_t* recordsStart = fileData.data() + sizeof(AvDbHeader);
  size_t recordsLen = fileSize - sizeof(AvDbHeader);

  // п.4: Проверка HMAC манифеста.
  if (verifyManifest) {
    auto expectedHmac = HmacSha256Manifest(recordsStart, recordsLen);
    if (std::memcmp(hdr.manifestHmac, expectedHmac.data(), 32) != 0) {
      return false; // Манифест повреждён
    }
  }

  // Десериализуем записи.
  size_t offset = 0;
  for (uint32_t i = 0; i < hdr.recordCount; ++i) {
    AvRecord rec{};
    if (!DeserializeRecord(recordsStart, recordsLen, offset, rec)) {
      break; // Ошибка формата
    }

    // п.7: Проверка ЭЦП записи.
    if (verifyRecords) {
      auto expectedSig = ComputeRecordSignature(rec);
      if (rec.recordSignature != expectedSig) {
        if (skippedRecords) ++(*skippedRecords);
        continue; // Пропускаем запись с невалидной ЭЦП
      }
    }

    tree[rec.signaturePrefix].push_back(std::move(rec));
  }

  return true;
}

// Путь к exe службы.
static std::wstring GetServiceDir() {
  wchar_t me[MAX_PATH]{};
  if (GetModuleFileNameW(nullptr, me, MAX_PATH) == 0) return L".";
  wchar_t* lastSlash = wcsrchr(me, L'\\');
  if (lastSlash) *lastSlash = L'\0';
  else return L".";
  return me;
}

std::wstring GetDbFilePath() {
  return GetServiceDir() + L"\\avdb.bin";
}

std::wstring GetBackupFilePath() {
  return GetServiceDir() + L"\\avdb.bin.bak";
}

std::wstring GetDefaultDbFilePath() {
  return GetServiceDir() + L"\\avdb_default.bin";
}

// п.необяз.2: Резервная копия перед обновлением.
bool CreateBackup() {
  return CopyFileW(GetDbFilePath().c_str(),
                   GetBackupFilePath().c_str(), FALSE) != 0;
}

// п.5: Восстановление из резервной копии.
bool RestoreFromBackup() {
  return CopyFileW(GetBackupFilePath().c_str(),
                   GetDbFilePath().c_str(), FALSE) != 0;
}

// п.2: Сгенерировать базу по умолчанию и сохранить на диск.
bool GenerateDefaultDatabase() {
  // Генерируем тестовые записи в памяти (LoadDatabase()),
  // затем сохраняем дерево в файл.
  LoadDatabase(); // Заполняет внутреннее дерево
  const auto& tree = GetTree();
  return SaveToFile(GetDefaultDbFilePath(), tree);
}

// п.3-6: Каскадная загрузка с диска.
bool LoadDatabaseFromDisk() {
  AvTree tree;
  uint32_t skipped = 0;

  // п.3-4: Пытаемся загрузить основной файл.
  if (LoadFromFile(GetDbFilePath(), tree, true, true, &skipped)) {
    // Успешно — применяем.
    std::lock_guard<std::mutex> lk(GetMutex());
    SetTree(tree);
    return true;
  }

  // п.5: Манифест невалиден — пробуем резервную копию.
  if (LoadFromFile(GetBackupFilePath(), tree, true, true, &skipped)) {
    std::lock_guard<std::mutex> lk(GetMutex());
    SetTree(tree);
    // Восстанавливаем основной файл из бэкапа.
    RestoreFromBackup();
    return true;
  }

  // п.6: Резервная копия тоже повреждена — загружаем базу по умолчанию.
  if (LoadFromFile(GetDefaultDbFilePath(), tree, true, true, &skipped)) {
    std::lock_guard<std::mutex> lk(GetMutex());
    SetTree(tree);
    // Сохраняем как основной файл.
    SaveToFile(GetDbFilePath(), tree);
    return true;
  }

  // Крайний случай: генерируем в памяти и сохраняем.
  LoadDatabase();
  const auto& memTree = GetTree();
  SaveToFile(GetDbFilePath(), memTree);
  SaveToFile(GetDefaultDbFilePath(), memTree);
  return true;
}

}  // namespace av
