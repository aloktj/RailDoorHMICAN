@echo off
setlocal

REM Start three DoorNode instances on the same channel.
start "DoorNode-1" "apps\DoorNode\x64\Debug\DoorNode.exe" --id 1 --channel PCAN_USBBUS1
start "DoorNode-2" "apps\DoorNode\x64\Debug\DoorNode.exe" --id 2 --channel PCAN_USBBUS1
start "DoorNode-3" "apps\DoorNode\x64\Debug\DoorNode.exe" --id 3 --channel PCAN_USBBUS1

endlocal
