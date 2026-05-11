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
#include <thread>

#pragma comment(lib, "user32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "gdi32.lib")

// ---- Dark Theme Colors ----
static constexpr COLORREF CLR_BG        = RGB(24, 24, 36);    // main background
static constexpr COLORREF CLR_CARD      = RGB(34, 34, 52);    // card panels
static constexpr COLORREF CLR_TEXT      = RGB(220, 220, 235);  // primary text
static constexpr COLORREF CLR_TEXT_DIM  = RGB(140, 140, 165);  // secondary text
static constexpr COLORREF CLR_ACCENT    = RGB(88, 101, 242);   // Discord-blue accent
static constexpr COLORREF CLR_GREEN     = RGB(87, 242, 135);   // success green
static constexpr COLORREF CLR_RED       = RGB(237, 66, 69);    // error red
static constexpr COLORREF CLR_EDIT_BG   = RGB(44, 44, 64);     // edit field bg
static constexpr COLORREF CLR_EDIT_TEXT = RGB(230, 230, 245);  // edit field text
static constexpr COLORREF CLR_YELLOW    = RGB(254, 231, 92);   // warning

static HBRUSH g_hBrBg     = nullptr;
static HBRUSH g_hBrCard   = nullptr;
static HBRUSH g_hBrEdit   = nullptr;
static HFONT  g_hFontUI   = nullptr;   // 14px Segoe UI
static HFONT  g_hFontTitle = nullptr;  // 20px Segoe UI Semibold
static HFONT  g_hFontSmall = nullptr;  // 12px Segoe UI

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

constexpr UINT WM_APP_STATE_UPDATED = WM_APP + 100;

enum class GuiState {
  Auth,
  Activation,
  Main
};

GuiState g_state = GuiState::Auth;

// Background state for async polling
struct AsyncState {
  GuiState newState = GuiState::Auth;
  std::wstring username;
  std::wstring expiry;
  bool isLicensed = false;
};

AsyncState g_asyncResult;
bool g_pollInProgress = false;

// UI Controls
HWND g_hAuthTitle = nullptr;
HWND g_hUserLbl = nullptr;
HWND g_hUserEdit = nullptr;
HWND g_hPassLbl = nullptr;
HWND g_hPassEdit = nullptr;
HWND g_hLoginBtn = nullptr;
HWND g_hAuthErr = nullptr;

HWND g_hActTitle = nullptr;
HWND g_hCodeLbl = nullptr;
HWND g_hCodeEdit = nullptr;
HWND g_hActBtn = nullptr;
HWND g_hActErr = nullptr;

HWND g_hMainTitle = nullptr;
HWND g_hStatusLbl = nullptr;
HWND g_hMainUserLbl = nullptr;
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
  bool isAuth = (g_state == GuiState::Auth);
  ShowWindow(g_hAuthTitle, isAuth ? SW_SHOW : SW_HIDE);
  ShowWindow(g_hUserLbl, isAuth ? SW_SHOW : SW_HIDE);
  ShowWindow(g_hUserEdit, isAuth ? SW_SHOW : SW_HIDE);
  ShowWindow(g_hPassLbl, isAuth ? SW_SHOW : SW_HIDE);
  ShowWindow(g_hPassEdit, isAuth ? SW_SHOW : SW_HIDE);
  ShowWindow(g_hLoginBtn, isAuth ? SW_SHOW : SW_HIDE);
  ShowWindow(g_hAuthErr, isAuth ? SW_SHOW : SW_HIDE);

  bool isAct = (g_state == GuiState::Activation);
  ShowWindow(g_hActTitle, isAct ? SW_SHOW : SW_HIDE);
  ShowWindow(g_hCodeLbl, isAct ? SW_SHOW : SW_HIDE);
  ShowWindow(g_hCodeEdit, isAct ? SW_SHOW : SW_HIDE);
  ShowWindow(g_hActBtn, isAct ? SW_SHOW : SW_HIDE);
  ShowWindow(g_hActErr, isAct ? SW_SHOW : SW_HIDE);

  bool isMain = (g_state == GuiState::Main);
  ShowWindow(g_hMainTitle, isMain ? SW_SHOW : SW_HIDE);
  ShowWindow(g_hStatusLbl, isMain ? SW_SHOW : SW_HIDE);
  ShowWindow(g_hMainUserLbl, isMain ? SW_SHOW : SW_HIDE);
  ShowWindow(g_hLicLbl, isMain ? SW_SHOW : SW_HIDE);
  ShowWindow(g_hLogoutBtn, isMain ? SW_SHOW : SW_HIDE);

  if (isMain) {
    SetWindowTextW(g_hMainUserLbl, (L"Пользователь: " + g_currentUser).c_str());
    if (g_isLicensed) {
      SetWindowTextW(g_hStatusLbl, L"Антивирус: АКТИВЕН (разблокирован)");
      SetWindowTextW(g_hLicLbl, (L"Лицензия до: " + g_currentExpiry).c_str());
    } else {
      SetWindowTextW(g_hStatusLbl, L"Антивирус: ЗАБЛОКИРОВАН (нет лицензии)");
      SetWindowTextW(g_hLicLbl, L"Лицензия отсутствует");
    }
  }

  InvalidateRect(hwnd, nullptr, TRUE);
  UpdateWindow(hwnd);
}

// Background thread: does RPC without blocking UI.
void CheckStateAsync(HWND hwnd) {
  if (g_pollInProgress) return;
  g_pollInProgress = true;

  std::thread([hwnd]() {
    AsyncState as;
    std::wstring username;
    long st = Praktika_RpcGetAuthInfo(username);
    if (st != CYCL_OK || username.empty()) {
      as.newState = GuiState::Auth;
      as.username.clear();
      as.isLicensed = false;
    } else {
      as.username = username;
      bool isLic = false;
      std::wstring expiry;
      st = Praktika_RpcGetLicenseInfo(isLic, expiry);
      if (st == CYCL_NO_LICENSE || !isLic) {
        as.newState = GuiState::Activation;
        as.isLicensed = false;
      } else {
        as.newState = GuiState::Main;
        as.isLicensed = true;
        as.expiry = expiry;
      }
    }
    g_asyncResult = as;
    PostMessageW(hwnd, WM_APP_STATE_UPDATED, 0, 0);
  }).detach();
}

// Called on UI thread when background poll completes.
void ApplyAsyncState(HWND hwnd) {
  g_pollInProgress = false;
  g_state = g_asyncResult.newState;
  g_currentUser = g_asyncResult.username;
  g_isLicensed = g_asyncResult.isLicensed;
  g_currentExpiry = g_asyncResult.expiry;
  if (g_state == GuiState::Auth) {
    SetWindowTextW(g_hAuthErr, L"");
  } else if (g_state == GuiState::Activation) {
    SetWindowTextW(g_hActErr, L"");
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
  EnableWindow(g_hLoginBtn, FALSE);
  
  std::wstring user(u), pass(p);
  SecureZeroMemory(p, sizeof(p));
  
  std::thread([hwnd, user, pass]() {
    long st = Praktika_RpcLogin(user.c_str(), pass.c_str());
    // Clear password copy
    auto passCopy = pass;
    SecureZeroMemory(&passCopy[0], passCopy.size() * sizeof(wchar_t));
    
    if (st != CYCL_OK) {
      PostMessageW(hwnd, WM_APP_STATE_UPDATED, 1, 0); // 1 = login failed
    } else {
      // Trigger a full state check
      g_pollInProgress = false;
      CheckStateAsync(hwnd);
    }
  }).detach();
}

void OnActivateClick(HWND hwnd) {
  wchar_t c[256]{};
  GetWindowTextW(g_hCodeEdit, c, 256);
  
  SetWindowTextW(g_hActErr, L"Подождите...");
  UpdateWindow(g_hActErr);
  EnableWindow(g_hActBtn, FALSE);
  
  std::wstring code(c);
  
  std::thread([hwnd, code]() {
    long st = Praktika_RpcActivateProduct(code.c_str());
    if (st != CYCL_OK) {
      PostMessageW(hwnd, WM_APP_STATE_UPDATED, 2, 0); // 2 = activation failed
    } else {
      g_pollInProgress = false;
      CheckStateAsync(hwnd);
    }
  }).detach();
}

void OnLogoutClick(HWND hwnd) {
  EnableWindow(g_hLogoutBtn, FALSE);
  std::thread([hwnd]() {
    Praktika_RpcLogout();
    g_pollInProgress = false;
    CheckStateAsync(hwnd);
  }).detach();
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
    
    // Create UI Controls directly on main window
    HINSTANCE hInst = (HINSTANCE)GetWindowLongPtrW(hwnd, GWLP_HINSTANCE);
    
    // --- Auth Controls ---
    g_hAuthTitle = CreateWindowExW(0, L"STATIC", L"◉  Вход в систему", WS_CHILD | WS_VISIBLE, 30, 25, 350, 28, hwnd, nullptr, hInst, nullptr);
    g_hUserLbl = CreateWindowExW(0, L"STATIC", L"Логин", WS_CHILD | WS_VISIBLE, 30, 75, 100, 20, hwnd, nullptr, hInst, nullptr);
    g_hUserEdit = CreateWindowExW(0, L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_BORDER | ES_AUTOHSCROLL, 30, 98, 340, 28, hwnd, (HMENU)ID_USER_EDIT, hInst, nullptr);
    g_hPassLbl = CreateWindowExW(0, L"STATIC", L"Пароль", WS_CHILD | WS_VISIBLE, 30, 140, 100, 20, hwnd, nullptr, hInst, nullptr);
    g_hPassEdit = CreateWindowExW(0, L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_BORDER | ES_AUTOHSCROLL | ES_PASSWORD, 30, 163, 340, 28, hwnd, (HMENU)ID_PASS_EDIT, hInst, nullptr);
    g_hLoginBtn = CreateWindowExW(0, L"BUTTON", L"Войти", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW, 30, 210, 340, 38, hwnd, (HMENU)ID_LOGIN_BTN, hInst, nullptr);
    g_hAuthErr = CreateWindowExW(0, L"STATIC", L"", WS_CHILD | WS_VISIBLE, 30, 260, 340, 22, hwnd, nullptr, hInst, nullptr);
    
    // --- Activation Controls ---
    g_hActTitle = CreateWindowExW(0, L"STATIC", L"◉  Активация продукта", WS_CHILD | WS_VISIBLE, 30, 25, 350, 28, hwnd, nullptr, hInst, nullptr);
    g_hCodeLbl = CreateWindowExW(0, L"STATIC", L"Код лицензии", WS_CHILD | WS_VISIBLE, 30, 75, 200, 20, hwnd, nullptr, hInst, nullptr);
    g_hCodeEdit = CreateWindowExW(0, L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_BORDER | ES_AUTOHSCROLL, 30, 98, 340, 28, hwnd, (HMENU)ID_CODE_EDIT, hInst, nullptr);
    g_hActBtn = CreateWindowExW(0, L"BUTTON", L"Активировать", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW, 30, 145, 340, 38, hwnd, (HMENU)ID_ACTIVATE_BTN, hInst, nullptr);
    g_hActErr = CreateWindowExW(0, L"STATIC", L"", WS_CHILD | WS_VISIBLE, 30, 195, 340, 22, hwnd, nullptr, hInst, nullptr);
    
    // --- Main Controls ---
    g_hMainTitle = CreateWindowExW(0, L"STATIC", L"◉  Praktika Antivirus", WS_CHILD | WS_VISIBLE, 30, 25, 350, 28, hwnd, nullptr, hInst, nullptr);
    g_hStatusLbl = CreateWindowExW(0, L"STATIC", L"", WS_CHILD | WS_VISIBLE, 30, 80, 340, 24, hwnd, nullptr, hInst, nullptr);
    g_hMainUserLbl = CreateWindowExW(0, L"STATIC", L"", WS_CHILD | WS_VISIBLE, 30, 120, 340, 22, hwnd, nullptr, hInst, nullptr);
    g_hLicLbl = CreateWindowExW(0, L"STATIC", L"", WS_CHILD | WS_VISIBLE, 30, 155, 340, 22, hwnd, nullptr, hInst, nullptr);
    g_hLogoutBtn = CreateWindowExW(0, L"BUTTON", L"Выйти из аккаунта", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW, 30, 200, 340, 38, hwnd, (HMENU)ID_LOGOUT_BTN, hInst, nullptr);
    
    // Apply fonts: titles get big font, rest get UI font
    SendMessageW(g_hAuthTitle, WM_SETFONT, (WPARAM)g_hFontTitle, TRUE);
    SendMessageW(g_hActTitle, WM_SETFONT, (WPARAM)g_hFontTitle, TRUE);
    SendMessageW(g_hMainTitle, WM_SETFONT, (WPARAM)g_hFontTitle, TRUE);
    HWND allCtrls[] = { g_hUserLbl, g_hUserEdit, g_hPassLbl, g_hPassEdit,
      g_hLoginBtn, g_hAuthErr, g_hCodeLbl, g_hCodeEdit, g_hActBtn, g_hActErr,
      g_hStatusLbl, g_hMainUserLbl, g_hLicLbl, g_hLogoutBtn };
    for (HWND h : allCtrls)
      SendMessageW(h, WM_SETFONT, (WPARAM)g_hFontUI, TRUE);
    
    SetTimer(hwnd, ID_TIMER_POLL, 5000, nullptr);
    CheckStateAsync(hwnd);
    return 0;
  }
  case WM_TIMER:
    if (wParam == ID_TIMER_POLL) {
      CheckStateAsync(hwnd);
    }
    return 0;
  case WM_APP_STATE_UPDATED:
    if (wParam == 1) {
      // Login failed
      EnableWindow(g_hLoginBtn, TRUE);
      SetWindowTextW(g_hAuthErr, L"Ошибка аутентификации.");
    } else if (wParam == 2) {
      // Activation failed
      EnableWindow(g_hActBtn, TRUE);
      SetWindowTextW(g_hActErr, L"Ошибка активации. Проверьте код.");
    } else {
      // Normal state update
      ApplyAsyncState(hwnd);
      EnableWindow(g_hLoginBtn, TRUE);
      EnableWindow(g_hActBtn, TRUE);
      EnableWindow(g_hLogoutBtn, TRUE);
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
  case WM_CTLCOLORSTATIC: {
    HDC hdc = (HDC)wParam;
    HWND ctrl = (HWND)lParam;
    SetBkMode(hdc, TRANSPARENT);
    // Title labels
    if (ctrl == g_hAuthTitle || ctrl == g_hActTitle || ctrl == g_hMainTitle) {
      SetTextColor(hdc, CLR_TEXT);
    }
    // Error labels (red/yellow)
    else if (ctrl == g_hAuthErr || ctrl == g_hActErr) {
      SetTextColor(hdc, CLR_RED);
    }
    // Status label — green if licensed, red otherwise
    else if (ctrl == g_hStatusLbl) {
      SetTextColor(hdc, g_isLicensed ? CLR_GREEN : CLR_RED);
    }
    // Dim labels
    else if (ctrl == g_hUserLbl || ctrl == g_hPassLbl || ctrl == g_hCodeLbl) {
      SetTextColor(hdc, CLR_TEXT_DIM);
    }
    // Normal text
    else {
      SetTextColor(hdc, CLR_TEXT);
    }
    return (LRESULT)g_hBrBg;
  }
  case WM_CTLCOLOREDIT: {
    HDC hdc = (HDC)wParam;
    SetTextColor(hdc, CLR_EDIT_TEXT);
    SetBkColor(hdc, CLR_EDIT_BG);
    return (LRESULT)g_hBrEdit;
  }
  case WM_DRAWITEM: {
    auto* di = (DRAWITEMSTRUCT*)lParam;
    if (di->CtlType != ODT_BUTTON) break;
    COLORREF bg = CLR_ACCENT;
    if (di->CtlID == ID_LOGOUT_BTN) bg = RGB(60, 60, 80);
    if (di->itemState & ODS_SELECTED) {
      bg = RGB(GetRValue(bg)*3/4, GetGValue(bg)*3/4, GetBValue(bg)*3/4);
    }
    if (di->itemState & ODS_DISABLED) bg = RGB(60,60,75);
    // Rounded rect
    HBRUSH hbr = CreateSolidBrush(bg);
    HPEN pen = CreatePen(PS_SOLID, 1, bg);
    SelectObject(di->hDC, hbr);
    SelectObject(di->hDC, pen);
    RoundRect(di->hDC, di->rcItem.left, di->rcItem.top,
              di->rcItem.right, di->rcItem.bottom, 8, 8);
    // Text
    SetBkMode(di->hDC, TRANSPARENT);
    SetTextColor(di->hDC, (di->itemState & ODS_DISABLED) ? CLR_TEXT_DIM : RGB(255,255,255));
    SelectObject(di->hDC, g_hFontUI);
    wchar_t txt[128]{};
    GetWindowTextW(di->hwndItem, txt, 128);
    DrawTextW(di->hDC, txt, -1, &di->rcItem, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    DeleteObject(hbr);
    DeleteObject(pen);
    // Focus rect
    if (di->itemState & ODS_FOCUS) {
      RECT r = di->rcItem;
      InflateRect(&r, -3, -3);
      DrawFocusRect(di->hDC, &r);
    }
    return TRUE;
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
  // Create dark theme resources.
  g_hBrBg   = CreateSolidBrush(CLR_BG);
  g_hBrCard = CreateSolidBrush(CLR_CARD);
  g_hBrEdit = CreateSolidBrush(CLR_EDIT_BG);
  g_hFontUI    = CreateFontW(-16, 0,0,0, FW_NORMAL,0,0,0, DEFAULT_CHARSET,0,0, CLEARTYPE_QUALITY,0, L"Segoe UI");
  g_hFontTitle = CreateFontW(-22, 0,0,0, FW_SEMIBOLD,0,0,0, DEFAULT_CHARSET,0,0, CLEARTYPE_QUALITY,0, L"Segoe UI");
  g_hFontSmall = CreateFontW(-13, 0,0,0, FW_NORMAL,0,0,0, DEFAULT_CHARSET,0,0, CLEARTYPE_QUALITY,0, L"Segoe UI");

  wc.cbSize = sizeof(wc);
  wc.style = CS_HREDRAW | CS_VREDRAW;
  wc.lpfnWndProc = WndProc;
  wc.hInstance = hInstance;
  wc.hIcon = static_cast<HICON>(LoadImageW(
      nullptr, reinterpret_cast<LPCWSTR>(IDI_APPLICATION), IMAGE_ICON, 0, 0,
      LR_SHARED));
  wc.hCursor = LoadCursorW(nullptr, reinterpret_cast<LPCWSTR>(IDC_ARROW));
  wc.hbrBackground = g_hBrBg;
  wc.lpszClassName = kWindowClass;
  if (!RegisterClassExW(&wc)) {
    if (g_singletonMutex) {
      CloseHandle(g_singletonMutex);
    }
    return 1;
  }

  HWND main = CreateWindowExW(0, kWindowClass, kWindowTitle,
                              WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
                              420, 360, nullptr, nullptr, hInstance, nullptr);
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
