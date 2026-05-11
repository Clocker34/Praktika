cmake_minimum_required(VERSION 3.20)
project(Praktika LANGUAGES CXX)

# Только MSVC + Windows SDK (требование: Win32, конвейер сборки).
if(WIN32 AND NOT MSVC)
  message(FATAL_ERROR
    "Praktika: используйте MSVC + Windows SDK. В VS Code: расширение CMake Tools, "
    "Kit «Visual Studio 2022 Release - amd64» (НЕ MinGW).")
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# WIN32 — точка входа wWinMain (без консоли); /MT — без CRT-зависимостей в exe.
add_executable(Praktika WIN32 src/main.cpp)

if(MSVC)
  target_compile_options(Praktika PRIVATE /utf-8 /W4 /permissive-)
  target_compile_definitions(Praktika PRIVATE UNICODE _UNICODE)
  set_property(TARGET Praktika PROPERTY
    MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

target_link_libraries(Praktika PRIVATE user32 shell32 advapi32 gdi32)

if(CMAKE_CONFIGURATION_TYPES)
  set_target_properties(Praktika PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY_DEBUG          "${CMAKE_BINARY_DIR}/bin/Debug"
    RUNTIME_OUTPUT_DIRECTORY_RELEASE        "${CMAKE_BINARY_DIR}/bin/Release"
    RUNTIME_OUTPUT_DIRECTORY_MINSIZEREL     "${CMAKE_BINARY_DIR}/bin/MinSizeRel"
    RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO "${CMAKE_BINARY_DIR}/bin/RelWithDebInfo"
  )
else()
  set_target_properties(Praktika PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
endif()
