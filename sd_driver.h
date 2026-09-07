#ifndef SD_DRIVER_H
#define SD_DRIVER_H

#define SD_INIT_SPEED 200 * 1000
#define SD_WORD_SIZE 8

int sd_probe (struct spi_device *spi);
void sd_shutdown (struct spi_device *spi);
void sd_remove (struct spi_device *spi);

#endif

