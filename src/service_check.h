// Проверки службы со стороны GUI (задание 2 п.1, п.2).
#pragma once

#include <windows.h>

// PID процесса службы Praktika в SCM или 0, если служба не зарегистрирована.
DWORD Praktika_GetServiceProcessId();

// Состояние службы: запустить, дождаться Running. true — этому процессу
// нужно тут же завершиться (служба запустит GUI повторно — уже как ребёнок).
bool Praktika_BootstrapServiceAndExit();

// Парсинг /fromservice:<pid> из аргументов (Win10+ часто не выставляет
// родителем процесс службы; служба передаёт свой PID явно).
DWORD Praktika_ParseFromServiceArg();

// PID родителя текущего процесса.
DWORD Praktika_GetParentProcessId();
