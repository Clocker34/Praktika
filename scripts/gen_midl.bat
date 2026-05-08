@echo off
:: Генерация MIDL-стабов из rpc\Praktika_Stop.idl. Запускать из любого окна:
:: stubs кладутся в rpc\ и должны быть закоммичены в репозиторий.
setlocal
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 (
  echo VS 2022 vcvars64.bat not found.
  exit /b 1
)
pushd "%~dp0..\rpc"
midl.exe /env x64 /target NT100 ^
  /h Praktika_Stop.h ^
  /sstub Praktika_Stop_s.c ^
  /cstub Praktika_Stop_c.c ^
  /Oicf Praktika_Stop.idl
set rc=%errorlevel%
popd
endlocal & exit /b %rc%
