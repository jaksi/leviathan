# leviathan
CLI application to control and monitor NZXT (and possibly other USB connected) liquid coolers from Linux

NZXT is **NOT** involved in this project, do **NOT** contact them if your device is damaged while using this software.

Also, while it doesn't seem like the hardware could be damaged by silly USB messages (apart from overheating), I do **NOT** take any responsibility for any damage done to your cooler.

# Supported devices
* NZXT Kraken X61 (Vendor/Product ID: `2433:b200`)
* NZXT Kraken X41 (Vendor/Product ID: `2433:b200`)

If you have an unsupported liquid cooler and want to help out, see [CONTRIBUTING.md](CONTRIBUTING.md).

# Installation
```Shell
sudo python3 -m pip install leviathan
```

# Usage
See the help message:
```Shell
sudo levctl --help
```

Note: unspecified arguments will still apply their default value. For example, executing `sudo levctl --color 0,0,255` will set the speed to 50%.
