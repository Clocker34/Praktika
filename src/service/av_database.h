// av_database.h — Антивирусные базы в оперативной памяти (задание 3.1).
// Красно-чёрное дерево (std::map): ключ = первые 8 байт сигнатуры,
// значение = массив записей с совпадающим префиксом.
#pragma once

#include <cstdint>
#include <vector>
#include <map>
#include <string>
#include <mutex>

namespace av {

// п.2.6: тип сканируемого объекта (минимум 2: PE + ещё один).
enum class ObjectType : uint8_t {
  PE_FILE         = 0,   // Portable Executable (.exe, .dll, .sys)
  POWERSHELL_SCRIPT = 1, // PowerShell Script (.ps1)
  PYTHON_SCRIPT   = 2,   // Python Script (.py)
  JAVASCRIPT      = 3,   // JavaScript (.js)
  JAVA_CLASS      = 4,   // Java Class (.class)
  NET_ASSEMBLY    = 5,   // .NET Assembly (.exe/.dll managed)
  ANY             = 255  // Любой тип
};

// Одна запись антивирусной базы (п.2).
struct AvRecord {
  uint64_t    signaturePrefix;   // 2.1: первые 8 байт сигнатуры
  uint32_t    signatureLength;   // 2.2: полная длина сигнатуры (включая префикс)
  std::vector<uint8_t> signatureHash; // 2.3: SHA-256 хеш сигнатуры
  uint64_t    offsetBegin;       // 2.4: начало диапазона позиции
  uint64_t    offsetEnd;         // 2.5: конец диапазона позиции
  ObjectType  objectType;        // 2.6: тип объекта
  std::vector<uint8_t> recordSignature; // 2.7: ЭЦП записи (HMAC-SHA256)
  std::string threatName;        // Имя угрозы (для отображения)
};

// Метаданные базы.
struct DbInfo {
  std::wstring releaseDate;      // Дата выпуска базы
  uint32_t     recordCount = 0;  // Количество записей
  bool         loaded = false;   // Загружена ли база
};

// Красно-чёрное дерево: ключ = signaturePrefix, значение = массив записей.
using AvTree = std::map<uint64_t, std::vector<AvRecord>>;

// Загрузить базу (генерирует тестовые записи, включая EICAR).
void LoadDatabase();

// Получить информацию о базе.
DbInfo GetDbInfo();

// Найти записи по 8-байтовому префиксу.
const std::vector<AvRecord>* Lookup(uint64_t prefix);

// Проверить, загружена ли база.
bool IsLoaded();

// Мьютекс для потокобезопасного доступа.
std::mutex& GetMutex();

// Получить дерево (для отладки/статистики).
const AvTree& GetTree();

}  // namespace av
