# SD-Card-SPI-Driver-for-Linux

Linux kernel block device driver for SD/SDHC cards made to operate over SPI withSD card on embedded systems. This driver registers SD card as regular block device (/dev/sd\_card-yaroslav).
  
## Setup  

![Foto of SD card and rasberry pi connected via jumper wires.](/assets/photo_2026-09-08_11-03-59.jpg)  

### SD card module 5v - 3.3v stepdown
![](/assets/photo_2026-09-08_11-03-54.jpg)  
### Raspbarry pi 3.2b  
![](/assets/photo_2026-09-08_11-03-52.jpg)

## Testing  
### 1. Buil and load the module

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
