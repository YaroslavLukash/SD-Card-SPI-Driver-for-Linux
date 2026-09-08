# SD-card SPI Driver for Linux  

Linux kernel block device driver for SD/SDHC cards made to operate over SPI with SD-card on embedded systems. This driver registers the SD-card as a regular block device (/dev/sd\_card-yaroslav). Implemented SD-card communication, block-level read/write operations, device registration, and filesystem persistence testing on Raspberry Pi hardware.
## Technologies  

* Linux kernel API for block devices (bio\_for\_each\_segment, gendisk, bio, bio\_vec...)
* SPI protocol and Linux kernel SPI subsystem
* Device tree and device binding
* SD-card protocol (CMD0, ACMD41, CMD17, CMD24...)
* Make utility 
* Synchronization (mutex\_t, spin\_lock\_t)
## Implementation 
 
                +------------+
                |Raspberry Pi|
                +------------+
                      |
                      | Kernel Block I/O requests
                      |
                +------------+
                | Our Driver |
                +------------+
                      |
                      |  Breaks up I/O requests into sequence of single-sector read/write commands in SD-card format
                      |
                +------------+
                |On-board spi|
                |Controller  |
                +------------+
                      |
                      |
                      |
                +-----------+
                |  SD-card  |
                +-----------+
## Setup  

![Photo of SD card and Raspberry Pi connected via jumper wires.](/assets/photo_2026-09-08_11-03-59.jpg)  

## Testing  
### 1. Build and load the module

* make
* sudo insmod sd\_driver.ko
* sudo dmesg
![](/assets/photo_2026-09-08_11-03-44.jpg)
### dmesg

![](/assets/photo_2026-09-08_11-03-45.jpg)
### 2. Verify block device and create ext4 filesystem

* lsblk
* sudo mkfs.ext4 /dev/sd\_card-yaroslav
![](/assets/photo_2026-09-08_11-03-47.jpg)
### 3. Mount and test read/write persistence

* mkdir test  
* sudo mount /dev/sd\_card-yaroslav test/
* sudo chown -R yaroslav: test/  
* echo "this is just a test" > test/temp.txt
* cat test/temp.txt  
* sudo umount test/
* sudo mount /dev/sd\_card-yaroslav test/
* cat test/temp.txt
![](/assets/photo_2026-09-08_11-03-50.jpg)
## Limitations

This project is intended as an experimental/educational Linux kernel
driver.

Current limitations include:

* Tested on Raspberry Pi hardware only
* Supports SD/SDHC cards
* No support for multi sector read/write commands
