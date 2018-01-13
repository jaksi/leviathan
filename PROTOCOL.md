# Reverse engineered USB protocol of the NZXT Kraken X61
Basic knowledge of the USB protocol is assumed.
Example codes use the [PyUSB](http://walac.github.io/pyusb/) Python module.
## Initialization
The device has to be initialized with the following control message:

Request Type | Request | Value | Index | Length
-------------|---------|-------|-------|-------
`0x40` | `2` | `0x0002` | `0` | `0`

Python code:
```Python
dev.ctrl_transfer(0x40, 2, 0x0002)
```

## Messages
### Messages sent by the host
The first byte of messages sent by the host is the message type:

Type | Description | Length in bytes
-----|-------------|----------------
`0x10` | Color | 19
`0x12` | Fan speed | 2
`0x13` | Pump speed | 2

Notes:
* The pump speed doesn't seem to be too responsive
* The Windows software always matches the pump and fan speeds
* While the Windows software lets you set the speed to as low as 25%, it actually won't send messages containing speeds less than 30% to the device

#### Color message

Byte | Meaning
-----|--------
1-3 | Color (RGB)
4-6 | Alternate color (RGB)
7-10 | `0x0000003c`
11 | Alternating interval (sec)
12 | Blinking interval (sec)
13 | Enabled (on/off)
14 | Alternating mode (on/off)
15 | Blinking mode (on/off)
16-18 | `0x000001`

Notes:
* Byte 10 has something to do with alternating mode, values <= 0x20 stop the color change
* Changing byte 18 to anything other than 0x01 ramps up the speed to 100%
* Newer versions of CAM (checked in 3.5.50) no longer allow changing the blinking and alternating interval

#### Fan speed message

Byte | Meaning
-----|--------
1 | Fan speed percentage (30-100)

#### Pump speed message

Byte | Meaning
-----|--------
1 | Pump speed percentage (30-100)

### Status message sent by the cooler
Byte | Meaning
-----|--------
0-1 | Fan speed (RPM)
2-7 | ?
8-9 | Pump speed (RPM)
10 | Liquid temperature (°C)
11-31 | ?

## Communication
Every transaction begins with the following control message:

Request Type | Request | Value | Index | Length
-------------|---------|-------|-------|-------
`0x40` | `2` | `0x0001` | `0` | `0`

Python code:
```Python
dev.ctrl_transfer(0x40, 2, 0x0001)
```
Afterwards, one of the following sequence of bulk messages are sent to endpoint `0x02`:
* a color message
* a pump speed message followed by a fan speed message

Python code:
```Python
dev.write(2, [13, 50])
dev.write(2, [12, 50])
```

Finally, a status message of 64 bytes is read from the device (endpoint `0x82`).

Python code:
```Python
status = dev.read(0x82, 64)
fan_speed = 256 * status[0] + status[1]
pump_speed = 256 * status[8] + status[9]
liquid_temperature = status[10]
```
