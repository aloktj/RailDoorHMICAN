# One-PC Testing with PCAN-View

## Prereqs
- Install PEAK PCAN drivers and PCAN-View.
- Ensure `PCANBasic.dll` is available (system install, no DLLs are stored in this repo).
- Build x64 Debug or Release in Visual Studio.

## (a) Validate DoorNode TX with PCAN-View
1. Start three DoorNode instances (examples below use the default channel):
   ```bat
   scripts\run_doors.bat
   ```
2. Open **PCAN-View** and connect with:
   - **Channel**: `PCAN_USBBUS1`
   - **Bitrate**: `500 kbit/s`
   - **Message type**: Standard (11-bit)
3. Confirm you see IDs **0x101**, **0x102**, **0x103** at roughly **100 ms** intervals.
4. Inspect data bytes to confirm the state encoding and door_id.

### Example Expected Frame (Door 1 idle)
- ID: `0x101`
- DATA: `00 00 00 01 00 00 00 00`

## (b) Validate HmiApp RX by sending frames from PCAN-View
1. Start HmiApp:
   ```bat
   scripts\run_hmi.bat
   ```
2. In PCAN-View, open the **Transmit** window and add a frame:
   - **ID**: `0x101`
   - **Type**: Standard
   - **DLC**: `8`
   - **Data**: `01 00 00 01 00 00 00 00` (Door 1 OPEN)
   - **Send type**: Manual (or Cyclic)
   - **Period** (if cyclic): `100 ms`
3. Send the frame repeatedly (or enable periodic transmit).
4. Confirm HmiApp updates Door 1 to **OPEN**.
5. Stop transmitting; after ~500 ms HmiApp should show **STALE**.
   - Note: 500 ms corresponds to ~5 missed updates at the default 100 ms status period.

## (c) Validate HmiApp TX (Commands)
1. Use the HmiApp menu to send **OPEN**, **CLOSE**, or **RESET_FAULT**.
2. In PCAN-View, watch for command frames:
   - ID: `0x201`
   - DATA example (Open Door 2): `02 01 00 00 00 00 00 00`

## Acceptance Checks
- DoorNode sends 0x101..0x103 at ~100 ms.
- HmiApp shows live values for all doors and STALE after >500 ms.
- HmiApp commands are visible on the bus and cause the addressed DoorNode to move.
