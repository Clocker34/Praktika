// Praktika — задание 2 + задание 3:
//   Windows-служба + Win32 RPC (ALPC) + WTS-логика
//   + аутентификация/лицензирование через HTTPS + RPC-интерфейс Praktika_Api.
//
// Архитектура:
//   * ServiceMain      — статусы SCM, без приёма STOP/SHUTDOWN (зад.2 п.3).
//   * RpcThread        — ncalrpc-сервер (ALPC):
//                         - PraktikaSvcStop (Praktika_Stop, зад.2 п.4-5)
//                         - PraktikaSvcApi  (Praktika_Api,  зад.3 п.10)
//   * Auth/License     — менеджеры аутентификации и лицензирования (зад.3).
//   * Sessions         — WTS: запуск GUI (зад.2 п.1, п.2).
//   * Доп. баллы: WTSSendMessage, DACL.
#include "../app_config.h"
#include "auth_manager.h"
#include "license_manager.h"
#include "av_database.h"
#include "scan_engine.h"

extern "C" {
#include "Praktika_Stop.h"
#include "Praktika_Api.h"
}

#include <windows.h>
#include <winsvc.h>
#include <wtsapi32.h>
#include <userenv.h>
#include <shlwapi.h>
#include <shellapi.h>

#include <rpc.h>
#include <stdio.h>

#include <aclapi.h>
#include <string>
#include <vector>
#include <sddl.h>

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

// Доп. баллы (процесс службы LOCALSYSTEM): DENY TERMINATE для Everyone,
// полный доступ — SYSTEM и Administrators; дескриптор службы после
// CreateProcess не затрагивается.
bool HardenServiceProcessKill(HANDLE hProcess) {
  if (!hProcess || hProcess == INVALID_HANDLE_VALUE) {
    return false;
  }
  PSID pEveryone = nullptr;
  PSID pSystem = nullptr;
  PSID pAdmins = nullptr;
  SID_IDENTIFIER_AUTHORITY nt = SECURITY_NT_AUTHORITY;

  if (!AllocateAndInitializeSid(&nt, 1, SECURITY_WORLD_RID, 0, 0, 0, 0, 0, 0, 0,
                                &pEveryone)) {
    return false;
  }
  if (!AllocateAndInitializeSid(&nt, 1, SECURITY_LOCAL_SYSTEM_RID, 0, 0, 0, 0, 0,
                                0, 0, &pSystem)) {
    FreeSid(pEveryone);
    return false;
  }
  if (!AllocateAndInitializeSid(
          &nt, 2, SECURITY_BUILTIN_DOMAIN_RID, DOMAIN_ALIAS_RID_ADMINS, 0, 0, 0,
          0, 0, 0, &pAdmins)) {
    FreeSid(pEveryone);
    FreeSid(pSystem);
    return false;
  }

  EXPLICIT_ACCESSW ea[4]{};
  int i = 0;

  ea[i].grfAccessPermissions = PROCESS_TERMINATE;
  ea[i].grfAccessMode = DENY_ACCESS;
  ea[i].grfInheritance = NO_INHERITANCE;
  ea[i].Trustee.TrusteeForm = TRUSTEE_IS_SID;
  ea[i].Trustee.TrusteeType = TRUSTEE_IS_WELL_KNOWN_GROUP;
  ea[i].Trustee.ptstrName = reinterpret_cast<LPWSTR>(pEveryone);
  ++i;

  ea[i].grfAccessPermissions =
      PROCESS_QUERY_LIMITED_INFORMATION | SYNCHRONIZE | PROCESS_VM_READ;
  ea[i].grfAccessMode = GRANT_ACCESS;
  ea[i].grfInheritance = NO_INHERITANCE;
  ea[i].Trustee.TrusteeForm = TRUSTEE_IS_SID;
  ea[i].Trustee.TrusteeType = TRUSTEE_IS_WELL_KNOWN_GROUP;
  ea[i].Trustee.ptstrName = reinterpret_cast<LPWSTR>(pEveryone);
  ++i;

  ea[i].grfAccessPermissions = PROCESS_ALL_ACCESS;
  ea[i].grfAccessMode = GRANT_ACCESS;
  ea[i].grfInheritance = NO_INHERITANCE;
  ea[i].Trustee.TrusteeForm = TRUSTEE_IS_SID;
  ea[i].Trustee.TrusteeType = TRUSTEE_IS_USER;
  ea[i].Trustee.ptstrName = reinterpret_cast<LPWSTR>(pSystem);
  ++i;

  ea[i].grfAccessPermissions = PROCESS_ALL_ACCESS;
  ea[i].grfAccessMode = GRANT_ACCESS;
  ea[i].grfInheritance = NO_INHERITANCE;
  ea[i].Trustee.TrusteeForm = TRUSTEE_IS_SID;
  ea[i].Trustee.TrusteeType = TRUSTEE_IS_GROUP;
  ea[i].Trustee.ptstrName = reinterpret_cast<LPWSTR>(pAdmins);
  ++i;

  PACL pAcl = nullptr;
  const DWORD aceErr = SetEntriesInAclW(i, ea, nullptr, &pAcl);
  if (aceErr != ERROR_SUCCESS || !pAcl) {
    if (pAcl) {
      LocalFree(pAcl);
    }
    FreeSid(pEveryone);
    FreeSid(pSystem);
    FreeSid(pAdmins);
    return false;
  }
  FreeSid(pEveryone);
  FreeSid(pSystem);
  FreeSid(pAdmins);

  const DWORD st = SetSecurityInfo(hProcess, SE_KERNEL_OBJECT,
                                   DACL_SECURITY_INFORMATION, nullptr, nullptr,
                                   pAcl, nullptr);
  LocalFree(pAcl);
  if (st != ERROR_SUCCESS) {
    Log(L"SetSecurityInfo(Harden)", 0, st);
  }
  return st == ERROR_SUCCESS;
}

// Доп. баллы (GUI от имени пользователя): только этот пользователь, SYSTEM и
// Administrators имеют нужные права; остальные не получают TERMINATE
// через OpenProcess (без DENY для Everyone — иначе владелец тоже «попадает»
// под отказ из-за членства во Everyone).
bool HardenGuiProcessKill(HANDLE hProcess, HANDLE hUserPriToken) {
  if (!hProcess || !hUserPriToken) {
    return false;
  }

  DWORD need = 0;
  GetTokenInformation(hUserPriToken, TokenUser, nullptr, 0, &need);
  if (!need || need > 65536u) {
    return false;
  }
  std::vector<BYTE> tokBuf(static_cast<size_t>(need));
  if (!GetTokenInformation(
          hUserPriToken, TokenUser, reinterpret_cast<PTOKEN_USER>(tokBuf.data()),
          need, &need)) {
    return false;
  }
  PSID const pUser = reinterpret_cast<PTOKEN_USER>(tokBuf.data())->User.Sid;

  PSID pSystem = nullptr;
  PSID pAdmins = nullptr;
  SID_IDENTIFIER_AUTHORITY nt = SECURITY_NT_AUTHORITY;
  if (!AllocateAndInitializeSid(&nt, 1, SECURITY_LOCAL_SYSTEM_RID, 0, 0, 0, 0,
                                0, 0, 0, &pSystem)) {
    return false;
  }
  if (!AllocateAndInitializeSid(
          &nt, 2, SECURITY_BUILTIN_DOMAIN_RID, DOMAIN_ALIAS_RID_ADMINS, 0, 0, 0,
          0, 0, 0, &pAdmins)) {
    FreeSid(pSystem);
    return false;
  }

  EXPLICIT_ACCESSW ea[3]{};
  int i = 0;

  ea[i].grfAccessPermissions = PROCESS_TERMINATE | SYNCHRONIZE |
                             PROCESS_QUERY_LIMITED_INFORMATION |
                             PROCESS_VM_READ | READ_CONTROL;
  ea[i].grfAccessMode = GRANT_ACCESS;
  ea[i].grfInheritance = NO_INHERITANCE;
  ea[i].Trustee.TrusteeForm = TRUSTEE_IS_SID;
  ea[i].Trustee.TrusteeType = TRUSTEE_IS_USER;
  ea[i].Trustee.ptstrName = reinterpret_cast<LPWSTR>(pUser);
  ++i;

  ea[i].grfAccessPermissions = PROCESS_ALL_ACCESS;
  ea[i].grfAccessMode = GRANT_ACCESS;
  ea[i].grfInheritance = NO_INHERITANCE;
  ea[i].Trustee.TrusteeForm = TRUSTEE_IS_SID;
  ea[i].Trustee.TrusteeType = TRUSTEE_IS_USER;
  ea[i].Trustee.ptstrName = reinterpret_cast<LPWSTR>(pSystem);
  ++i;

  ea[i].grfAccessPermissions = PROCESS_ALL_ACCESS;
  ea[i].grfAccessMode = GRANT_ACCESS;
  ea[i].grfInheritance = NO_INHERITANCE;
  ea[i].Trustee.TrusteeForm = TRUSTEE_IS_SID;
  ea[i].Trustee.TrusteeType = TRUSTEE_IS_GROUP;
  ea[i].Trustee.ptstrName = reinterpret_cast<LPWSTR>(pAdmins);
  ++i;

  PACL pAcl = nullptr;
  const DWORD aceErr = SetEntriesInAclW(i, ea, nullptr, &pAcl);
  FreeSid(pSystem);
  FreeSid(pAdmins);
  if (aceErr != ERROR_SUCCESS || !pAcl) {
    if (pAcl) {
      LocalFree(pAcl);
    }
    return false;
  }

  const DWORD st = SetSecurityInfo(hProcess, SE_KERNEL_OBJECT,
                                   DACL_SECURITY_INFORMATION, nullptr, nullptr,
                                   pAcl, nullptr);
  LocalFree(pAcl);
  if (st != ERROR_SUCCESS) {
    Log(L"SetSecurityInfo(GUI Harden)", 0, st);
  }
  return st == ERROR_SUCCESS;
}

DWORD FindFirstActiveUserSession() {
  PWTS_SESSION_INFOW arr = nullptr;
  DWORD n = 0;
  if (!WTSEnumerateSessionsW(WTS_CURRENT_SERVER_HANDLE, 0, 1, &arr, &n)) {
    return MAXDWORD;
  }
  DWORD sid = MAXDWORD;
  for (DWORD j = 0; j < n; ++j) {
    if (arr[j].SessionId == 0) {
      continue;
    }
    if (arr[j].State != WTSActive) {
      continue;
    }
    sid = arr[j].SessionId;
    break;
  }
  WTSFreeMemory(arr);
  return sid;
}

// Доп. баллы: диалог подтверждения в интерактивной сессии пользователя
// (аналогично Winlogon secure desktop — отдельный рабочий стол Winlogon здесь
// не поднимается; используется штатный WTSSendMessage в активной сессии).
bool AskUserConfirmStop() {
  const DWORD sid = FindFirstActiveUserSession();
  if (sid == MAXDWORD) {
    Log(L"no active session for stop confirm", 0, 0);
    return false;
  }
  static const wchar_t kTitle[] = L"Praktika";
  static const wchar_t kMsg[] =
      L"Остановить службу Praktika и закрыть значок в трее?";
  DWORD resp = static_cast<DWORD>(-1);
  const BOOL ok = WTSSendMessageW(
      WTS_CURRENT_SERVER_HANDLE, sid, const_cast<LPWSTR>(kTitle),
      static_cast<DWORD>(sizeof(kTitle) - sizeof(wchar_t)),
      const_cast<LPWSTR>(kMsg),
      static_cast<DWORD>(sizeof(kMsg) - sizeof(wchar_t)),
      MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2 | MB_SETFOREGROUND |
          MB_TOPMOST,
      0, &resp, TRUE);
  if (!ok) {
    Log(L"WTSSendMessageW", sid, GetLastError());
    return false;
  }
  return resp == IDYES;
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

  if (!ok) {
    Log(L"CreateProcessAsUserW", sid, GetLastError());
    CloseHandle(hPri);
    return false;
  }
  if (pi.hThread) {
    CloseHandle(pi.hThread);
  }
  (void)HardenGuiProcessKill(pi.hProcess, hPri);
  CloseHandle(hPri);
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
// Зад.3 п.10: регистрируем дополнительный интерфейс Praktika_Api.
DWORD WINAPI RpcThread(LPVOID) {
  static wchar_t kProt[] = L"ncalrpc";

  PSECURITY_DESCRIPTOR pSd = nullptr;
  ConvertStringSecurityDescriptorToSecurityDescriptorW(L"D:(A;;GA;;;WD)", SDDL_REVISION_1, &pSd, nullptr);

  // Эндпоинт 1: Praktika_Stop (зад.2).
  static wchar_t kEpStop[] = L"PraktikaSvcStop";
  RPC_STATUS s = RpcServerUseProtseqEpW(reinterpret_cast<RPC_WSTR>(kProt), 32,
                                        reinterpret_cast<RPC_WSTR>(kEpStop),
                                        pSd);
  if (s != RPC_S_OK && s != RPC_S_DUPLICATE_ENDPOINT) {
    if (pSd) LocalFree(pSd);
    return 1;
  }

  // Эндпоинт 2: Praktika_Api (зад.3 п.10).
  static wchar_t kEpApi[] = PRAKTIKA_RPC_API_ENDPOINT;
  s = RpcServerUseProtseqEpW(reinterpret_cast<RPC_WSTR>(kProt), 32,
                             reinterpret_cast<RPC_WSTR>(kEpApi), pSd);
  if (s != RPC_S_OK && s != RPC_S_DUPLICATE_ENDPOINT) {
    Log(L"RpcServerUseProtseqEpW(Api)", 0, static_cast<DWORD>(s));
  }

  if (pSd) {
    LocalFree(pSd);
  }

  // Регистрируем интерфейс остановки (зад.2 п.5).
  s = RpcServerRegisterIf(Praktika_Stop_v1_0_s_ifspec, nullptr, nullptr);
  if (s != RPC_S_OK) {
    return 2;
  }

  // Регистрируем интерфейс аутентификации/лицензирования (зад.3 п.10).
  s = RpcServerRegisterIf(Praktika_Api_v1_0_s_ifspec, nullptr, nullptr);
  if (s != RPC_S_OK) {
    Log(L"RpcServerRegisterIf(Api)", 0, static_cast<DWORD>(s));
    return 3;
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
    if ((evType == WTS_SESSION_LOGON || evType == WTS_SESSION_RECONNECT || evType == WTS_CONSOLE_CONNECT) && evData) {
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

  (void)HardenServiceProcessKill(GetCurrentProcess());

  // Зад.3: запускаем менеджеры аутентификации и лицензирования.
  auth::Init();
  license::Init();

  LaunchInAllSessions();

  // Ждём, пока RPC-поток не закончит RpcServerListen (по RequestStop).
  WaitForSingleObject(g_rpcThread, INFINITE);
  CloseHandle(g_rpcThread);
  g_rpcThread = nullptr;

  // Зад.3: останавливаем менеджеры.
  license::Shutdown();
  auth::Shutdown();

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
  if (!AskUserConfirmStop()) {
    InterlockedExchange(&g_stopping, 0);
    return;
  }
  RpcMgmtStopServerListening(nullptr);
}

// ----- RPC-методы (Praktika_Api.idl, задание 3 п.10) -----

// П.10a: информация о текущем аутентифицированном пользователе.
extern "C" long GetAuthInfo(handle_t, long bufLen, wchar_t* username) {
  if (!username || bufLen <= 0) return CYCL_AUTH_FAILED;
  username[0] = L'\0';
  if (!auth::IsAuthenticated()) {
    return CYCL_NOT_AUTHENTICATED;
  }
  const std::wstring name = auth::GetUsername();
  wcsncpy_s(username, static_cast<size_t>(bufLen), name.c_str(), _TRUNCATE);
  return CYCL_OK;
}

// П.10b: вход в аккаунт.
extern "C" long Login(handle_t, const wchar_t* username,
                      const wchar_t* password) {
  if (!username || !password) return CYCL_AUTH_FAILED;
  return auth::Login(username, password);
}

// П.10b: выход из аккаунта.
// П.7: при выходе удаляется лицензионный тикет.
extern "C" long Logout(handle_t) {
  license::ClearTicket();  // п.7: удалить тикет при выходе
  auth::Logout();          // п.3: удалить JWT-токены
  return CYCL_OK;
}

// П.10c: информация об активной лицензии.
// П.11: при отсутствии лицензии — ошибка CYCL_NO_LICENSE.
extern "C" long GetLicenseInfo(handle_t, long* pIsLicensed,
                               long expiryBufLen, wchar_t* expiryDate) {
  if (!pIsLicensed) return CYCL_SERVER_ERROR;
  *pIsLicensed = 0;
  if (expiryDate && expiryBufLen > 0) expiryDate[0] = L'\0';

  if (!auth::IsAuthenticated()) {
    return CYCL_NOT_AUTHENTICATED;
  }
  if (!license::IsLicensed()) {
    return CYCL_NO_LICENSE;  // п.11
  }
  *pIsLicensed = 1;
  const std::wstring exp = license::GetExpiryDate();
  if (expiryDate && expiryBufLen > 0) {
    wcsncpy_s(expiryDate, static_cast<size_t>(expiryBufLen),
              exp.c_str(), _TRUNCATE);
  }
  return CYCL_OK;
}

// П.10d: активация продукта.
extern "C" long ActivateProduct(handle_t, const wchar_t* activationCode) {
  if (!activationCode) return CYCL_ACTIVATION_FAILED;
  if (!auth::IsAuthenticated()) return CYCL_NOT_AUTHENTICATED;
  long result = license::Activate(activationCode);
  // п.1: после успешной активации загружаем АВ-базы.
  if (result == CYCL_OK && !av::IsLoaded()) {
    av::LoadDatabase();
  }
  return result;
}

// ---- Задание 3: АВ-движок — RPC-обработчики ----

// п.7: информация об антивирусных базах.
extern "C" long GetAvDbInfo(handle_t, long* pLoaded, long* pRecordCount,
                            long dateBufLen, wchar_t* releaseDate) {
  if (!pLoaded || !pRecordCount || !releaseDate) return CYCL_SERVER_ERROR;
  auto info = av::GetDbInfo();
  *pLoaded = info.loaded ? 1 : 0;
  *pRecordCount = static_cast<long>(info.recordCount);
  if (dateBufLen > 0) {
    wcsncpy_s(releaseDate, dateBufLen, info.releaseDate.c_str(), _TRUNCATE);
  }
  return CYCL_OK;
}

// п.6: сканирование файла.
extern "C" long ScanFilePath(handle_t, const wchar_t* filePath,
                             long* pIsThreat, long nameBufLen,
                             wchar_t* threatName) {
  if (!filePath || !pIsThreat || !threatName) return CYCL_SERVER_ERROR;
  if (!av::IsLoaded()) return CYCL_NO_LICENSE; // Базы не загружены
  auto res = scan::ScanFile(filePath);
  *pIsThreat = res.isThreat ? 1 : 0;
  if (res.isThreat && nameBufLen > 0) {
    std::wstring name(res.threatName.begin(), res.threatName.end());
    wcsncpy_s(threatName, nameBufLen, name.c_str(), _TRUNCATE);
  } else if (nameBufLen > 0) {
    threatName[0] = L'\0';
  }
  return CYCL_OK;
}

// п.6: сканирование директории.
extern "C" long ScanDirPath(handle_t, const wchar_t* dirPath,
                            long* pTotalFiles, long* pScannedFiles,
                            long* pThreatsFound, long listBufLen,
                            wchar_t* threatList) {
  if (!dirPath || !pTotalFiles || !pScannedFiles || !pThreatsFound || !threatList)
    return CYCL_SERVER_ERROR;
  if (!av::IsLoaded()) return CYCL_NO_LICENSE;
  auto dr = scan::ScanDirectory(dirPath);
  *pTotalFiles = static_cast<long>(dr.totalFiles);
  *pScannedFiles = static_cast<long>(dr.scannedFiles);
  *pThreatsFound = static_cast<long>(dr.threatsFound);
  // Формируем список: "path|threatName\n..."
  std::wstring list;
  for (const auto& t : dr.threats) {
    list += t.filePath + L"|";
    list += std::wstring(t.threatName.begin(), t.threatName.end()) + L"\n";
  }
  if (listBufLen > 0) {
    wcsncpy_s(threatList, listBufLen, list.c_str(), _TRUNCATE);
  }
  return CYCL_OK;
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
