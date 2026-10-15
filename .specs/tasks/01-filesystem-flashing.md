Kind: issue
Description: Server won't start or will respond with 500 error if FS is built not within same version of mklittlefs and flashed not at proper address that corresponds to the board selected Flash Size

### Context
Sketch is uploaded using arduino IDE with selected Flash Size option - 2MB (FS:1MB OTA:~512KB) 
That produced a `local.eagle.flash.ld` file with following content after compilation:
```
/* Flash Split for 2M chips */
/* sketch @0x40200000 (~1019KB) (1044464B) */
/* empty  @0x402FEFF0 (~4KB) (4112B) */
/* spiffs @0x40300000 (~1000KB) (1024000B) */
/* eeprom @0x403FB000 (4KB) */
/* rfcal  @0x403FC000 (4KB) */
/* wifi   @0x403FD000 (12KB) */
MEMORY
{
  dport0_0_seg : org = 0x3FF00000, len = 0x10
  dram0_0_seg : org = 0x3FFE8000, len = 0x14000
  irom0_0_seg : org = 0x40201010, len = 0xfeff0
}
PROVIDE ( _FS_start = 0x40300000 );
PROVIDE ( _FS_end = 0x403FA000 );
PROVIDE ( _FS_page = 0x100 );
PROVIDE ( _FS_block = 0x2000 );
PROVIDE ( _EEPROM_start = 0x403fb000 );
/* The following symbols are DEPRECATED and will be REMOVED in a future release */
PROVIDE ( _SPIFFS_start = 0x40300000 );
PROVIDE ( _SPIFFS_end = 0x403FA000 );
PROVIDE ( _SPIFFS_page = 0x100 );
PROVIDE ( _SPIFFS_block = 0x2000 );
INCLUDE "local.eagle.app.v6.common.ld"
```

All filesystem related operations done inside WSL having a COM13 port mirrorred to WSL using command `usbipd attach -a --wsl --busid 2-11`
The WSL port is `/dev/serial/by-id/usb-Silicon_Labs_CP2102_USB_to_UART_Bridge_Controller_0001-if00-port0`

Filesystem prepared with command:
`mklittlefs -l -b 8192 -s 1024000 fs.bin`

Filesystem content check command:
`mklittlefs -l -b 8192 -s 1024000 fs.bin`
outputs with:
```
1584    /index.html     Sun May 17 21:09:55 2026
Creation time:  Mon May 18 22:59:54 2026
```

Filesystem erased from esp using command (goal is to not touch sketch but only erase flash after 1Mb address to the end of memory):
`esptool --port /dev/serial/by-id/usb-Silicon_Labs_CP2102_USB_to_UART_Bridge_Controller_0001-if00-port0 --baud 115200 erase_region 0x100000 3145728`

Filesystem was written using command:
`esptool --port /dev/serial/by-id/usb-Silicon_Labs_CP2102_USB_to_UART_Bridge_Controller_0001-if00-port0 --baud 115200 write_flash --flash_mode dout 0x300000 fs.bin`

Files system was written into various addresses like 0x100000 0x200000 0x300000 with erasing before each flash write operaion. 

### Problem

FS written to address 0x300000 - sketch logs are:
```
[WiFi] Connecting to "multimedia_okrzei20"
[WiFi] Connected — IP 192.168.0.11
[FS] LittleFS mounted
Listing directory: /
[Web] Server started
[Mode] → AMBIENT
```

Note: no file in directory listing. And server http://192.168.0.11/ respond with 500 error

FS written to other addresses - sketch log:
```
[FS] LittleFS mount failed — web UI unavailable
```


# Solution

fs.bin have to be generated using strictly same version that is used by ESP8266 Arduino IDE package 
So it have to be done with command
`/mnt/c/Users/varloc2000/AppData/Local/Arduino15/packages/esp8266/tools/mklittlefs/3.1.0-gcc10.3-e5f9fec/mklittlefs.exe -c data -p 256 -b 8192 -s 1024000 fs.bin`
Or aliased version inside WSL
`mklittlefs_win -c data -p 256 -b 8192 -s 1024000 fs.bin`

So then it can be written:
`esptool --port /dev/serial/by-id/usb-Silicon_Labs_CP2102_USB_to_UART_Bridge_Controller_0001-if00-port0 --baud 115200 write-flash 0x100000 fs.bin`

The proper starting address for Flash Size 2MB is 0x100000

mklittlefs.exe from windows added as alias into WSL - mklittlefs_win 