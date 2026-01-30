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
