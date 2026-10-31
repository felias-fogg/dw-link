
# Installation of firmware and software environment

## Firmware installation

If you work with the Arduino IDE, the simplest way to flash the hardware debugger firmware onto an Arduino UNO is to install the **dw-link** library using the library manager, and then upload the example sketch dw-link (`File` -> `Examples` -> `dw-link` -> `dw-link`). Alternatively, you can download or clone the `dw-link repository` and then compile and upload the dw-link sketch from the examples folder.

You can also use PlatformIO for that purpose, because the repo is already in the format of a PlatformIO project. Download or clone the repository and open it as a PlatformIO project. Now you can build and upload the sketch. 

A final alternative, if you are not into the Arduino IDE or PlatformIO, is to download the hex files of the firmware from the [**Releases**](https://github.com/felias-fogg/dw-link/releases/latest) tab of the repo. With avrdude or other flashing software, you can install the firmware on the MCU.

## Installation of debugging software

Setting up the debug software environment is also quite simple. It mainly consists of installing an alternative, debug-enabled Arduino core or a fork of the PlatformIO framework for AVR processors. Check out the debug software installation guide in the [PyAvrOCD manual](https://PyAvrOCD.io).
