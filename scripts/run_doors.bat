@echo off
setlocal

REM Start three DoorNode instances on the same channel.
set "DOOR_EXE=apps\DoorNode\x64\Debug\DoorNode.exe"
if not exist "%DOOR_EXE%" (
  echo DoorNode executable not found: %DOOR_EXE%
  echo Build the solution first.
  pause
  exit /b 1
)

echo Starting Door 1...
start "DoorNode-1" "%DOOR_EXE%" --id 1 --channel PCAN_USBBUS1
if errorlevel 1 (
  echo Failed to start Door 1.
  pause
  exit /b 1
)

echo Starting Door 2...
start "DoorNode-2" "%DOOR_EXE%" --id 2 --channel PCAN_USBBUS1
if errorlevel 1 (
  echo Failed to start Door 2.
  pause
  exit /b 1
)

echo Starting Door 3...
start "DoorNode-3" "%DOOR_EXE%" --id 3 --channel PCAN_USBBUS1
if errorlevel 1 (
  echo Failed to start Door 3.
  pause
  exit /b 1
)

endlocal
