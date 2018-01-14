# kraken
Linux device driver that supports controlling and monitoring NZXT Kraken water coolers

NZXT is **NOT** involved in this project, do **NOT** contact them if your device is damaged while using this software.

Also, while it doesn't seem like the hardware could be damaged by silly USB messages (apart from overheating), I do **NOT** take any responsibility for any damage done to your cooler.

# Supported devices
* NZXT Kraken X61 (Vendor/Product ID: `2433:b200`)
* NZXT Kraken X41 (Vendor/Product ID: `2433:b200`)
* NZXT Kraken X31 (Vendor/Product ID: `2433:b200`) (Only for controlling the fan/pump speed, since there's no controllable LED on the device)

If you have an unsupported liquid cooler and want to help out, see [CONTRIBUTING.md](CONTRIBUTING.md).

# Installation
Make sure the headers for the kernel you are running are installed.
```Shell
make
sudo insmod kraken.ko
```

# Usage
The driver can be controlled with device files under `/sys/bus/usb/drivers/kraken`.

Find the symbolic links that point to the connected compatible devices.
In my case, there's only one Kraken connected.
```Shell
/sys/bus/usb/drivers/kraken/2-1:1.0 -> ../../../../devices/pci0000:00/0000:00:06.0/usb2/2-1/2-1:1.0
```

## Changing the speed
The speed must be between 30 and 100.
```Shell
echo SPEED > /sys/bus/usb/drivers/kraken/DEVICE/speed
```

## Changing the color
The color must be in hexadecimal format (e.g., `ff00ff` for magenta).
```Shell
echo COLOR > /sys/bus/usb/drivers/kraken/DEVICE/color
```

The alternate color for the alternating mode can be set similarly.
```Shell
echo COLOR > /sys/bus/usb/drivers/kraken/DEVICE/alternate_color
```

## Changing the alternating and blinking interval
The interval is in seconds and must be between 1 and 255.
```Shell
echo INTERVAL > /sys/bus/usb/drivers/kraken/DEVICE/interval
```

## Changing the mode
The mode must be one of normal, alternating, blinking and off.
```Shell
echo MODE > /sys/bus/usb/drivers/kraken/DEVICE/mode
```

## Monitoring the liquid temperature
The liquid temperature is returned in °C.
```Shell
cat /sys/bus/usb/drivers/kraken/DEVICE/temp
```

## Monitoring the pump speed
The pump speed is returned in RPM.
```Shell
cat /sys/bus/usb/drivers/kraken/DEVICE/pump
```

## Monitoring the fan speed
The fan speed is returned in RPM.
```Shell
cat /sys/bus/usb/drivers/kraken/DEVICE/fan
```
