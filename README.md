# Praktika

Учебный проект на **Windows**, **C/C++**: трей-приложение (задание 1) + Windows-служба и **Windows RPC (ALPC)** (задание 2). Сборка — **CMake + MSVC (x64)**, разработка в **VS Code** (CMake Tools).

---

## Сборка

В VS Code:

1. **CMake Tools**, **C/C++** — расширения.
2. **Ctrl+Shift+P → CMake: Select Configure Preset → Visual Studio 2022 x64**.
3. **CMake: Configure**, затем **CMake: Build** (или **F7**).

В консоли:

```text
cmake -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
```

После сборки в `build\bin\Release\` лежат **`Praktika.exe`** и **`PraktikaService.exe`** (оба — в одной папке: служба ищет GUI рядом).

MIDL-стабы (`rpc/Praktika_Stop_*.{h,c}`) **закоммичены**; перегенерировать после правок IDL — **`scripts\gen_midl.bat`** (нужны Visual Studio Build Tools 2022).

---

## Установка службы (от администратора)

```text
cd build\bin\Release
PraktikaService.exe install
sc.exe start PraktikaSvc
```

После старта служба сама запускает `Praktika.exe` во **всех активных** пользовательских терминальных сессиях (≠ 0), от **имени владельца** сессии, **главное окно скрыто**. Иконка появится в трее.

Удаление:

```text
sc.exe stop PraktikaSvc        # не сработает по дизайну: STOP отключён
PraktikaService.exe uninstall  # сначала остановите процесс через GUI «Выход»
```

> Служба **не принимает** `STOP` и `SHUTDOWN` (требование п.3). Остановка — только через **Выход** в GUI (RPC-метод `Praktika_Stop::RequestStop`).

---

## Архитектура

### Межпроцессное взаимодействие

- Транспорт **ALPC** через **Windows RPC**, протокол **`ncalrpc`**, эндпоинт **`PraktikaSvcStop`**.
- IDL: `rpc/Praktika_Stop.idl` — один метод `RequestStop()`.
- Клиент GUI: `src/service_client.cpp` (`Praktika_CallRequestStop`).
- Сервер службы: `src/service/PraktikaService.cpp` (поток `RpcThread`, `RpcServerListen`).

### Служба `PraktikaService.exe`

| Требование (задание 2) | Реализация |
|------------------------|------------|
| 1. Запуск GUI во всех сессиях ≠ 0, от владельца, окно скрыто | `WTSEnumerateSessionsW` (только `WTSActive`) → `WTSQueryUserToken` + `DuplicateTokenEx` → `CreateProcessAsUserW`, аргументы `/background --silent /fromservice:<pid>` |
| 2. Слежение за новыми пользователями | `SERVICE_ACCEPT_SESSIONCHANGE` + `WTS_SESSION_LOGON` → `LaunchInSession` |
| 3. STOP/SHUTDOWN отключены | В `HandlerEx`: возвращаем `ERROR_CALL_NOT_IMPLEMENTED`; `dwControlsAccepted` = только `SERVICE_ACCEPT_SESSIONCHANGE` |
| 4. RPC-сервер ALPC, работа до остановки RPC | Отдельный поток с `RpcServerListen`; `ServiceMain` ждёт его `WaitForSingleObject` |
| 5. Зарегистрированный интерфейс остановки | `Praktika_Stop_v1_0_s_ifspec` через `RpcServerRegisterIf2` |
| 6. При остановке завершить все GUI | `KillAllChildren` хранит `HANDLE` дочерних процессов и вызывает `TerminateProcess` |

`install`/`uninstall` (как у [lqt5h/ziovpontvrs](https://github.com/lqt5h/ziovpontvrs)): `CreateServiceW` (тип `SERVICE_AUTO_START`) и `DeleteService`.

### Графическое приложение `Praktika.exe`

| Требование (задание 2) | Реализация |
|------------------------|------------|
| 1. Если служба остановлена — запустить, дождаться Running, выйти | `Praktika_BootstrapServiceAndExit` (`StartServiceW` + цикл `QueryServiceStatus`) |
| 2. Родителем должна быть служба, иначе выход | `OpenSCManager` + `QueryServiceStatusEx` → PID службы; сравнение с **`GetParentProcessId`** **или** с `/fromservice:<pid>` (Win10+ часто не выставляет parent) |
| 3. «Файл → Выход» останавливает службу | `IDM_FILE_EXIT` → `Praktika_CallRequestStop` |
| 4. «Выход» в трее останавливает службу | `IDM_TRAY_EXIT` → `Praktika_CallRequestStop` |

Кроме того, GUI решает **задание 1** (12 пунктов: трей, ЛКМ/ПКМ, «Открыть»/«Выход», `TaskbarCreated`, скрытый старт `/background`, фон при закрытии, `Файл → Выход`, single-instance мьютекс).

### Необязательные баллы (защита и подтверждение)

| Пункт курса | Реализация |
|-------------|------------|
| Подтверждение остановки в интерактивной сессии | После RPC `RequestStop` служба блокирует второй вызов (`g_stopping`), вызывает **`WTSSendMessageW`** в первой сессии **`WTSActive`** (≠ 0). Кнопка по умолчанию — **«Нет»**. При «Нет» флаг сбрасывается, служба не останавливается. Это диалог **рабочего стола пользователя**, а не отдельный **Secure Desktop Winlogon** (как при UAC — для него нужна иная схема). |
| DACL процесса службы | после `SERVICE_RUNNING`: **`SetSecurityInfo`** на **`GetCurrentProcess`** — **DENY** `PROCESS_TERMINATE` для **Everyone**, доп. **GRANT** ограниченного чтения Everyone, **GRANT** всего у **SYSTEM** и **Administrators**. Обычный пользователь из диспетчера задач не завершает `PraktikaService.exe`. Повышенный администратор всё ещё может. |
| DACL процессов GUI | для каждого дочернего `Praktika.exe` — **явный GRANT**: **владелец** (SID из primary-токена сессии), **SYSTEM**, **Administrators**; у остальных SIDs прав на `OpenProcess`/`TerminateProcess` нет. Так владелец сессии может завершить свой трей вручную, а **DENY для Everyone** не используется (он ломал бы и владельца). |

---

## Запуск (вручную после `install`)

```text
sc.exe start PraktikaSvc        # служба сама стартует Praktika.exe в трее
```

GUI **руками не запускайте** — он сам поднимется службой и пройдёт проверку родителя. Если запустить GUI вручную при остановленной службе — он стартует службу и завершится.

---

## CI

`.github/workflows/build.yml` — сборка обоих exe на `windows-latest`, артефакт **`Praktika-Release-x64`** содержит `Praktika.exe` и `PraktikaService.exe`.
