# CAN ICD (Phase-1)

## Bus
- Bitrate: **500 kbit/s** (default)
- Classic CAN (11-bit standard identifiers)
- DLC: **8**

## Status Frames (cyclic)
- Door 1 status: **0x101**
- Door 2 status: **0x102**
- Door 3 status: **0x103**
- Period: **100 ms** default (configurable)

### Payload (DLC = 8)
| Byte | Name         | Description |
|------|--------------|-------------|
| B0   | state        | 0=CLOSED, 1=OPEN, 2=MOVING, 3=FAULTED |
| B1   | obstruction  | 0/1 |
| B2   | fault_code   | 0..255 |
| B3   | door_id      | 1..3 |
| B4   | reserved     | 0 |
| B5   | reserved     | 0 |
| B6   | reserved     | 0 |
| B7   | reserved     | 0 |

### Example (Door 2 = OPEN, no obstruction/fault)
- ID: `0x102`
- DATA: `01 00 00 02 00 00 00 00`

### Hex Dump Example
```
0x102  [8] 01 00 00 02 00 00 00 00
```

## Command Frames (event)
- Command ID: **0x201**

### Payload (DLC = 8)
| Byte | Name     | Description |
|------|----------|-------------|
| B0   | door_id  | 1..3 |
| B1   | cmd      | 1=OPEN, 2=CLOSE, 3=RESET_FAULT |
| B2   | reserved | 0 |
| B3   | reserved | 0 |
| B4   | reserved | 0 |
| B5   | reserved | 0 |
| B6   | reserved | 0 |
| B7   | reserved | 0 |

### Example (Open Door 1)
- ID: `0x201`
- DATA: `01 01 00 00 00 00 00 00`

### Example (Reset Door 3 Fault)
- ID: `0x201`
- DATA: `03 03 00 00 00 00 00 00`

### Hex Dump Example
```
0x201  [8] 03 03 00 00 00 00 00 00
```
