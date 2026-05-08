cmake_minimum_required(VERSION 3.20)
project(Praktika LANGUAGES CXX C)

if(WIN32 AND NOT MSVC)
  message(FATAL_ERROR
    "Praktika: используйте MSVC + Windows SDK. В VS Code: расширение CMake Tools, "
    "Configure Preset «Visual Studio 2022 x64» (НЕ MinGW).")
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Каталоги поиска заголовков общие для GUI и сервиса.
set(PRAKTIKA_INCLUDES
  ${CMAKE_CURRENT_SOURCE_DIR}/src
  ${CMAKE_CURRENT_SOURCE_DIR}/rpc
)

# MIDL-стабы (закоммичены, генерируются скриптом scripts/gen_midl.bat).
set_source_files_properties(rpc/Praktika_Stop_c.c rpc/Praktika_Stop_s.c
                            rpc/Praktika_Api_c.c  rpc/Praktika_Api_s.c
                            rpc/midl_user.c PROPERTIES LANGUAGE C)

# ---- GUI (задание 1 + клиент RPC + задание 3: аутентификация/лицензирование) ----
add_executable(Praktika WIN32
  src/main.cpp
  src/service_check.cpp
  src/service_client.cpp
  src/api_client.cpp
  rpc/Praktika_Stop_c.c
  rpc/Praktika_Api_c.c
  rpc/midl_user.c
)
target_include_directories(Praktika PRIVATE ${PRAKTIKA_INCLUDES})
target_compile_options(Praktika PRIVATE /utf-8 /W4 /permissive-)
target_compile_definitions(Praktika PRIVATE UNICODE _UNICODE)
set_property(TARGET Praktika PROPERTY
  MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
target_link_libraries(Praktika PRIVATE
  user32 shell32 advapi32 gdi32 Shlwapi Rpcrt4)

# ---- Служба (задание 2: SCM + WTS + RPC сервер + задание 3: auth/license) ----
add_executable(PraktikaService
  src/service/PraktikaService.cpp
  src/service/auth_manager.cpp
  src/service/license_manager.cpp
  src/service/http_client.cpp
  src/service/json_parser.cpp
  rpc/Praktika_Stop_s.c
  rpc/Praktika_Api_s.c
  rpc/midl_user.c
)
target_include_directories(PraktikaService PRIVATE ${PRAKTIKA_INCLUDES})
target_compile_options(PraktikaService PRIVATE /utf-8 /W4 /permissive-)
target_compile_definitions(PraktikaService PRIVATE UNICODE _UNICODE)
set_property(TARGET PraktikaService PROPERTY
  MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
target_link_libraries(PraktikaService PRIVATE
  advapi32 user32 Wtsapi32 Userenv Shlwapi shell32 Rpcrt4 Winhttp)

# Размещаем оба exe в одной папке (служба ищет Praktika.exe рядом).
if(CMAKE_CONFIGURATION_TYPES)
  foreach(t Praktika PraktikaService)
    set_target_properties(${t} PROPERTIES
      RUNTIME_OUTPUT_DIRECTORY_DEBUG          "${CMAKE_BINARY_DIR}/bin/Debug"
      RUNTIME_OUTPUT_DIRECTORY_RELEASE        "${CMAKE_BINARY_DIR}/bin/Release"
      RUNTIME_OUTPUT_DIRECTORY_MINSIZEREL     "${CMAKE_BINARY_DIR}/bin/MinSizeRel"
      RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO "${CMAKE_BINARY_DIR}/bin/RelWithDebInfo"
    )
  endforeach()
else()
  foreach(t Praktika PraktikaService)
    set_target_properties(${t} PROPERTIES
      RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
  endforeach()
endif()

# ---- Задание 4: Инсталлятор (CPack + NSIS) ----
# 1) Инсталлятор должен включать все исполняемые файлы
install(TARGETS Praktika PraktikaService DESTINATION bin)

# 2) Установка сторонних зависимостей (MSVC рантайм)
# Даже при статической линковке (/MT) хорошей практикой является использование
# встроенного механизма CMake для упаковки рантайма.
set(CMAKE_INSTALL_UCRT_LIBRARIES TRUE)
include(InstallRequiredSystemLibraries)

# Настройка CPack для создания NSIS-инсталлятора
set(CPACK_GENERATOR "NSIS")
set(CPACK_PACKAGE_NAME "Praktika")
set(CPACK_PACKAGE_VENDOR "Clocker34")
set(CPACK_PACKAGE_VERSION "1.0.0")
set(CPACK_PACKAGE_INSTALL_DIRECTORY "Praktika")

# 3) Автоматический запуск службы после установки (регистрация)
# Служба сама прописывает SERVICE_AUTO_START в функции DoInstall.
set(CPACK_NSIS_EXTRA_INSTALL_COMMANDS "
  ExecWait '\\\"$INSTDIR\\\\bin\\\\PraktikaService.exe\\\" install'
  ExecWait 'sc.exe start PraktikaSvc'
")

# 4, 5, 6) При удалении: остановить службу, удалить регистрацию, удалить файлы
# NSIS автоматически удаляет установленные файлы (требования 4, 5).
# Ниже мы добавляем скрипт для остановки и удаления службы (требование 6).
set(CPACK_NSIS_EXTRA_UNINSTALL_COMMANDS "
  ExecWait 'sc.exe stop PraktikaSvc'
  Sleep 2000
  ExecWait '\\\"$INSTDIR\\\\bin\\\\PraktikaService.exe\\\" uninstall'
")

include(CPack)
