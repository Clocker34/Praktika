// Praktika — задание 2: Windows-служба + Win32 RPC (ALPC) + WTS-логика.
//
// Архитектура:
//   * ServiceMain      — статусы SCM, без приёма STOP/SHUTDOWN (п.3 ТЗ).
//   * RpcThread        — ncalrpc-сервер (ALPC), эндпоинт PraktikaSvcStop;
//                         выходит по RpcMgmtStopServerListening (п.4 ТЗ).
//   * RequestStop      — RPC-метод (Praktika_Stop.idl, п.5 ТЗ).
//   * Sessions         — WTSEnumerateSessions/SESSION_LOGON: запуск GUI в
//                         сессиях (≠0), от имени владельца, окно скрыто
//                         (п.1, п.2 ТЗ).
//   * KillAllChildren  — TerminateProcess по PID запущенных GUI (п.6 ТЗ).
//   * install/uninstall— регистрация в SCM одной командой
//                         (PraktikaService.exe install).
#include "../app_config.h"

extern "C" {
#include "Praktika_Stop.h"
}

#include <windows.h>
#include <winsvc.h>
#include <wtsapi32.h>
#include <userenv.h>
#include <shlwapi.h>
#include <shellapi.h>

#include <rpc.h>
#include <stdio.h>

#include <string>
#include <vector>

#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "Wtsapi32.lib")
#pragma comment(lib, "Userenv.lib")
#pragma comment(lib, "Shlwapi.lib")
#pragma comment(lib, "Rpcrt4.lib")

namespace {

SERVICE_STATUS g_ss{};
SERVICE_STATUS_HANDLE g_ssh = nullptr;

CRITICAL_SECTION g_csChildren;
std::vector<HANDLE> g_childProcs;  // дескрипторы процессов GUI
std::wstring g_guiPath;

HANDLE g_rpcThread = nullptr;
volatile LONG g_stopping = 0;

void Log(const wchar_t* what, DWORD sid, DWORD err) {
  wchar_t b[300]{};
  _snwprintf_s(b, _TRUNCATE, L"PraktikaSvc: %ls sid=%lu err=%lu\n", what,
               static_cast<unsigned long>(sid),
               static_cast<unsigned long>(err));
  OutputDebugStringW(b);
}

void TrackChild(HANDLE h) {
  if (!h) {
    return;
  }
  EnterCriticalSection(&g_csChildren);
  g_childProcs.push_back(h);
  LeaveCriticalSection(&g_csChildren);
}

// П.6 ТЗ: при остановке завершить все GUI.
void KillAllChildren() {
  std::vector<HANDLE> copy;
  EnterCriticalSection(&g_csChildren);
  copy.swap(g_childProcs);
  LeaveCriticalSection(&g_csChildren);
  for (HANDLE h : copy) {
    TerminateProcess(h, 0);
    CloseHandle(h);
  }
}

void SetState(DWORD state, DWORD acc) {
  g_ss.dwServiceType = SERVICE_WIN32_OWN_PROCESS;
  g_ss.dwCurrentState = state;
  g_ss.dwControlsAccepted = acc;
  g_ss.dwCheckPoint = 0;
  g_ss.dwWin32ExitCode = 0;
  g_ss.dwServiceSpecificExitCode = 0;
  g_ss.dwWaitHint = 0;
  if (g_ssh) {
    SetServiceStatus(g_ssh, &g_ss);
  }
}

bool BuildGuiPath() {
  WCHAR me[MAX_PATH]{};
  if (!GetModuleFileNameW(nullptr, me, MAX_PATH)) {
    return false;
  }
  PathRemoveFileSpecW(me);
  g_guiPath = me;
  g_guiPath += L"\\";
  g_guiPath += PRAKTIKA_GUI_EXE;
  return PathFileExistsW(g_guiPath.c_str()) != FALSE;
}

// П.1, п.2 ТЗ: запустить GUI в сессии sid от имени её владельца, окно скрыто.
bool LaunchInSession(DWORD sid) {
  if (sid == 0) {
    return false;
  }
  if (g_guiPath.empty()) {
    return false;
  }

  HANDLE hUser = nullptr;
  if (!WTSQueryUserToken(sid, &hUser)) {
    Log(L"WTSQueryUserToken", sid, GetLastError());
    return false;
  }
  // DuplicateTokenEx даёт primary-токен, пригодный для CreateProcessAsUser.
  HANDLE hPri = nullptr;
  if (!DuplicateTokenEx(hUser, MAXIMUM_ALLOWED, nullptr, SecurityIdentification,
                        TokenPrimary, &hPri)) {
    Log(L"DuplicateTokenEx", sid, GetLastError());
    CloseHandle(hUser);
    return false;
  }
  CloseHandle(hUser);

  LPVOID env = nullptr;
  (void)CreateEnvironmentBlock(&env, hPri, FALSE);

  STARTUPINFOW si{};
  si.cb = sizeof(si);
  si.dwFlags = STARTF_USESHOWWINDOW;
  si.wShowWindow = SW_HIDE;  // GUI стартует скрыто (для соответствия ТЗ)
  si.lpDesktop = const_cast<LPWSTR>(L"winsta0\\default");

  // Передаём свой PID через /fromservice:<pid> — это позволяет GUI на
  // Win10+ корректно проверить «родитель = служба», даже если ОС не
  // ставит реального parent у CreateProcessAsUserW.
  std::wstring cl = L"\"";
  cl += g_guiPath;
  cl += L"\" /background --silent /fromservice:";
  cl += std::to_wstring(GetCurrentProcessId());
  std::vector<wchar_t> buf(cl.begin(), cl.end());
  buf.push_back(L'\0');

  PROCESS_INFORMATION pi{};
  const BOOL ok = CreateProcessAsUserW(
      hPri, g_guiPath.c_str(), buf.data(), nullptr, nullptr, FALSE,
      CREATE_UNICODE_ENVIRONMENT | CREATE_NO_WINDOW, env, nullptr, &si, &pi);

  if (env) {
    DestroyEnvironmentBlock(env);
  }
  CloseHandle(hPri);

  if (!ok) {
    Log(L"CreateProcessAsUserW", sid, GetLastError());
    return false;
  }
  if (pi.hThread) {
    CloseHandle(pi.hThread);
  }
  TrackChild(pi.hProcess);  // оставляем HANDLE для KillAllChildren()
  return true;
}

// П.1 ТЗ: при старте — все активные сессии (≠0).
void LaunchInAllSessions() {
  PWTS_SESSION_INFOW arr = nullptr;
  DWORD n = 0;
  if (!WTSEnumerateSessionsW(WTS_CURRENT_SERVER_HANDLE, 0, 1, &arr, &n)) {
    Log(L"WTSEnumerateSessionsW", 0, GetLastError());
    return;
  }
  for (DWORD i = 0; i < n; ++i) {
    if (arr[i].SessionId == 0) {
      continue;
    }
    if (arr[i].State != WTSActive) {
      continue;
    }
    (void)LaunchInSession(arr[i].SessionId);
  }
  WTSFreeMemory(arr);
}

// П.4 ТЗ: ncalrpc/ALPC; завершается по RpcMgmtStopServerListening.
DWORD WINAPI RpcThread(LPVOID) {
  static wchar_t kProt[] = L"ncalrpc";
  static wchar_t kEp[] = L"PraktikaSvcStop";
  RPC_STATUS s = RpcServerUseProtseqEpW(reinterpret_cast<RPC_WSTR>(kProt), 32,
                                        reinterpret_cast<RPC_WSTR>(kEp),
                                        nullptr);
  if (s != RPC_S_OK && s != RPC_S_DUPLICATE_ENDPOINT) {
    return 1;
  }
  // П.5 ТЗ: регистрируем интерфейс остановки.
  s = RpcServerRegisterIf2(Praktika_Stop_v1_0_s_ifspec, nullptr, nullptr,
                           RPC_IF_ALLOW_LOCAL_ONLY,
                           RPC_C_LISTEN_MAX_CALLS_DEFAULT, (unsigned)-1,
                           nullptr);
  if (s != RPC_S_OK) {
    s = RpcServerRegisterIf(Praktika_Stop_v1_0_s_ifspec, nullptr, nullptr);
    if (s != RPC_S_OK) {
      return 2;
    }
  }
  (void)RpcServerListen(1, RPC_C_LISTEN_MAX_CALLS_DEFAULT, FALSE);
  return 0;
}

// П.3 ТЗ: STOP/SHUTDOWN не принимаем; SESSIONCHANGE — отслеживаем (п.2 ТЗ).
DWORD WINAPI HandlerEx(DWORD ctrl, DWORD evType, LPVOID evData, LPVOID) {
  switch (ctrl) {
  case SERVICE_CONTROL_STOP:
  case SERVICE_CONTROL_SHUTDOWN:
    return ERROR_CALL_NOT_IMPLEMENTED;
  case SERVICE_CONTROL_INTERROGATE:
    return NO_ERROR;
  case SERVICE_CONTROL_SESSIONCHANGE:
    if (evType == WTS_SESSION_LOGON && evData) {
      const auto* n =
          static_cast<const WTSSESSION_NOTIFICATION*>(evData);
      if (n->cbSize >= sizeof(WTSSESSION_NOTIFICATION) &&
          n->dwSessionId != 0) {
        Sleep(2000);  // дать сессии прогрузиться
        (void)LaunchInSession(n->dwSessionId);
      }
    }
    return NO_ERROR;
  default:
    return ERROR_CALL_NOT_IMPLEMENTED;
  }
}

void WINAPI ServiceMain(DWORD, LPWSTR*) {
  g_ssh = RegisterServiceCtrlHandlerExW(PRAKTIKA_SERVICE_NAME, HandlerEx,
                                        nullptr);
  if (!g_ssh) {
    return;
  }
  InitializeCriticalSection(&g_csChildren);
  SetState(SERVICE_START_PENDING, 0);
  if (!BuildGuiPath()) {
    Log(L"GUI exe missing in service folder", 0, 0);
    SetState(SERVICE_STOPPED, 0);
    DeleteCriticalSection(&g_csChildren);
    return;
  }

  g_rpcThread = CreateThread(nullptr, 0, RpcThread, nullptr, 0, nullptr);
  if (!g_rpcThread) {
    SetState(SERVICE_STOPPED, 0);
    DeleteCriticalSection(&g_csChildren);
    return;
  }

  // dwControlsAccepted: только SESSIONCHANGE — не принимаем STOP/SHUTDOWN.
  SetState(SERVICE_RUNNING, SERVICE_ACCEPT_SESSIONCHANGE);

  LaunchInAllSessions();

  // Ждём, пока RPC-поток не закончит RpcServerListen (по RequestStop).
  WaitForSingleObject(g_rpcThread, INFINITE);
  CloseHandle(g_rpcThread);
  g_rpcThread = nullptr;

  KillAllChildren();
  SetState(SERVICE_STOPPED, 0);
  DeleteCriticalSection(&g_csChildren);
}

}  // namespace

// ----- RPC-метод (Praktika_Stop.idl) -----
extern "C" void RequestStop(handle_t) {
  if (InterlockedCompareExchange(&g_stopping, 1, 0) != 0) {
    return;
  }
  // Останавливаем приём новых RPC; сами завершаем текущий вызов и выходим.
  RpcMgmtStopServerListening(nullptr);
}

// ----- install / uninstall -----
namespace {
int DoInstall() {
  SC_HANDLE scm = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CREATE_SERVICE);
  if (!scm) {
    return static_cast<int>(GetLastError());
  }
  WCHAR path[MAX_PATH]{};
  if (!GetModuleFileNameW(nullptr, path, MAX_PATH)) {
    CloseServiceHandle(scm);
    return static_cast<int>(GetLastError());
  }
  SC_HANDLE svc = CreateServiceW(
      scm, PRAKTIKA_SERVICE_NAME, PRAKTIKA_SERVICE_DISPLAY, SERVICE_ALL_ACCESS,
      SERVICE_WIN32_OWN_PROCESS, SERVICE_AUTO_START, SERVICE_ERROR_NORMAL, path,
      nullptr, nullptr, nullptr, nullptr, nullptr);
  if (!svc) {
    const DWORD e = GetLastError();
    CloseServiceHandle(scm);
    if (e == ERROR_SERVICE_EXISTS) {
      return 0;
    }
    return static_cast<int>(e);
  }
  SERVICE_DESCRIPTIONW d{};
  static wchar_t kDesc[] = PRAKTIKA_SERVICE_DESC;
  d.lpDescription = kDesc;
  ChangeServiceConfig2W(svc, SERVICE_CONFIG_DESCRIPTION, &d);
  CloseServiceHandle(svc);
  CloseServiceHandle(scm);
  return 0;
}

int DoUninstall() {
  SC_HANDLE scm = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_ALL_ACCESS);
  if (!scm) {
    return static_cast<int>(GetLastError());
  }
  SC_HANDLE svc = OpenServiceW(scm, PRAKTIKA_SERVICE_NAME, DELETE);
  if (!svc) {
    const DWORD e = GetLastError();
    CloseServiceHandle(scm);
    return static_cast<int>(e);
  }
  const BOOL ok = DeleteService(svc);
  const DWORD e = ok ? 0U : GetLastError();
  CloseServiceHandle(svc);
  CloseServiceHandle(scm);
  return static_cast<int>(e);
}
}  // namespace

int wmain(int argc, wchar_t** argv) {
  if (argc >= 2) {
    if (_wcsicmp(argv[1], L"install") == 0) {
      const int r = DoInstall();
      wprintf(L"PraktikaService install: %d\n", r);
      return r;
    }
    if (_wcsicmp(argv[1], L"uninstall") == 0) {
      const int r = DoUninstall();
      wprintf(L"PraktikaService uninstall: %d\n", r);
      return r;
    }
  }
  SERVICE_TABLE_ENTRYW table[] = {
      {const_cast<LPWSTR>(PRAKTIKA_SERVICE_NAME), ServiceMain},
      {nullptr, nullptr},
  };
  if (!StartServiceCtrlDispatcherW(table)) {
    const DWORD e = GetLastError();
    wprintf(L"StartServiceCtrlDispatcher: %lu\n"
            L"Запустите 'PraktikaService.exe install' от администратора.\n",
            static_cast<unsigned long>(e));
    return static_cast<int>(e);
  }
  return 0;
}
