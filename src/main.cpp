// Praktika — задание 1: Win32 трей с одним экземпляром на пользователя.
// Выполнены 12 пунктов требования (см. README).
#include <windows.h>
#include <shellapi.h>
#include <string>

#pragma comment(lib, "user32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "advapi32.lib")

namespace {

constexpr wchar_t kWindowClass[] = L"Praktika_TrayWindow";
constexpr wchar_t kWindowTitle[] = L"Praktika";
// П.10: один процесс на пользователя — Local\\ ограничивает мьютекс сессией.
constexpr wchar_t kSingletonMutex[] = L"Local\\Praktika_SingleInstance";

constexpr UINT kTrayCallbackMsg = WM_APP + 1;
constexpr UINT kTrayIconId = 1;

// П.7: поддержка скрытого старта; принимаем «/background», «-background», «--silent».
constexpr wchar_t kArgBackground1[] = L"/background";
constexpr wchar_t kArgBackground2[] = L"-background";
constexpr wchar_t kArgSilent1[] = L"--silent";
constexpr wchar_t kArgSilent2[] = L"-silent";

enum CommandIds : UINT_PTR {
  IDM_FILE_EXIT = 100,
  IDM_TRAY_OPEN = 101,
  IDM_TRAY_EXIT = 102,
};

HANDLE g_singletonMutex = nullptr;
// П.6: системное сообщение TaskbarCreated, динамический id.
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

// П.10: true — этот процесс должен немедленно завершиться, не показывая трей.
bool AcquireSingletonOrExit() {
  g_singletonMutex = CreateMutexW(nullptr, FALSE, kSingletonMutex);
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

// П.2 / П.4: показать и активировать главное окно.
void ShowAndActivateMainWindow(HWND hwnd) {
  if (!IsWindowVisible(hwnd)) {
    ShowWindow(hwnd, SW_SHOW);
  }
  ShowWindow(hwnd, SW_RESTORE);
  SetForegroundWindow(hwnd);
}

// П.3 / П.4 / П.5: правый клик — контекстное меню «Открыть» / «Выход».
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
  // Нужно «фокусом», иначе меню не закроется по клику вне окна.
  SetForegroundWindow(hwnd);
  TrackPopupMenu(menu, TPM_RIGHTBUTTON | TPM_BOTTOMALIGN | TPM_LEFTALIGN, pt.x,
                 pt.y, 0, hwnd, nullptr);
  PostMessageW(hwnd, WM_NULL, 0, 0);
  DestroyMenu(menu);
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
  // П.6: после рестарта Explorer окно получит TaskbarCreated — повторно добавить иконку.
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
    // П.9: главное меню окна с пунктом «Файл → Выход».
    HMENU bar = CreateMenu();
    HMENU file = CreateMenu();
    AppendMenuW(file, MF_STRING, IDM_FILE_EXIT, L"Выход");
    AppendMenuW(bar, MF_POPUP, reinterpret_cast<UINT_PTR>(file), L"Файл");
    SetMenu(hwnd, bar);
    return 0;
  }
  case WM_COMMAND: {
    const UINT id = LOWORD(wParam);
    if (id == IDM_TRAY_OPEN) {
      ShowAndActivateMainWindow(hwnd);
    } else if (id == IDM_TRAY_EXIT || id == IDM_FILE_EXIT) {
      // П.5 / П.9: оба «Выход» завершают приложение.
      DestroyWindow(hwnd);
    }
    return 0;
  }
  case WM_CLOSE:
    // П.8: крестик скрывает окно, процесс продолжает работу из трея.
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
  // П.10: проверка до создания окна и иконки трея.
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

  // П.7: запуск с /background — окно скрыто, иконка в трее уже есть.
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
