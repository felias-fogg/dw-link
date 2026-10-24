
# Installation of firmware and software environment

There are only a few steps necessary for installing the dw-link firmware on the hardware debugger. 

## Firmware installation

You need to install the dw-link firmware on an UNO. Connect the UNO to your computer and upload the firmware.

The simplest way is to download an uploader from the [Release assets of the GitHub repo](https://github.com/felias-fogg/dw-link/releases/latest). The name of the uploader should fit your architecture, e.g., `dw-uploader-windows-intel64` for Windows. Under *Linux* and *macOS*, open a terminal window, go to the download folder, and set the executable permission using `chmod +x`. Afterward, execute the program. Under *Windows*, it is enough to start the program after downloading by double-clicking on it. Instead of downloading an uploader, you can download the hex-file `dw-link.hex` and flash it with, e.g., `avrdude` on the UNO.

Alternatively, you can download or clone the dw-link repository and then compile and upload the dw-link Arduino sketch. You can also use PlatformIO for that purpose, because the repo is already in the right format.

## Installation of debugging software

Setting up the debug software environment is also quite simple. Check out the [debug software installation guide on PyAvrOCD](https://PyAvrOCD.io/debugging-software/).
