
# Installation of firmware and software environment

There are only a few steps necessary for installing the dw-link firmware on the hardware debugger. Setting up the software environment is also quite simple, if you either opt for CLI debugging with AVR-GDB or the Arduino IDE 2. For other options, I refer to [PyAvrOCD](https://PyAvrOCD.io).

## Firmware installation

Since the firmware of the hardware debugger comes in the form of an Arduino sketch, you need to download, first of all, the [Arduino IDE](https://www.arduino.cc/en/software) or [Arduino CLI](https://docs.arduino.cc/arduino-cli/), if you have not done that already. You can also use [PlatformIO](https://platformio.org) as an alternative. 

Second, you need to download the dw-link firmware. If you use PlatformIO, note that the repository is already prepared to be opened as a PlatformIO project, i.e., it contains a `platformio.ini` file. Either download/clone the dw-link repository or download a release.

Third, you have to connect your future hardware debugger, i.e., the ATmega328 board, to your computer, select the right board in the Arduino IDE, and upload the `dw-link.ino` sketch to the board. Similarly, in PlatformIO, you have to choose the right board and choose the `Upload` menu entry.

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

Open the `Preferences` dialog of the Arduino IDE and paste the following three URLs into the list of `Additional boards manager URLs`:

```
https://downloads.pyavrocd.io/package_debug_enabled_index.json
https://MCUdude.github.io/MicroCore/package_MCUdude_MicroCore_index.json
https://MCUdude.github.io/MiniCore/package_MCUdude_MiniCore_index.json
```
The first package index will make the following board packages known to the IDE:

- *[Atmel AVR Xplained-minis (Debug enabled)](https://github.com/felias-fogg/avr-xminis-debug-enabled)*, which is a new core only for the three Atmega328/168 Xplained mini boards. It is based on MiniCore (see below), but is heavily tailored towards these development boards. Since these boards have a hardware debugger on board, dw-link is not needed here. 

- *[ATTinyCore (Debug enabled)](https://github.com/felias-fogg/ATTinyCore-debug-enabled)*, which is a fork of ATTinyCore-2.0.0 extended to allow for debugging.

The remaining two package indices make the [*MicroCore*](https://github.com/MCUdude/MicroCore) (ATtin13(a)) and [*MiniCore*](https://github.com/MCUdude/MiniCore) (ATmegaX8) known to the IDE. These cores are already debug-enabled.

Then, you need to start the  `Boards Manager`, which you find under `Tools`-->`Board`. Install any of the above cores you want. Note that the packages include tools that might be incompatible with older OS versions. If you encounter problems when starting the debugging software, consult the troubleshooting section on [startup problems](troubleshooting.md#startup-problems).

