// Проверки службы со стороны GUI: SCM, родитель, /fromservice.
#include "app_config.h"
#include "service_check.h"

#include <windows.h>
#include <winsvc.h>
#include <tlhelp32.h>
#include <shellapi.h>
#include <stdlib.h>

#pragma comment(lib, "advapi32.lib")

namespace {
SC_HANDLE OpenSvc(DWORD access) {
  const SC_HANDLE scm = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT);
  if (!scm) {
    return nullptr;
  }
  const SC_HANDLE svc = OpenServiceW(scm, PRAKTIKA_SERVICE_NAME, access);
  CloseServiceHandle(scm);
  return svc;
}
}  // namespace

DWORD Praktika_GetServiceProcessId() {
  SC_HANDLE svc = OpenSvc(SERVICE_QUERY_STATUS);
  if (!svc) {
    return 0;
  }
  SERVICE_STATUS_PROCESS ssp{};
  DWORD n = 0;
  if (!QueryServiceStatusEx(svc, SC_STATUS_PROCESS_INFO,
                            reinterpret_cast<LPBYTE>(&ssp), sizeof(ssp), &n)) {
    CloseServiceHandle(svc);
    return 0;
  }
  CloseServiceHandle(svc);
  return ssp.dwProcessId;
}

bool Praktika_BootstrapServiceAndExit() {
  SC_HANDLE svc = OpenSvc(SERVICE_QUERY_STATUS);
  if (!svc) {
    // Службы нет в SCM — задача 2 п.1 неприменима, продолжать работу нельзя.
    MessageBoxW(
        nullptr,
        L"Служба Praktika не зарегистрирована в системе.\n\n"
        L"От администратора:\n"
        L"  PraktikaService.exe install\n"
        L"  sc.exe start PraktikaSvc",
        L"Praktika", MB_OK | MB_ICONINFORMATION);
    return true;
  }
  SERVICE_STATUS st{};
  if (!QueryServiceStatus(svc, &st)) {
    CloseServiceHandle(svc);
    return true;
  }
  if (st.dwCurrentState == SERVICE_RUNNING) {
    CloseServiceHandle(svc);
    return false;  // продолжать обычный путь GUI (далее — проверка родителя)
  }
  // Открываем повторно с правом запуска.
  CloseServiceHandle(svc);
  svc = OpenSvc(SERVICE_START | SERVICE_QUERY_STATUS);
  if (!svc) {
    MessageBoxW(nullptr,
                L"Нет прав на запуск службы. Запустите от администратора:\n"
                L"  sc.exe start PraktikaSvc",
                L"Praktika", MB_OK | MB_ICONWARNING);
    return true;
  }
  (void)StartServiceW(svc, 0, nullptr);
  for (int i = 0; i < 600; ++i) {  // до 60 с
    if (!QueryServiceStatus(svc, &st)) {
      break;
    }
    if (st.dwCurrentState == SERVICE_RUNNING) {
      break;
    }
    Sleep(100);
  }
  CloseServiceHandle(svc);
  return true;  // GUI завершается: служба сама поднимет ребёнка
}

DWORD Praktika_ParseFromServiceArg() {
  int argc = 0;
  LPWSTR* av = CommandLineToArgvW(GetCommandLineW(), &argc);
  if (!av) {
    return 0;
  }
  DWORD pid = 0;
  for (int i = 1; i < argc; ++i) {
    if (_wcsnicmp(av[i], L"/fromservice:", 13) == 0 ||
        _wcsnicmp(av[i], L"-fromservice:", 13) == 0) {
      pid = static_cast<DWORD>(wcstoul(av[i] + 13, nullptr, 10));
      break;
    }
  }
  LocalFree(av);
  return pid;
}

DWORD Praktika_GetParentProcessId() {
  const DWORD self = GetCurrentProcessId();
  const HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
  if (snap == INVALID_HANDLE_VALUE) {
    return 0;
  }
  PROCESSENTRY32W pe{};
  pe.dwSize = sizeof(pe);
  DWORD parent = 0;
  if (Process32FirstW(snap, &pe)) {
    do {
      if (pe.th32ProcessID == self) {
        parent = pe.th32ParentProcessID;
        break;
      }
    } while (Process32NextW(snap, &pe));
  }
  CloseHandle(snap);
  return parent;
}
