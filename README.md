# Praktika

Учебный проект на **Windows**, **C/C++**: трей-приложение (**задание 1**) + Windows-служба и **Windows RPC (ALPC)** (**задание 2**). Сборка — **CMake + MSVC (x64)**; разработка в **VS Code** с расширением CMake Tools.

---

## Ветки Git

| Ветка | Содержание |
|-------|------------|
| `main` | Задание 1: Win32-трей, CMake, VS Code, CI (`Praktika.exe`). |
| `assignment-2` | Задание 2: служба `PraktikaService.exe`, RPC/alpc, WTS, bootstrap GUI; необязательные баллы (диалог остановки, DACL). |

После клонирования для проверки второго задания переключитесь: `git checkout assignment-2`.

---

## Сборка

### VS Code и CMake Tools

1. Расширения **CMake Tools** и **C/C++**.
2. **Ctrl+Shift+P → CMake: Select Configure Preset → Visual Studio 2022 x64**.
3. **CMake: Configure**, затем **CMake: Build** (или **F7**).

### Консоль

Из корня репозитория:

```text
cmake -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
```

После сборки каталог с exe: **`build\bin\Release\`** — там должны лежать **`Praktika.exe`** и **`PraktikaService.exe`** в **одной папке** (служба подставляет путь к GUI относительно себя).

### Если команда `cmake` «не найдена» в терминале

Часто так бывает до добавления CMake в **PATH** пользователя или когда встроенный терминал редактора не подхватывает обновлённые переменные окружения.

1. Установите [CMake для Windows](https://cmake.org/download/) и при установке включите пункт **Add CMake to the user PATH**, **или** добавьте вручную папку с `cmake.exe`, например:  
   `...\cmake-x.y.z-windows-x86_64\bin`
2. **Полностью перезапустите** VS Code после правки PATH.
3. Если в реестре путь есть, а в терминале `cmake` всё равно не виден — в **этой сессии** PowerShell выполните:
   ```powershell
   $env:Path = [Environment]::GetEnvironmentVariable("Path", "Machine") + ";" + [Environment]::GetEnvironmentVariable("Path", "User")
   cmake --version
   ```
4. CMake входит в **Visual Studio 2022** (пример пути):  
   `C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe`  
   можно вызывать с полным путём без настройки PATH.

MIDL-стабы (`rpc/Praktika_Stop_*.{h,c}`) **закоммичены**; перегенерировать после правок IDL — **`scripts\gen_midl.bat`** (нужна среда **Visual Studio Build Tools 2022** и `midl.exe`).

---

## Установка и запуск службы

Команды **`install`** / удаление службы выполняются **от администратора** (регистрация в SCM). Запускать **`PraktikaService.exe`** нужно **из каталога сборки**, где рядом лежит **`Praktika.exe`**.

В **PowerShell** обязателен префикс **`.\`** для программы в текущей папке:

```powershell
cd "C:\путь\к\репозиторию\Praktika\build\bin\Release"
.\PraktikaService.exe install
sc.exe query PraktikaSvc   # краткая проверка; через пару секунд STATE должен быть RUNNING
sc.exe start PraktikaSvc
```

Если выполнять установку из `C:\Windows\System32`, PowerShell не найдёт exe без полного пути — сначала перейдите в `build\bin\Release`, как выше.

После старта служба сама поднимает `Praktika.exe` во **всех активных** пользовательских терминальных сессиях (≠ `0`), от **имени владельца** сессии, **главное окно скрыто**; значок появляется в трее.

### Остановка и удаление

```text
sc.exe stop PraktikaSvc        # не сработает по дизайну: STOP/SHUTDOWN от SCM отключены
.\PraktikaService.exe uninstall  # сначала остановите через GUI «Выход» (RPC)
```

> Служба **не принимает** `STOP` и `SHUTDOWN` (п. 3 ТЗ). Остановка — через **Выход** в меню приложения или трее: вызывается RPC-метод `Praktika_Stop::RequestStop`. Перед остановкой (необязательный балл) показывается диалог подтверждения в активной пользовательской сессии.

---

## Архитектура

### Межпроцессное взаимодействие

- Транспорт **ALPC** через **Windows RPC**, протокол **`ncalrpc`**, эндпоинт **`PraktikaSvcStop`**.
- IDL: `rpc/Praktika_Stop.idl` — метод `RequestStop()`.
- Клиент GUI: `src/service_client.cpp` (`Praktika_CallRequestStop`).
- Сервер службы: `src/service/PraktikaService.cpp` (поток `RpcThread`, `RpcServerListen`).

### Служба `PraktikaService.exe`

| Требование (задание 2) | Реализация |
|------------------------|------------|
| 1. Запуск GUI во всех сессиях ≠ 0, от владельца, окно скрыто | `WTSEnumerateSessionsW` (только `WTSActive`) → `WTSQueryUserToken` + `DuplicateTokenEx` → `CreateProcessAsUserW`, аргументы `/background --silent /fromservice:<pid>` |
| 2. Слежение за новыми пользователями | `SERVICE_ACCEPT_SESSIONCHANGE` + `WTS_SESSION_LOGON` → `LaunchInSession` |
| 3. STOP/SHUTDOWN отключены | В `HandlerEx`: `ERROR_CALL_NOT_IMPLEMENTED`; `dwControlsAccepted` = только `SERVICE_ACCEPT_SESSIONCHANGE` |
| 4. RPC-сервер ALPC | Отдельный поток с `RpcServerListen`; `ServiceMain` ждёт его `WaitForSingleObject` |
| 5. Интерфейс остановки | `Praktika_Stop_v1_0_s_ifspec` через `RpcServerRegisterIf2` |
| 6. При остановке завершить все GUI | `KillAllChildren`: `TerminateProcess` по сохранённым `HANDLE` дочерних процессов |

`install` / `uninstall` (идея как у [lqt5h/ziovpontvrs](https://github.com/lqt5h/ziovpontvrs)): `CreateServiceW` (`SERVICE_AUTO_START`) и `DeleteService`.

### Графическое приложение `Praktika.exe`

| Требование (задание 2) | Реализация |
|------------------------|------------|
| 1. Служба остановлена — поднять и выйти | `Praktika_BootstrapServiceAndExit` (`StartServiceW` + ожидание `Running`) |
| 2. Должна быть связь со службой | PID службы из `QueryServiceStatusEx` и сравнение с **`GetParentProcessId`** **или** аргумент `/fromservice:<pid>` |
| 3–4. «Выход» | `IDM_FILE_EXIT` / `IDM_TRAY_EXIT` → `Praktika_CallRequestStop` |

Кроме того, GUI закрывает **задание 1** (трей, ЛКМ/ПКМ, «Открыть»/«Выход», `TaskbarCreated`, скрытый старт `/background`, фон при закрытии, single-instance мьютекс и т.д.).

### Необязательные баллы (ветка `assignment-2`)

| Пункт | Реализация |
|-------|------------|
| Подтверждение остановки | После RPC `RequestStop`: флаг `g_stopping`, **`WTSSendMessageW`** в первой сессии **`WTSActive`** (≠ 0); по умолчанию **«Нет»**; при отказе флаг сбрасывается. Диалог на **рабочем столе пользователя**, не изолированный Secure Desktop Winlogon (UAC). |
| DACL процесса службы | После `SERVICE_RUNNING`: **`SetSecurityInfo`** на **`GetCurrentProcess`** — **DENY** `PROCESS_TERMINATE` для **Everyone**, ограниченный **GRANT** на чтение, **GRANT** полного доступа **SYSTEM** и **Administrators**. |
| DACL процессов GUI | Для каждого дочернего `Praktika.exe`: явные **GRANT** для SID пользователя сессии (из primary-токена), **SYSTEM**, **Administrators**; без **DENY Everyone** (иначе страдает владелец процесса). |

---

## Запуск для проверки

```text
sc.exe start PraktikaSvc
```

GUI **не запускайте вручную** для штатной работы — его поднимает служба. Ручной запуск при остановленной службе запускает bootstrap (поднять службу и выйти).

---

## CI

`.github/workflows/build.yml` — сборка обоих exe на `windows-latest`, артефакт **`Praktika-Release-x64`** содержит `Praktika.exe` и `PraktikaService.exe`.

---

## Типичные проблемы

| Симптом | Что проверить |
|---------|----------------|
| `cmake` не распознан | PATH к `...\bin` с `cmake.exe`, перезапуск VS Code; см. раздел «Если команда cmake…». |
| `PraktikaService.exe` не найден | Текущая папка не `build\bin\Release`; в PowerShell используйте `.\PraktikaService.exe`. |
| `sc start` → служба не установлена (1060) | Сначала успешный `.\PraktikaService.exe install` из папки с exe. |
| Сразу после `sc start` видно `START_PENDING` | Подождите несколько секунд и снова `sc query PraktikaSvc` — ожидается **RUNNING**. |
| `LNK1104` при сборке | Закройте запущенный `Praktika.exe` / перезапустите службу, чтобы не держал файл. |
