# RailDoorHMICAN

Phase-1: Direct CAN↔CAN POC on Windows using PEAK PCAN and PCANBasic-Wrapper.

## Repository layout
- DoorNode (C++ console app, multiple instances for doors)
- HmiApp (C++ console app initially, GUI optional later)
- PCANBasic-Wrapper is added as a submodule under `third_party/PCANBasic-Wrapper` (do not commit wrapper sources here)

## Build (Windows, Visual Studio)
1. Clone with submodules:
   ```bash
   git clone --recurse-submodules https://github.com/aloktj/RailDoorHMICAN.git
   ```
2. Open `RailDoorHMICAN.sln` in Visual Studio (VS 2022 or newer).
3. Select **x64** and **Debug**.
4. Build the solution.

## Runtime dependency
Both apps require the PEAK PCAN drivers and the `PCANBasic.dll` runtime from PEAK; do not bundle the DLL in this repo.

## Phase-1 Demo Walkthrough
### 1-PC testing using PCAN-View
1. Build the solution (x64 Debug or Release).
2. Start the DoorNode instances:
   ```bat
   scripts\run_doors.bat
   ```
3. Open **PCAN-View** and connect to `PCAN_USBBUS1` at **500 kbit/s**.
4. Confirm you see status frames **0x101..0x103** every ~100 ms.
5. Start the HMI:
   ```bat
   scripts\run_hmi.bat
   ```
6. In PCAN-View, transmit a status frame (see `docs/OnePC_Testing_With_PCANView.md`) and verify HmiApp updates the door state.
7. Use the HmiApp menu to send OPEN/CLOSE/RESET commands and confirm PCAN-View shows `0x201`.

### 2-PC testing (Doors PC + HMI PC)
1. On the Doors PC, connect PCAN-USB to the CAN bus and build/run:
   ```bat
   scripts\run_doors.bat
   ```
2. On the HMI PC, connect a second PCAN-USB to the same bus and run:
   ```bat
   scripts\run_hmi.bat
   ```
3. Verify HmiApp receives status updates and commands appear on the bus.

## Known Limitations (Phase-1)
- Direct CAN↔CAN only (no TCMS or gateway yet).
- No redundancy.
- Console UI only.
