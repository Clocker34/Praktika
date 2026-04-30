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
                            rpc/midl_user.c PROPERTIES LANGUAGE C)

# ---- GUI (задание 1 + клиент RPC) ----
add_executable(Praktika WIN32
  src/main.cpp
  src/service_check.cpp
  src/service_client.cpp
  rpc/Praktika_Stop_c.c
  rpc/midl_user.c
)
target_include_directories(Praktika PRIVATE ${PRAKTIKA_INCLUDES})
target_compile_options(Praktika PRIVATE /utf-8 /W4 /permissive-)
target_compile_definitions(Praktika PRIVATE UNICODE _UNICODE)
set_property(TARGET Praktika PROPERTY
  MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
target_link_libraries(Praktika PRIVATE
  user32 shell32 advapi32 gdi32 Shlwapi Rpcrt4)

# ---- Служба (задание 2: SCM + WTS + RPC сервер) ----
add_executable(PraktikaService
  src/service/PraktikaService.cpp
  rpc/Praktika_Stop_s.c
  rpc/midl_user.c
)
target_include_directories(PraktikaService PRIVATE ${PRAKTIKA_INCLUDES})
target_compile_options(PraktikaService PRIVATE /utf-8 /W4 /permissive-)
target_compile_definitions(PraktikaService PRIVATE UNICODE _UNICODE)
set_property(TARGET PraktikaService PROPERTY
  MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
target_link_libraries(PraktikaService PRIVATE
  advapi32 user32 Wtsapi32 Userenv Shlwapi shell32 Rpcrt4)

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
