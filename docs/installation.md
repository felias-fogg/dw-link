
# Installation of firmware and software environment

There are only a few steps necessary for installing the dw-link firmware on the hardware debugger. Setting up the software environment is also quite simple, if you either opt for CLI debugging with AVR-GDB or the Arduino IDE 2. For other options, I refer to [PyAvrOCD](https://PyAvrOCD.io).

## Firmware installation

You need to install the dw-link firmware on an UNO. Connect the UNO to your computer and upload the firmware.

The simplest way is to download an uploader from the Release assets of the GitHub repo. This should fit your architecture, e.g., `dw-uploader-windows-intel64` for Windows. Under *Linux* and *macOS*, open a terminal window, go to the download folder, and set the executable permission using `chmod +x`. Afterward, execute the program. Under *Windows*, it is enough to start the program after downloading by double-clicking on it.

Alternatively, you can download or clone the dw-link repository and then compile and upload the dw-link Arduino sketch. You can also use PlatformIO for that purpose, because the repo is already in the right format.

## Setting up the debugging software

Finally, you need to install the debugging software on the host. 

### Installing AVR-GDB

If you want to perform debugging using a command-line interface, you only need to install avr-gdb, which is most probably already installed on your host anyway. On Linux, the package manager of your choice will solve the problem. Note that the corresponding package is named *gdb-avr*! On macOS, use [Homebrew](https://brew.sh). After having installed Homebrew, type:

```bash
xcode-select --install
brew tap osx-cross/avr
brew install avr-gdb
```

Under Windows, you can download a version from Zak's [avr-gcc-build](https://github.com/ZakKemble/avr-gcc-build) repository.

### Installing board packages for the Arduino IDE 2

Open the `Preferences` dialog of the Arduino IDE and paste the following URLs into the list of `Additional boards manager URLs`:

```
https://MCUdude.github.io/TinyCore/package_MCUdude_TinyCore_index.json
https://MCUdude.github.io/MicroCore/package_MCUdude_MicroCore_index.json
https://MCUdude.github.io/MiniCore/package_MCUdude_MiniCore_index.json
```
These package indices make
[*MicroCore*](https://github.com/MCUdude/MicroCore) (ATtin13(a)),
[*TinyCore*](https://github.com/MCUdude/TinyCore) (all other classic ATtinys), 
and [*MiniCore*](https://github.com/MCUdude/MiniCore) (ATmegaX8) known to the IDE. These cores are all debug-enabled.

Then, you need to start the  `Boards Manager`, which you find under `Tools`-->`Board`. Install any of the above cores you want. Note that the packages include tools that might be incompatible with older OS versions. If you encounter problems when starting the debugging software, consult the troubleshooting section on [startup problems](troubleshooting.md#startup-problems).

