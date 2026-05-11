// scan_engine.cpp — Реализация антивирусного движка (задание 3, п.3-5).
// Алгоритм сканирования:
//   3.1: позиция = 0
//   3.2: считать 8 байт → поиск в красно-чёрном дереве
//   3.3: проверки: ObjectType → Offset → Hash
//   3.5: если не найдено → сдвиг на 1 байт → повтор
//   3.6: если найдено → объект вредоносный
#include "scan_engine.h"
#include "av_database.h"

#include <windows.h>
#include <bcrypt.h>
#include <shlwapi.h>
#include <algorithm>
#include <cstring>

#pragma comment(lib, "Bcrypt.lib")
#pragma comment(lib, "Shlwapi.lib")

namespace scan {

// SHA-256 (дублируем из av_database для независимости модуля).
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

// Определить ObjectType по расширению файла.
uint8_t DetectObjectType(const std::wstring& filePath) {
  const wchar_t* ext = PathFindExtensionW(filePath.c_str());
  if (!ext || !*ext) return static_cast<uint8_t>(av::ObjectType::ANY);

  if (_wcsicmp(ext, L".exe") == 0 || _wcsicmp(ext, L".dll") == 0 ||
      _wcsicmp(ext, L".sys") == 0 || _wcsicmp(ext, L".scr") == 0)
    return static_cast<uint8_t>(av::ObjectType::PE_FILE);

  if (_wcsicmp(ext, L".ps1") == 0 || _wcsicmp(ext, L".psm1") == 0)
    return static_cast<uint8_t>(av::ObjectType::POWERSHELL_SCRIPT);

  if (_wcsicmp(ext, L".py") == 0 || _wcsicmp(ext, L".pyw") == 0)
    return static_cast<uint8_t>(av::ObjectType::PYTHON_SCRIPT);

  if (_wcsicmp(ext, L".js") == 0)
    return static_cast<uint8_t>(av::ObjectType::JAVASCRIPT);

  if (_wcsicmp(ext, L".class") == 0)
    return static_cast<uint8_t>(av::ObjectType::JAVA_CLASS);

  // Для файлов без известного расширения — проверяем MZ-заголовок потом.
  return static_cast<uint8_t>(av::ObjectType::ANY);
}

// Ядро: сканирование потока байтов (п.3).
ScanResult ScanBuffer(const uint8_t* data, size_t size, uint8_t objectType,
                      const std::wstring& name) {
  ScanResult result;
  result.filePath = name;

  if (!av::IsLoaded() || size < 8) return result;

  std::lock_guard<std::mutex> lk(av::GetMutex());

  // п.3.1: позиция = 0
  for (size_t pos = 0; pos + 8 <= size; ++pos) {
    // п.3.2: считываем 8 байт и ищем в дереве.
    uint64_t prefix = 0;
    std::memcpy(&prefix, data + pos, 8);

    const auto* records = av::Lookup(prefix);
    if (!records) continue; // Нет записей — сдвиг на 1 (п.3.5)

    // п.3.3: проверяем каждую запись.
    for (const auto& rec : *records) {
      // п.3.3.1: проверка ObjectType (лёгкая).
      if (rec.objectType != av::ObjectType::ANY &&
          static_cast<uint8_t>(rec.objectType) != objectType &&
          objectType != static_cast<uint8_t>(av::ObjectType::ANY)) {
        continue; // Не совпадает — исключаем (п.3.4)
      }

      // п.3.3.2: проверка позиции в диапазоне [OffsetBegin, OffsetEnd].
      if (pos < rec.offsetBegin || pos > rec.offsetEnd) {
        continue;
      }

      // п.3.3.3: считываем дополнительные байты (sigLength - 8).
      uint32_t fullLen = rec.signatureLength;
      if (fullLen < 8) fullLen = 8;
      if (pos + fullLen > size) continue; // Не хватает данных

      // п.3.3.4: хеш от prefix + дополнительные байты.
      std::vector<uint8_t> sigData(data + pos, data + pos + fullLen);
      auto hash = Sha256(sigData.data(), sigData.size());

      // п.3.3.5: сравниваем хеш.
      if (hash == rec.signatureHash) {
        // п.3.6: объект вредоносный!
        result.isThreat = true;
        result.threatName = rec.threatName;
        result.matchOffset = pos;
        return result;
      }
    }
    // п.3.5: все записи не подошли → сдвиг на 1 байт (цикл for).
  }

  return result;
}

// п.4: сканирование файла.
ScanResult ScanFile(const std::wstring& filePath) {
  ScanResult result;
  result.filePath = filePath;

  // Открываем файл.
  HANDLE hFile = CreateFileW(filePath.c_str(), GENERIC_READ, FILE_SHARE_READ,
                             nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (hFile == INVALID_HANDLE_VALUE) return result;

  // Получаем размер.
  LARGE_INTEGER fileSize;
  if (!GetFileSizeEx(hFile, &fileSize) || fileSize.QuadPart == 0) {
    CloseHandle(hFile);
    return result;
  }

  // Ограничение: сканируем до 64 МБ (для производительности).
  size_t scanSize = (size_t)min(fileSize.QuadPart, (LONGLONG)(64 * 1024 * 1024));

  // Маппим файл в память для быстрого доступа.
  HANDLE hMapping = CreateFileMappingW(hFile, nullptr, PAGE_READONLY, 0, 0, nullptr);
  if (!hMapping) {
    CloseHandle(hFile);
    return result;
  }

  const uint8_t* data = (const uint8_t*)MapViewOfFile(hMapping, FILE_MAP_READ,
                                                       0, 0, scanSize);
  if (!data) {
    CloseHandle(hMapping);
    CloseHandle(hFile);
    return result;
  }

  // Определяем тип объекта.
  uint8_t objType = DetectObjectType(filePath);
  // Если тип неизвестен, проверяем MZ-заголовок.
  if (objType == static_cast<uint8_t>(av::ObjectType::ANY) && scanSize >= 2) {
    if (data[0] == 'M' && data[1] == 'Z') {
      objType = static_cast<uint8_t>(av::ObjectType::PE_FILE);
    }
  }

  // Сканируем.
  result = ScanBuffer(data, scanSize, objType, filePath);

  UnmapViewOfFile(data);
  CloseHandle(hMapping);
  CloseHandle(hFile);
  return result;
}

// п.5: сканирование директории (рекурсивно).
DirScanResult ScanDirectory(const std::wstring& dirPath,
    std::function<bool(uint32_t, uint32_t)> progressCb) {
  DirScanResult dr;
  dr.inProgress = true;

  // Рекурсивный обход с помощью FindFirstFile/FindNextFile.
  struct DirEntry {
    std::wstring path;
  };
  std::vector<DirEntry> stack;
  stack.push_back({dirPath});

  while (!stack.empty()) {
    auto current = stack.back();
    stack.pop_back();

    std::wstring searchPath = current.path + L"\\*";
    WIN32_FIND_DATAW fd{};
    HANDLE hFind = FindFirstFileW(searchPath.c_str(), &fd);
    if (hFind == INVALID_HANDLE_VALUE) continue;

    do {
      if (wcscmp(fd.cFileName, L".") == 0 || wcscmp(fd.cFileName, L"..") == 0)
        continue;

      std::wstring fullPath = current.path + L"\\" + fd.cFileName;

      if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
        stack.push_back({fullPath});
      } else {
        dr.totalFiles++;
        auto res = ScanFile(fullPath);
        dr.scannedFiles++;
        if (res.isThreat) {
          dr.threatsFound++;
          dr.threats.push_back(res);
        }
        // Вызываем callback прогресса.
        if (progressCb && !progressCb(dr.scannedFiles, dr.threatsFound)) {
          dr.cancelled = true;
          FindClose(hFind);
          dr.inProgress = false;
          return dr;
        }
      }
    } while (FindNextFileW(hFind, &fd));

    FindClose(hFind);
  }

  dr.inProgress = false;
  return dr;
}

}  // namespace scan
