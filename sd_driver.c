#include <linux/module.h>
#include <linux/spi/spi.h>
#include <linux/printk.h>
#include <linux/slab.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/jiffies.h>
#include <linux/blkdev.h>

#include "sd_driver.h"

#define SD_COMMAND_BUFFER_SIZE 1024
#define SD_START_BIT BIT (7)
#define SD_TRANSMITION_BIT BIT (6)
#define SD_END_BIT BIT (0)

#define HCS_BIT BIT (30)
#define SD_VOLTAGE (uint32_t) 0b0001
#define CRC_ON 1
#define CRC_OFF 0
#define CRC16 2
#define DATA_BLOCK_LEN 512
#define CSD_LEN 16
#define DATA_START_TOKEN 1

#define MAX_RESPONSE_LEN 5
#define R1 1
#define R7 5

#define CMD0(priv, r1)\
		sd_send_command (priv, 0, 0, (r1), R1)

#define CMD8(priv, args, r7)\
		sd_send_command (priv, 8, (args), (r7), R7)

#define ACMD41(priv, args, r1)\
		(sd_send_command (priv, 55, 0, (r1), R1))? -EIO: sd_send_command (priv, 41, (args), (r1), R1)

#define CMD59(priv, args, r1)\
		sd_send_command (priv, 59, (args), r1, R1)

#define CMD9(priv, r1, csd)\
		sd_card_read_sector (priv, 9, 0, r1, csd, CSD_LEN + CRC16)

#define CMD17(priv, args, r1, data_block)\
		sd_card_read_sector (priv, 17, (args), r1, data_block, DATA_BLOCK_LEN + CRC16)

#define CMD24(priv, args, r1, data_block)\
		sd_card_write_sector (priv, args, r1, data_block)


struct sd_driver {
	struct spi_device *spi;
	struct gendisk *disk;
	char *tx_buf;
	char *rx_buf;

	struct mutex lock;
};

static const struct of_device_id sd_card_of_match[] = {
	{.compatible = "yaroslav-inc,sd_card"},
	{}
};

static const struct spi_device_id sd_card_spi_match[] = {
	{"sd_card", 0},
	{}
};

MODULE_DEVICE_TABLE (of, sd_card_of_match);
MODULE_DEVICE_TABLE (spi, sd_card_spi_match);

uint8_t crc7 (uint8_t *msg, int len);

uint8_t crc7 (uint8_t *msg, int len){
	uint8_t polynomial = 0b10001001;
	uint8_t crc = 0;

	for (int i = 0; i < len; i ++){
		crc ^= msg[i];
		for (int j = 0; j < 8; j ++){
			if (crc & 0x80) crc ^= polynomial;
			crc <<= 1;
		}
	}
	return (crc >> 1);
}	

uint16_t crc16 (uint8_t *msg, int len);

uint16_t crc16 (uint8_t *msg, int len){
	uint16_t polynomial = 0x1021;
	uint16_t crc = 0;

	for (int i = 0; i < len; i ++){
		crc ^= ((uint16_t) msg[i] << 8);
		for (int j = 0; j < 8; j ++){
			if (crc & 0x8000) crc = (crc << 1) ^ polynomial;
			else crc <<= 1;
		}
	}
	return crc;
}

int sd_spi_trs (struct spi_device *spi,
		char *rx,
	   	char *tx,
	   	unsigned len,
	   	unsigned cs_change);

int __sd_send_command (struct sd_driver *priv, 
				uint8_t index, 
				uint32_t args,
				char *response,
				uint8_t response_len);

int sd_send_command (struct sd_driver *priv, 
				uint8_t index, 
				uint32_t args,
				char *response,
				uint8_t response_len);

int sd_spi_trs (struct spi_device *spi,
		char *tx,
	   	char *rx,
	   	unsigned len,
	   	unsigned cs_change){

	struct spi_transfer trs = (struct spi_transfer) {
		.tx_buf = tx,
		.rx_buf = rx,
		.len = len,
		.cs_change = cs_change
	};

	return spi_sync_transfer (spi, &trs, 1);
}

int __sd_send_command (struct sd_driver *priv, 
				uint8_t index, 
				uint32_t args,
				char *response,
				uint8_t response_len){

	unsigned len = 6;
	char *tx = priv->tx_buf;
	char *rx = priv->rx_buf;
	int ret;

	tx[0] = (index | SD_TRANSMITION_BIT) & (~SD_START_BIT);
	tx[1] = args >> 24;
	tx[2] = args >> 16;
	tx[3] = args >> 8;
	tx[4] = args >> 0;
	tx[5] = (crc7 (tx, len - 1) << 1) | SD_END_BIT;
	//pr_info ("sd_command: %02x, %02x, %02x, %02x, %02x, %02x\n", tx[0], tx[1], tx[2], tx[3], tx[4], tx[5]); 
	if ((ret = sd_spi_trs (priv->spi, tx, rx, len, 1))) return ret;

	memset (tx, 0xff, response_len);
	for (int i = 0; i < 8; i ++){
		if ((ret = sd_spi_trs (priv->spi, tx, rx, 1, 1))) return ret;
		if (!(rx[0] & 0x80)) break;
	}

	if (response_len > 1){
		if ((ret = sd_spi_trs (priv->spi, tx, rx + 1, response_len - 1, 1))) return ret;
	}
	if (response){
		memcpy (response, rx, response_len);
	}

	return 0;
}

int sd_send_command (struct sd_driver *priv, 
				uint8_t index, 
				uint32_t args,
				char *response,
				uint8_t response_len){

	int ret = __sd_send_command (priv, index, args, response, response_len);
	if (ret) return ret;
	if ((ret = sd_spi_trs (priv->spi, priv->tx_buf, priv->rx_buf, 1, 0))) return ret;
	return 0;
}

int sd_card_read_sector (struct sd_driver *priv,
				uint8_t index,
				uint32_t args,
				char *r1,
				char *data,
				uint16_t data_len);

int sd_card_read_sector (struct sd_driver *priv,
				uint8_t index,
				uint32_t args,
				char *r1,
				char *data,
				uint16_t data_len){

	struct spi_device *spi = priv->spi;
	char *tx = priv->tx_buf;
	char *rx = priv->rx_buf;
	int ret;

	ret = __sd_send_command (priv, index, args, r1, R1);
	if (ret) return ret;
	if ((*r1)) return -EIO;

	memset (tx, 0xff, data_len);

	uint64_t start = get_jiffies_64 ();
	while (start + (HZ / 10) > get_jiffies_64 ()){
		rx[0] = 0xff;
		if ((ret = sd_spi_trs (spi, tx, rx, 1, 1))) return ret;
		if (*rx == 0b11111110 || !((*rx) & 0xf0)) break;
	}

	if (!((*rx) & 0xf0)){
		*r1 = *rx;
		return -EIO;
	}else if (!(*rx == 0b11111110)){
		return -EIO;
	}

	if ((ret = sd_spi_trs (spi, tx, rx, data_len, 0))) return ret;
	memcpy (data, rx, data_len);
	return 0;
}


int sd_card_write_sector (struct sd_driver *priv,
				uint32_t args,
				char *r1,
				char *data);

int sd_card_write_sector (struct sd_driver *priv,
				uint32_t args,
				char *r1,
				char *data){

	struct spi_device *spi = priv->spi;
	char *tx = priv->tx_buf;
	char *rx = priv->rx_buf;
	int ret;

	ret = __sd_send_command (priv, 24, args, r1, R1);
	if (ret) return ret;

	uint16_t crc = crc16 (data, DATA_BLOCK_LEN);
	tx[0] = 0xfe;
	memcpy (tx + 1, data, DATA_BLOCK_LEN);
	tx[DATA_BLOCK_LEN + 1] = (crc >> 8);
	tx[DATA_BLOCK_LEN + 2] = (crc >> 0);

	if ((ret = sd_spi_trs (spi, tx, rx, 1 + DATA_BLOCK_LEN + CRC16, 1))) return ret;

	tx[0] = 0xff;
	for (int i = 0; i < 8; i ++){
		if ((ret = sd_spi_trs (spi, tx, rx, 1, 1))) return ret;
		if (!(rx[0] & 0x10)){
			break;
		}
	}

	switch ((0x0f & rx[0])){
		case 0x05:
			uint64_t start = get_jiffies_64 ();
			while (start + (HZ / 2) > get_jiffies_64 ()){
				if ((ret = sd_spi_trs (spi, tx, rx, 1, 1))) return ret;
				if (*rx != 0x00) break;
			}
			if ((ret = sd_spi_trs (spi, tx, rx, 1, 0))) return ret;
			return 0;
	}

	pr_info ("debug: addr %u, datatoken %x\n", args, rx[0]);
	if ((ret = sd_spi_trs (spi, tx, rx, 1, 0))) return ret;
	return -EIO;
}

int init_sd_card (struct sd_driver *priv);

int init_sd_card (struct sd_driver *priv){
	char *rx = priv->rx_buf;
	char *tx = priv->tx_buf;
	struct spi_device *spi = priv->spi;
	int ret;
	char r[MAX_RESPONSE_LEN];

	memset (tx, 0xff, 8);
	if ((ret = sd_spi_trs (spi, tx, rx, 8, 0))) return ret; // dummy bytes
	
	if ((ret = CMD0 (priv, r))) return ret;

	if ((ret = CMD8 (priv, (SD_VOLTAGE << 8), r))) return ret;
	pr_info ("r7: %x, %x, %x, %x, %x\n", r[0], r[1], r[2], r[3], r[4]);

	if ((ret = CMD59 (priv, CRC_ON, r))) return ret;

	uint64_t start = get_jiffies_64 ();
	while (start + HZ > get_jiffies_64 ()){
		if ((ret = ACMD41 (priv, HCS_BIT, r))) return ret;
		if (!(*r & 0x01)) break;
	}
	if (*r & 0x01) return -EIO;
	pr_info ("r1: %x\n", r[0]);

	return 0;
}

int sd_card_do_bvec (struct sd_driver *priv,
		struct bio *bio,
		struct bio_vec *bvec,
		uint32_t pos);

int sd_card_do_bvec (struct sd_driver *priv,
		struct bio *bio,
		struct bio_vec *bvec,
		uint32_t pos){

	int ret;
	char r1;
	char buffer [DATA_BLOCK_LEN + CRC16];

	void *page = kmap_local_page (bvec->bv_page);
	uint8_t *data = (uint8_t *) page + bvec->bv_offset;
	enum req_op op = bio_op (bio);

	for (uint32_t i = 0; i < bvec->bv_len >> 9; i ++){
		switch (op){
			case REQ_OP_READ:
				ret = CMD17 (priv, pos + i, &r1, buffer);
				if (ret){
					kunmap_local (page);
					return ret;
				}
				memcpy (data, buffer, DATA_BLOCK_LEN);
				break;

			case REQ_OP_WRITE:
				ret = CMD24 (priv, pos + i, &r1, data);
				if (ret){
					kunmap_local (page);
					return ret;
				}
				break;

			default:
				kunmap_local (page);
				return -EIO;
		}
		data += DATA_BLOCK_LEN;
	}
	kunmap_local (page);
	return 0;
}

void sd_card_submit_bio (struct bio *bio);
void sd_card_submit_bio (struct bio *bio){
	struct bio_vec bvec;
	struct bvec_iter iter;
	struct sd_driver *priv = bio->bi_bdev->bd_disk->private_data;

	uint32_t pos = bio->bi_iter.bi_sector;
	mutex_lock (&priv->lock);
	bio_for_each_segment (bvec, bio, iter){
		if (sd_card_do_bvec (priv, bio, &bvec, pos)){
		   mutex_unlock (&priv->lock);
		   bio_io_error (bio);
		   return;
		}
		pos += bvec.bv_len >> 9;
	}
	mutex_unlock (&priv->lock);
	bio_endio (bio);
}

static const struct block_device_operations sd_card_blk_op = {
	.owner = THIS_MODULE,
	.submit_bio = sd_card_submit_bio
};

int sd_probe (struct spi_device *spi){
	struct sd_driver *priv;
	int ret = 0;

	spi->max_speed_hz = SD_INIT_SPEED;
	spi->bits_per_word = SD_WORD_SIZE;
	spi->mode = SPI_MODE_0;

	if (spi_setup (spi) != 0){
		pr_err ("coud not set up spi\n");
		ret = -EINVAL;
		goto fail;
	}

	char *tx = kmalloc (SD_COMMAND_BUFFER_SIZE, GFP_KERNEL);
	char *rx = kmalloc (SD_COMMAND_BUFFER_SIZE, GFP_KERNEL);

	priv = kzalloc (sizeof (*priv), GFP_KERNEL);

	if (!tx || !rx || !priv) {
		pr_err ("coud not allocate memory");
		ret = -ENOMEM;
		goto fail_free_memory;
	}

	priv->tx_buf = tx;
	priv->rx_buf = rx;
	priv->spi = spi;
	mutex_init (&priv->lock);

	spi_set_drvdata (spi, priv);

	ret = init_sd_card (priv);
	if (ret)
		goto fail_free_memory;


	char r1;
	char CSD[16 + CRC16];
	CMD9 (priv, &r1, CSD);

	uint64_t sectors = ((((CSD[7] & 0x1f) << 16) |
						(CSD[6] << 8) |
						(CSD[5] << 0)) + 1) * 1024;
	pr_info ("probe sectors: %llu\n", sectors);

	struct queue_limits lim = {
		.physical_block_size = DATA_BLOCK_LEN,
		.logical_block_size = DATA_BLOCK_LEN
	};

	struct gendisk *disk;

	disk = blk_alloc_disk (&lim, NUMA_NO_NODE);
	if (IS_ERR (disk)){
		ret = PTR_ERR (disk);
		goto fail_free_memory;
	}
	priv->disk = disk;

	disk->fops = &sd_card_blk_op;
	disk->private_data = priv;
	strcpy (disk->disk_name, "sd_card-yaroslav");
	set_capacity (disk, sectors);
	priv->disk = disk;

	ret = add_disk (disk);
	if (ret){
		goto fail_free_disk;
	}
	pr_info ("this is just a prob test\n");
	return 0;
fail_free_disk:
	put_disk (disk);
fail_free_memory:
	if (rx) kfree (rx);
	if (tx) kfree (tx);
	if (priv) kfree (priv);
fail:
	return ret;
}

void sd_remove (struct spi_device *spi){
	struct sd_driver *priv = spi_get_drvdata (spi);
	put_disk (priv->disk);
	kfree (priv->tx_buf);
	kfree (priv->rx_buf);
	kfree (priv);
	pr_info ("this is just a remove test\n");
}

static struct spi_driver sd_spi_driver = {
	.id_table = sd_card_spi_match,
	.probe = sd_probe,
	.remove = sd_remove,
	.driver = {
		.name = "yroslavs_sd_card_driver",
		.owner = THIS_MODULE,
		.of_match_table = sd_card_of_match
	}
};

module_spi_driver (sd_spi_driver);

MODULE_LICENSE ("GPL");

