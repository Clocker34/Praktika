// scan_engine.h — Антивирусный движок сканирования (задание 3, п.3-5).
// Принимает абстракцию потока байтов и выполняет поиск по АВ-базе.
#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <functional>

namespace scan {

// Результат сканирования одного файла.
struct ScanResult {
  std::wstring filePath;
  bool         isThreat = false;
  std::string  threatName;     // Имя угрозы (если найдена)
  uint64_t     matchOffset = 0; // Смещение, где найдена сигнатура
};

// Результат сканирования директории.
struct DirScanResult {
  uint32_t totalFiles = 0;
  uint32_t scannedFiles = 0;
  uint32_t threatsFound = 0;
  std::vector<ScanResult> threats; // Только файлы с угрозами
  bool     inProgress = false;
  bool     cancelled = false;
};

// Определить ObjectType файла по расширению.
uint8_t DetectObjectType(const std::wstring& filePath);

// п.4: Сканирование одного файла.
ScanResult ScanFile(const std::wstring& filePath);

// п.5: Сканирование директории (рекурсивно).
// progressCb вызывается после каждого файла (для отображения прогресса).
DirScanResult ScanDirectory(const std::wstring& dirPath,
    std::function<bool(uint32_t scanned, uint32_t threats)> progressCb = nullptr);

// Сканирование массива байтов (для тестирования / абстракция потока байтов п.3).
ScanResult ScanBuffer(const uint8_t* data, size_t size, uint8_t objectType,
                      const std::wstring& name = L"<buffer>");

}  // namespace scan
