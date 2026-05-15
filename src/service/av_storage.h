// av_storage.h — Хранение антивирусных баз на диске (задание 2.5).
// Бинарный формат: заголовок + записи + манифест (HMAC-SHA256).
#pragma once

#include "av_database.h"
#include <string>

namespace av {

// --- Бинарный формат файла АВ-баз ---
// [Header]  Magic(4) | Version(4) | RecordCount(4) | ManifestHMAC(32) = 44 байт
// [Records] Для каждой записи:
//   signaturePrefix(8) | signatureLength(4) | sigHashLen(4) | sigHash(N)
//   | offsetBegin(8) | offsetEnd(8) | objectType(1)
//   | threatNameLen(4) | threatName(N) | recSigLen(4) | recSig(N)
//
// ManifestHMAC = HMAC-SHA256(все записи, сериализованные)

constexpr uint32_t AVDB_MAGIC   = 0x42445641; // "AVDB" little-endian
constexpr uint32_t AVDB_VERSION = 1;

struct AvDbHeader {
  uint32_t magic;
  uint32_t version;
  uint32_t recordCount;
  uint8_t  manifestHmac[32];
};

// Сериализовать дерево в бинарный файл.
bool SaveToFile(const std::wstring& path, const AvTree& tree);

// Десериализовать из бинарного файла в дерево.
// verifyManifest: проверять HMAC манифеста (п.4).
// verifyRecords: проверять ЭЦП каждой записи, пропуская невалидные (п.7).
// skippedRecords: [out] количество пропущенных записей с невалидной ЭЦП.
bool LoadFromFile(const std::wstring& path, AvTree& tree,
                  bool verifyManifest = true, bool verifyRecords = true,
                  uint32_t* skippedRecords = nullptr);

// Получить путь к файлу баз рядом с exe службы.
std::wstring GetDbFilePath();

// Получить путь к резервной копии.
std::wstring GetBackupFilePath();

// Получить путь к базам по умолчанию.
std::wstring GetDefaultDbFilePath();

// Создать резервную копию (п.необяз.2).
bool CreateBackup();

// Восстановить из резервной копии (п.5).
bool RestoreFromBackup();

// Сгенерировать и сохранить базу по умолчанию (п.2).
bool GenerateDefaultDatabase();

// Загрузить базу с диска с каскадным fallback (п.3-6):
//   1) avdb.bin (проверка манифеста п.4)
//   2) avdb.bin.bak (резервная копия п.5)
//   3) avdb_default.bin (база по умолчанию п.6)
//   4) Генерация в памяти (крайний случай)
bool LoadDatabaseFromDisk();

}  // namespace av
