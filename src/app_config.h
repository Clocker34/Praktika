// Общие имена для службы и RPC. Совпадают с rpc/Praktika_Stop.idl.
#pragma once

#define PRAKTIKA_SERVICE_NAME    L"PraktikaSvc"
#define PRAKTIKA_SERVICE_DISPLAY L"Praktika Tray Service"
#define PRAKTIKA_SERVICE_DESC \
  L"Praktika: Windows-служба + RPC (ALPC) + Win32 трей."

// Имя GUI-исполняемого, ищется рядом со службой.
#define PRAKTIKA_GUI_EXE L"Praktika.exe"

// П.10 задания 1: один процесс GUI на пользователя.
#define PRAKTIKA_SINGLETON_MUTEX L"Local\\Praktika_SingleInstance"
