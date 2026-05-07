// Praktika — задание 1 + интеграция со службой (задание 2):
//   * При старте: проверка состояния службы; если STOPPED — запустить и выйти
//     (зад.2 GUI п.1). Если RUNNING — проверить, что мы запущены службой
//     (зад.2 GUI п.2: parent == service, либо корректный /fromservice:<pid>).
//   * Меню «Файл → Выход» и «Выход» в трее → RPC RequestStop (зад.2 GUI п.3, п.4).
#include "app_config.h"
#include "service_check.h"
#include "service_client.h"
#include "api_client.h"

#include <windows.h>
#include <shellapi.h>
#include <string>
#include <vector>

#pragma comment(lib, "user32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "advapi32.lib")

namespace {

constexpr wchar_t kWindowClass[] = L"Praktika_TrayWindow";
constexpr wchar_t kWindowTitle[] = L"Praktika";

constexpr UINT kTrayCallbackMsg = WM_APP + 1;
constexpr UINT kTrayIconId = 1;

constexpr wchar_t kArgBackground1[] = L"/background";
constexpr wchar_t kArgBackground2[] = L"-background";
constexpr wchar_t kArgSilent1[] = L"--silent";
constexpr wchar_t kArgSilent2[] = L"-silent";

enum CommandIds : UINT_PTR {
  IDM_FILE_EXIT = 100,
  IDM_TRAY_OPEN = 101,
  IDM_TRAY_EXIT = 102,

  ID_USER_EDIT = 200,
  ID_PASS_EDIT,
  ID_LOGIN_BTN,
  
  ID_CODE_EDIT,
  ID_ACTIVATE_BTN,
  
  ID_LOGOUT_BTN,
  
  ID_TIMER_POLL = 1
};

enum class GuiState {
  Auth,
  Activation,
  Main
};

GuiState g_state = GuiState::Auth;

// UI Controls
HWND g_hAuthPanel = nullptr;
HWND g_hUserEdit = nullptr;
HWND g_hPassEdit = nullptr;
HWND g_hLoginBtn = nullptr;
HWND g_hAuthErr = nullptr;

HWND g_hActPanel = nullptr;
HWND g_hCodeEdit = nullptr;
HWND g_hActBtn = nullptr;
HWND g_hActErr = nullptr;

HWND g_hMainPanel = nullptr;
HWND g_hStatusLbl = nullptr;
HWND g_hUserLbl = nullptr;
HWND g_hLicLbl = nullptr;
HWND g_hLogoutBtn = nullptr;

std::wstring g_currentUser;
std::wstring g_currentExpiry;
bool g_isLicensed = false;

HANDLE g_singletonMutex = nullptr;
UINT g_msgTaskbarCreated = 0;

bool ParseStartHidden() {
  int argc = 0;
  LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
  if (!argv) {
    return false;
  }
  bool hidden = false;
  for (int i = 1; i < argc; ++i) {
    if (_wcsicmp(argv[i], kArgBackground1) == 0 ||
        _wcsicmp(argv[i], kArgBackground2) == 0 ||
        _wcsicmp(argv[i], kArgSilent1) == 0 ||
        _wcsicmp(argv[i], kArgSilent2) == 0) {
      hidden = true;
      break;
    }
  }
  LocalFree(argv);
  return hidden;
}

bool AcquireSingletonOrExit() {
  g_singletonMutex = CreateMutexW(nullptr, FALSE, PRAKTIKA_SINGLETON_MUTEX);
  if (!g_singletonMutex) {
    return true;
  }
  if (GetLastError() == ERROR_ALREADY_EXISTS) {
    CloseHandle(g_singletonMutex);
    g_singletonMutex = nullptr;
    return true;
  }
  return false;
}

bool AddTrayIcon(HWND owner) {
  NOTIFYICONDATAW nid{};
  nid.cbSize = sizeof(nid);
  nid.hWnd = owner;
  nid.uID = kTrayIconId;
  nid.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
  nid.uCallbackMessage = kTrayCallbackMsg;
  nid.hIcon = static_cast<HICON>(LoadImageW(
      nullptr, reinterpret_cast<LPCWSTR>(IDI_APPLICATION), IMAGE_ICON, 0, 0,
      LR_SHARED));
  wcsncpy_s(nid.szTip, L"Praktika", _TRUNCATE);
  return Shell_NotifyIconW(NIM_ADD, &nid) == TRUE;
}

void RemoveTrayIcon(HWND owner) {
  NOTIFYICONDATAW nid{};
  nid.cbSize = sizeof(nid);
  nid.hWnd = owner;
  nid.uID = kTrayIconId;
  Shell_NotifyIconW(NIM_DELETE, &nid);
}

void ShowAndActivateMainWindow(HWND hwnd) {
  if (!IsWindowVisible(hwnd)) {
    ShowWindow(hwnd, SW_SHOW);
  }
  ShowWindow(hwnd, SW_RESTORE);
  SetForegroundWindow(hwnd);
}

void ShowTrayContextMenu(HWND hwnd) {
  HMENU menu = CreatePopupMenu();
  if (!menu) {
    return;
  }
  AppendMenuW(menu, MF_STRING, IDM_TRAY_OPEN, L"Открыть");
  AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
  AppendMenuW(menu, MF_STRING, IDM_TRAY_EXIT, L"Выход");
  POINT pt{};
  GetCursorPos(&pt);
  SetForegroundWindow(hwnd);
  TrackPopupMenu(menu, TPM_RIGHTBUTTON | TPM_BOTTOMALIGN | TPM_LEFTALIGN, pt.x,
                 pt.y, 0, hwnd, nullptr);
  PostMessageW(hwnd, WM_NULL, 0, 0);
  DestroyMenu(menu);
}

void UpdateUI(HWND hwnd) {
  ShowWindow(g_hAuthPanel, g_state == GuiState::Auth ? SW_SHOW : SW_HIDE);
  ShowWindow(g_hActPanel, g_state == GuiState::Activation ? SW_SHOW : SW_HIDE);
  ShowWindow(g_hMainPanel, g_state == GuiState::Main ? SW_SHOW : SW_HIDE);

  if (g_state == GuiState::Main) {
    SetWindowTextW(g_hUserLbl, (L"Пользователь: " + g_currentUser).c_str());
    if (g_isLicensed) {
      SetWindowTextW(g_hStatusLbl, L"Антивирус: АКТИВЕН (разблокирован)");
      SetWindowTextW(g_hLicLbl, (L"Лицензия до: " + g_currentExpiry).c_str());
    } else {
      SetWindowTextW(g_hStatusLbl, L"Антивирус: ЗАБЛОКИРОВАН (нет лицензии)");
      SetWindowTextW(g_hLicLbl, L"Лицензия отсутствует");
    }
  }
}

void CheckState(HWND hwnd) {
  std::wstring username;
  long st = Praktika_RpcGetAuthInfo(username);
  if (st != CYCL_OK || username.empty()) {
    g_state = GuiState::Auth;
    g_currentUser.clear();
    g_isLicensed = false;
    SetWindowTextW(g_hAuthErr, L"");
    UpdateUI(hwnd);
    return;
  }
  
  g_currentUser = username;
  
  bool isLic = false;
  std::wstring expiry;
  st = Praktika_RpcGetLicenseInfo(isLic, expiry);
  
  if (st == CYCL_NO_LICENSE || !isLic) {
    g_isLicensed = false;
    g_state = GuiState::Activation;
    SetWindowTextW(g_hActErr, L"");
  } else {
    g_isLicensed = true;
    g_currentExpiry = expiry;
    g_state = GuiState::Main;
  }
  
  UpdateUI(hwnd);
}

void OnLoginClick(HWND hwnd) {
  wchar_t u[256]{};
  wchar_t p[256]{};
  GetWindowTextW(g_hUserEdit, u, 256);
  GetWindowTextW(g_hPassEdit, p, 256);
  
  SetWindowTextW(g_hAuthErr, L"Подождите...");
  UpdateWindow(g_hAuthErr);
  
  long st = Praktika_RpcLogin(u, p);
  SecureZeroMemory(p, sizeof(p));
  
  if (st != CYCL_OK) {
    SetWindowTextW(g_hAuthErr, L"Ошибка аутентификации.");
  } else {
    CheckState(hwnd);
  }
}

void OnActivateClick(HWND hwnd) {
  wchar_t c[256]{};
  GetWindowTextW(g_hCodeEdit, c, 256);
  
  SetWindowTextW(g_hActErr, L"Подождите...");
  UpdateWindow(g_hActErr);
  
  long st = Praktika_RpcActivateProduct(c);
  if (st != CYCL_OK) {
    SetWindowTextW(g_hActErr, L"Ошибка активации. Проверьте код.");
  } else {
    CheckState(hwnd);
  }
}

void OnLogoutClick(HWND hwnd) {
  Praktika_RpcLogout();
  CheckState(hwnd);
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
  if (msg == g_msgTaskbarCreated) {
    AddTrayIcon(hwnd);
    return 0;
  }
  if (msg == kTrayCallbackMsg) {
    const UINT ev = static_cast<UINT>(lParam);
    if (ev == WM_LBUTTONUP) {
      ShowAndActivateMainWindow(hwnd);
    } else if (ev == WM_RBUTTONUP) {
      ShowTrayContextMenu(hwnd);
    }
    return 0;
  }
  switch (msg) {
  case WM_CREATE: {
    if (!AddTrayIcon(hwnd)) {
      MessageBoxW(hwnd, L"Не удалось добавить иконку в трей.",
                  L"Praktika — ошибка", MB_OK | MB_ICONERROR);
    }
    HMENU bar = CreateMenu();
    HMENU file = CreateMenu();
    AppendMenuW(file, MF_STRING, IDM_FILE_EXIT, L"Выход");
    AppendMenuW(bar, MF_POPUP, reinterpret_cast<UINT_PTR>(file), L"Файл");
    SetMenu(hwnd, bar);
    
    // Create UI Panels
    HFONT hFont = static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
    
    // --- Auth Panel ---
    g_hAuthPanel = CreateWindowExW(0, L"STATIC", nullptr, WS_CHILD | WS_VISIBLE, 0, 0, 600, 400, hwnd, nullptr, nullptr, nullptr);
    CreateWindowExW(0, L"STATIC", L"Вход в систему", WS_CHILD | WS_VISIBLE, 20, 20, 200, 20, g_hAuthPanel, nullptr, nullptr, nullptr);
    CreateWindowExW(0, L"STATIC", L"Логин:", WS_CHILD | WS_VISIBLE, 20, 60, 100, 20, g_hAuthPanel, nullptr, nullptr, nullptr);
    g_hUserEdit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL, 130, 60, 200, 24, g_hAuthPanel, (HMENU)ID_USER_EDIT, nullptr, nullptr);
    CreateWindowExW(0, L"STATIC", L"Пароль:", WS_CHILD | WS_VISIBLE, 20, 100, 100, 20, g_hAuthPanel, nullptr, nullptr, nullptr);
    g_hPassEdit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL | ES_PASSWORD, 130, 100, 200, 24, g_hAuthPanel, (HMENU)ID_PASS_EDIT, nullptr, nullptr);
    g_hLoginBtn = CreateWindowExW(0, L"BUTTON", L"Вход", WS_CHILD | WS_VISIBLE | WS_TABSTOP, 130, 140, 100, 30, g_hAuthPanel, (HMENU)ID_LOGIN_BTN, nullptr, nullptr);
    g_hAuthErr = CreateWindowExW(0, L"STATIC", L"", WS_CHILD | WS_VISIBLE, 20, 180, 400, 20, g_hAuthPanel, nullptr, nullptr, nullptr);
    
    // --- Activation Panel ---
    g_hActPanel = CreateWindowExW(0, L"STATIC", nullptr, WS_CHILD, 0, 0, 600, 400, hwnd, nullptr, nullptr, nullptr);
    CreateWindowExW(0, L"STATIC", L"Активация продукта", WS_CHILD | WS_VISIBLE, 20, 20, 200, 20, g_hActPanel, nullptr, nullptr, nullptr);
    CreateWindowExW(0, L"STATIC", L"Код:", WS_CHILD | WS_VISIBLE, 20, 60, 100, 20, g_hActPanel, nullptr, nullptr, nullptr);
    g_hCodeEdit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL, 130, 60, 200, 24, g_hActPanel, (HMENU)ID_CODE_EDIT, nullptr, nullptr);
    g_hActBtn = CreateWindowExW(0, L"BUTTON", L"Активировать", WS_CHILD | WS_VISIBLE | WS_TABSTOP, 130, 100, 150, 30, g_hActPanel, (HMENU)ID_ACTIVATE_BTN, nullptr, nullptr);
    g_hActErr = CreateWindowExW(0, L"STATIC", L"", WS_CHILD | WS_VISIBLE, 20, 140, 400, 20, g_hActPanel, nullptr, nullptr, nullptr);
    
    // --- Main Panel ---
    g_hMainPanel = CreateWindowExW(0, L"STATIC", nullptr, WS_CHILD, 0, 0, 600, 400, hwnd, nullptr, nullptr, nullptr);
    g_hStatusLbl = CreateWindowExW(0, L"STATIC", L"", WS_CHILD | WS_VISIBLE, 20, 20, 400, 20, g_hMainPanel, nullptr, nullptr, nullptr);
    g_hUserLbl = CreateWindowExW(0, L"STATIC", L"", WS_CHILD | WS_VISIBLE, 20, 60, 400, 20, g_hMainPanel, nullptr, nullptr, nullptr);
    g_hLicLbl = CreateWindowExW(0, L"STATIC", L"", WS_CHILD | WS_VISIBLE, 20, 100, 400, 20, g_hMainPanel, nullptr, nullptr, nullptr);
    g_hLogoutBtn = CreateWindowExW(0, L"BUTTON", L"Выйти", WS_CHILD | WS_VISIBLE | WS_TABSTOP, 20, 140, 100, 30, g_hMainPanel, (HMENU)ID_LOGOUT_BTN, nullptr, nullptr);
    
    EnumChildWindows(hwnd, [](HWND child, LPARAM param) -> BOOL {
      SendMessageW(child, WM_SETFONT, (WPARAM)param, MAKELPARAM(TRUE, 0));
      return TRUE;
    }, (LPARAM)hFont);
    
    SetTimer(hwnd, ID_TIMER_POLL, 5000, nullptr);
    CheckState(hwnd);
    return 0;
  }
  case WM_TIMER:
    if (wParam == ID_TIMER_POLL) {
      CheckState(hwnd);
    }
    return 0;
  case WM_COMMAND: {
    const UINT id = LOWORD(wParam);
    if (id == IDM_TRAY_OPEN) {
      ShowAndActivateMainWindow(hwnd);
    } else if (id == IDM_TRAY_EXIT || id == IDM_FILE_EXIT) {
      Praktika_CallRequestStop();
    } else if (id == ID_LOGIN_BTN) {
      OnLoginClick(hwnd);
    } else if (id == ID_ACTIVATE_BTN) {
      OnActivateClick(hwnd);
    } else if (id == ID_LOGOUT_BTN) {
      OnLogoutClick(hwnd);
    }
    return 0;
  }
  case WM_CLOSE:
    ShowWindow(hwnd, SW_HIDE);
    return 0;
  case WM_DESTROY:
    RemoveTrayIcon(hwnd);
    PostQuitMessage(0);
    return 0;
  default:
    return DefWindowProcW(hwnd, msg, wParam, lParam);
  }
}

}  // namespace

int APIENTRY wWinMain(_In_ HINSTANCE hInstance, _In_opt_ HINSTANCE,
                      _In_ LPWSTR, _In_ int) {
  // Зад.2 GUI п.1: служба остановлена → запустить, дождаться, выйти.
  if (Praktika_BootstrapServiceAndExit()) {
    return 0;
  }

  // Зад.2 GUI п.2: процесс должен быть запущен службой; иначе — выход.
  // На Win10+ CreateProcessAsUser не всегда выставляет parent равным процессу
  // службы, поэтому проверяем И parent==svcPid, И /fromservice:<svcPid>.
  const DWORD svcPid = Praktika_GetServiceProcessId();
  const DWORD parent = Praktika_GetParentProcessId();
  const DWORD fromArg = Praktika_ParseFromServiceArg();
  const bool okParent = (svcPid != 0 && parent == svcPid);
  const bool okFromArg = (svcPid != 0 && fromArg == svcPid && fromArg != 0);
  if (!okParent && !okFromArg) {
    return 0;
  }

  // П.10 задания 1: один экземпляр на пользователя.
  if (AcquireSingletonOrExit()) {
    return 0;
  }

  g_msgTaskbarCreated = RegisterWindowMessageW(L"TaskbarCreated");

  WNDCLASSEXW wc{};
  wc.cbSize = sizeof(wc);
  wc.style = CS_HREDRAW | CS_VREDRAW;
  wc.lpfnWndProc = WndProc;
  wc.hInstance = hInstance;
  wc.hIcon = static_cast<HICON>(LoadImageW(
      nullptr, reinterpret_cast<LPCWSTR>(IDI_APPLICATION), IMAGE_ICON, 0, 0,
      LR_SHARED));
  wc.hCursor = LoadCursorW(nullptr, reinterpret_cast<LPCWSTR>(IDC_ARROW));
  wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
  wc.lpszClassName = kWindowClass;
  if (!RegisterClassExW(&wc)) {
    if (g_singletonMutex) {
      CloseHandle(g_singletonMutex);
    }
    return 1;
  }

  HWND main = CreateWindowExW(0, kWindowClass, kWindowTitle,
                              WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
                              640, 480, nullptr, nullptr, hInstance, nullptr);
  if (!main) {
    if (g_singletonMutex) {
      CloseHandle(g_singletonMutex);
    }
    return 1;
  }

  // Зад.2 п.1 службы: GUI стартует со скрытым окном
  // (служба передаёт /background --silent).
  if (ParseStartHidden()) {
    ShowWindow(main, SW_HIDE);
  } else {
    ShowWindow(main, SW_SHOW);
  }
  UpdateWindow(main);

  MSG m{};
  while (GetMessageW(&m, nullptr, 0, 0) > 0) {
    TranslateMessage(&m);
    DispatchMessageW(&m);
  }

  if (g_singletonMutex) {
    CloseHandle(g_singletonMutex);
    g_singletonMutex = nullptr;
  }
  return static_cast<int>(m.wParam);
}
