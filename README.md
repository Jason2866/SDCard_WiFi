# Alternative WiFi SD Card Firmware (AFW)

*DISCLAIMER: This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.*

## Features

* FTP server (tested with FileZilla; for the FileZilla settings read the section FTP, please)
* WebDAV server
* WiFi config mode via an access point
* Over-the-Air (OTA) firmware update via a web interface
* Info page
* FAT16 and FAT32 support
* Supports up to 32 GB microSD/TF cards

![Printer Hookup Diagram](PrinterHookup2.jpg)

## Compile:

Use Platformio ;-)

#### Upload

1. Diag the switch on the module to `USB2UART`
2. Plug in the USB cable to your computer
3. Press and hold the module FLSH
4. Connect the USB cable to the module
5. Release the module FLSH button
6. Click Upload Button in VSC

A backup of the original firmware is always recommended.

### Over-the-Air (OTA)
This options is only available if you already have an installed AFW.

* Download the latest release
* Open a web browser
* Browse to `http://<your IP>/webota`
* Select the AFW firmware e.g. `firmware.bin`
* Click update


* Press the RST button to restart the device
* To verify the new firmware you can use a serial terminal e.g. minicom

* Now you can setup your WiFi, see section "WiFi Config"
* Put the WiFi SD Card back into your SD card slot

## Usage

### WiFi Config
If no WiFi settings are stored the device opens a new access point with the SSID `SDCard_WiFi`.

**How to set up**

* Connect via to the WiFi `SDCard_WiFi`
* Browse to `http://192.168.4.1`
* Configure your requested WiFi
* Save
