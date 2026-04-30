# Praktika — задание 1 (Win32 трей)

Графическое приложение на **Win32 API** + **CMake** + **MSVC**. Иконка в трее, контекстное меню, скрытый запуск, единственный экземпляр на пользователя. Сборка через VS Code (CMake Tools) или из консоли.

## Соответствие требованиям ТЗ

| № | Требование | Где реализовано |
|---|------------|------------------|
| 1 | Иконка в трее при запуске | `WM_CREATE` → `AddTrayIcon` (`Shell_NotifyIcon NIM_ADD`) |
| 2 | ЛКМ — главное окно | `kTrayCallbackMsg` + `WM_LBUTTONUP` → `ShowAndActivateMainWindow` |
| 3 | ПКМ — контекстное меню | `WM_RBUTTONUP` → `ShowTrayContextMenu` |
| 4 | «Открыть» в меню | `IDM_TRAY_OPEN` → `ShowAndActivateMainWindow` |
| 5 | «Выход» в меню | `IDM_TRAY_EXIT` → `DestroyWindow` → `WM_DESTROY` |
| 6 | Пересоздание панели задач | `RegisterWindowMessage("TaskbarCreated")` + повторный `NIM_ADD` |
| 7 | Скрытый запуск | `/background` / `--silent` → `SW_HIDE` главного окна |
| 8 | Закрытие окна — в фон | `WM_CLOSE` → `ShowWindow(SW_HIDE)`, не выход |
| 9 | Меню «Файл → Выход» | `CreateMenu` + `IDM_FILE_EXIT` |
| 10 | Один экземпляр на пользователя | `CreateMutexW("Local\\Praktika_SingleInstance")` |
| 11 | Сборка на конвейере | `CMakeLists.txt`, `CMakePresets.json`, `.github/workflows/build.yml` |
| 12 | Артефакт-exe | `build/bin/Release/Praktika.exe`, `MultiThreaded` (CRT в exe), артефакт CI |

## Сборка в VS Code

1. Установить **Visual Studio 2022 Build Tools** (или VS 2022) с рабочей нагрузкой **C++ desktop** и **Windows SDK**.
2. Открыть папку проекта в VS Code, согласиться на рекомендуемые расширения **CMake Tools**, **C/C++**.
3. **Ctrl+Shift+P** → `CMake: Select a Kit` → **Visual Studio 2022 Release - amd64**.
4. **CMake: Configure**, затем **CMake: Build** (или задача `CMake: build Release`).

Артефакт: `build/bin/Release/Praktika.exe`.

## Сборка из консоли

```powershell
cmake -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
```

## Запуск

- Обычно: `build\bin\Release\Praktika.exe` — окно + иконка.
- Скрытый: `Praktika.exe /background` — окно скрыто, только трей.
- ЛКМ — окно. ПКМ — меню «Открыть» / «Выход». Крестик — в трей.

## CI

Workflow `.github/workflows/build.yml` собирает Release на `windows-latest` и публикует артефакт **`Praktika-Release-x64`** (`Praktika.exe`).
