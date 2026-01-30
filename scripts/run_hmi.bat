@echo off
setlocal

set "HMI_EXE=apps\HmiApp\x64\Debug\HmiApp.exe"
if not exist "%HMI_EXE%" (
  echo HmiApp executable not found: %HMI_EXE%
  echo Build the solution first.
  pause
  exit /b 1
)

"%HMI_EXE%" --channel PCAN_USBBUS1
if errorlevel 1 (
  echo HmiApp exited with error.
  pause
  exit /b 1
)

endlocal
