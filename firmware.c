/* generic firmware-functions for HomeMatic
 *
 * Copyright (c) 2014-20 Michael Gernoth <michael@gernoth.net>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to
 * deal in the Software without restriction, including without limitation the
 * rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdint.h>
#include <string.h>
#include <strings.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/time.h>
#include <unistd.h>

#include "util.h"
#include "firmware.h"

#define CRC16_INIT	0xFFFF
#define CRC16_POLY	0x1021

/* This might be wrong, but it works for current fw */
#define MAX_BLOCK_LENGTH	2048

#define HEX_BLOCK_LENGTH_328P	128
#define HEX_BLOCK_LENGTH_644P	256
#define HEX_IMAGE_SIZE_328P	0x7000
#define HEX_IMAGE_SIZE_644P	0xF000
#define HEX_IMAGE_SIZE_MAX	0x10000

static uint16_t crc16(uint8_t* buf, int length, uint16_t crc)
{
	int i;
	uint16_t flag;

	while (length--) {
		for (i = 0; i < 8; i++) {
			flag = crc & 0x8000;
			crc <<= 1;
			if (*buf & 0x80) {
				crc |= 1;
			}
			if (flag) {
				crc ^= CRC16_POLY;
			}
			*buf <<= 1;
		}
		buf++;
	}

	return crc;
}

static struct firmware* firmware_read_ihex(int fd, struct firmware *fw, int atmega, int debug)
{
	uint8_t buf[2*MAX_BLOCK_LENGTH];
	uint8_t image[HEX_IMAGE_SIZE_MAX];
	uint16_t len = 0;
	uint16_t addr = 0;
	uint16_t type = 0;
	uint32_t offset = 0;
	uint32_t image_size = HEX_IMAGE_SIZE_328P;
	uint32_t block_length = HEX_BLOCK_LENGTH_328P;
	int r;
	int i;

	switch (atmega) {
		case ATMEGA_644P:
			printf("Using Atmega644P values for direct hex flashing\n");
			image_size = HEX_IMAGE_SIZE_644P;
			block_length = HEX_BLOCK_LENGTH_644P;
			break;
		case ATMEGA_328P:
			printf("Using Atmega328P values for direct hex flashing\n");
			image_size = HEX_IMAGE_SIZE_328P;
			block_length = HEX_BLOCK_LENGTH_328P;
			break;
		default:
			fprintf(stderr, "Atmega-type (328P/644P) not specified for flashing hex files\n");
			exit(EXIT_FAILURE);
			break;
	}

	memset(image, 0xff, sizeof(image));

	while (1) {
		memset(buf, 0, sizeof(buf));
		len = 2 /* len */ + 4 /* len */ + 2 /* type */;
		r = read(fd, buf, len);
		if (r < 0) {
			perror("read");
			exit(EXIT_FAILURE);
		} else if (r == 0) {
			fprintf(stderr, "EOF without EOF record, Firmware file not valid!\n");
			exit(EXIT_FAILURE);
		} else if (r != len) {
			printf("can't get record information!\n");
			exit(EXIT_FAILURE);
		}

		for (i = 0; i < r; i++) {
			if (!validate_nibble(buf[i])) {
				fprintf(stderr, "Firmware file not valid!\n");
				exit(EXIT_FAILURE);
			}
		}

		len = (ascii_to_nibble(buf[0]) & 0xf)<< 4;
		len |= ascii_to_nibble(buf[1]) & 0xf;

		addr = (ascii_to_nibble(buf[2]) & 0xf)<< 4;
		addr |= ascii_to_nibble(buf[3]) & 0xf;
		addr <<= 8;
		addr |= (ascii_to_nibble(buf[4]) & 0xf)<< 4;
		addr |= ascii_to_nibble(buf[5]) & 0xf;

		type = (ascii_to_nibble(buf[6]) & 0xf)<< 4;
		type |= ascii_to_nibble(buf[7]) & 0xf;

		if (debug)
			printf("Length: %d, Address: 0x%04x, Type: 0x%02x\n", len, addr, type);

		if (len > MAX_BLOCK_LENGTH) {
			fprintf(stderr, "Invalid block-length %u > %u for block %d!\n", len, MAX_BLOCK_LENGTH, fw->fw_blocks+1);
			exit(EXIT_FAILURE);
		}

		if (type == 0x00) {
			r = read(fd, buf, (len * 2) + 2 /* crc */);
			if (r < 0) {
				perror("read");
				exit(EXIT_FAILURE);
			} else if (r == 0) {
				break;
			} else if (r < ((len * 2) + 2)) {
				fprintf(stderr, "short read, aborting (%d < %d)\n", r, (len * 2) + 2);
				exit(EXIT_FAILURE);
			}

			for (i = 0; i < len * 2; i+=2) {
				if ((!validate_nibble(buf[i])) ||
				    (!validate_nibble(buf[i+1]))) {
					fprintf(stderr, "Firmware file not valid!\n");
					exit(EXIT_FAILURE);
				}

				image[addr + (i/2)] = (ascii_to_nibble(buf[i]) & 0xf)<< 4;
				image[addr + (i/2)] |= ascii_to_nibble(buf[i+1]) & 0xf;
			}

			while (1) {
				r = read(fd, buf, 1);
				if (r < 0) {
					perror("read");
					exit(EXIT_FAILURE);
				} else if (r == 0) {
					break;
				} else {
					if (buf[0] == ':') {
						break;
					}
				}
			}
		} else if (type == 0x01) {
			break;
		} else {
			fprintf(stderr, "Can't handle iHex type 0x%02x\n", type);
			exit(EXIT_FAILURE);
		}
	}

	image[image_size-2] = 0x00;
	image[image_size-1] = 0x00;

	while (offset < image_size) {
		fw->fw = realloc(fw->fw, sizeof(uint8_t*) * (fw->fw_blocks + 1));
		if (fw->fw == NULL) {
			perror("Can't reallocate fw->fw-blocklist");
			exit(EXIT_FAILURE);
		}

		len = block_length;

		fw->fw[fw->fw_blocks] = malloc(len + 4);
		if (fw->fw[fw->fw_blocks] == NULL) {
			perror("Can't allocate memory for fw->fw-block");
			exit(EXIT_FAILURE);
		}

		fw->fw[fw->fw_blocks][0] = (fw->fw_blocks >> 8) & 0xff;
		fw->fw[fw->fw_blocks][1] = fw->fw_blocks & 0xff;
		fw->fw[fw->fw_blocks][2] = (len >> 8) & 0xff;
		fw->fw[fw->fw_blocks][3] = len & 0xff;

		memcpy(fw->fw[fw->fw_blocks] + 4, image + offset, len);

		if ((len + offset) == image_size) {
			uint16_t crc;

			crc = crc16(image, image_size, CRC16_INIT);

			if (debug)
				printf("CRC: %04x\n", crc);

			fw->fw[fw->fw_blocks][len+3] = (crc >> 8) & 0xff;
			fw->fw[fw->fw_blocks][len+2] = crc & 0xff;
		}

		fw->fw_blocks++;
		if (debug)
			printf("Firmware block %d with length %u read.\n", fw->fw_blocks, len);

		offset += len;
	}

	if (fw->fw_blocks == 0) {
		fprintf(stderr, "Firmware file not valid!\n");
		exit(EXIT_FAILURE);
	}

	printf("Firmware with %d blocks successfully read.\n", fw->fw_blocks);

	return fw;
}

struct firmware* firmware_read_firmware(char *filename, int atmega, int debug)
{
	struct firmware *fw;
	struct stat stat_buf;
	uint8_t buf[2*MAX_BLOCK_LENGTH];
	uint16_t len;
	int fd;
	int r;
	int i;

	fw = malloc(sizeof(struct firmware));
	if (!fw) {
		perror("malloc(fw)");
		return NULL;
	}

	memset(fw, 0, sizeof(struct firmware));

	if (stat(filename, &stat_buf) == -1) {
		fprintf(stderr, "Can't stat %s: %s\n", filename, strerror(errno));
		exit(EXIT_FAILURE);
	}

	fd = open(filename, O_RDONLY);
	if (fd < 0) {
		fprintf(stderr, "Can't open %s: %s", filename, strerror(errno));
		exit(EXIT_FAILURE);
	}

	printf("Reading firmware from %s...\n", filename);

	memset(buf, 0, sizeof(buf));
	r = read(fd, buf, 1);
	if (r != 1) {
		perror("read");
		exit(EXIT_FAILURE);
	}

	//Intel hex?
	if (buf[0] == ':') {
		printf("HEX file detected (AsksinPP)\n");
		return firmware_read_ihex(fd, fw, atmega, debug);
	}

	if (lseek(fd, 0, SEEK_SET) != 0) {
		perror("lseek");
		exit(EXIT_FAILURE);
	}

	do {
		memset(buf, 0, sizeof(buf));
		r = read(fd, buf, 4);
		if (r < 0) {
			perror("read");
			exit(EXIT_FAILURE);
		} else if (r == 0) {
			break;
		} else if (r != 4) {
			printf("can't get length information!\n");
			exit(EXIT_FAILURE);
		}

		for (i = 0; i < r; i++) {
			if (!validate_nibble(buf[i])) {
				fprintf(stderr, "Firmware file not valid!\n");
				exit(EXIT_FAILURE);
			}
		}

		len = (ascii_to_nibble(buf[0]) & 0xf)<< 4;
		len |= ascii_to_nibble(buf[1]) & 0xf;
		len <<= 8;
		len |= (ascii_to_nibble(buf[2]) & 0xf)<< 4;
		len |= ascii_to_nibble(buf[3]) & 0xf;

		if (len > MAX_BLOCK_LENGTH) {
			fprintf(stderr, "Invalid block-length %u > %u for block %d!\n", len, MAX_BLOCK_LENGTH, fw->fw_blocks+1);
			exit(EXIT_FAILURE);
		}

		fw->fw = realloc(fw->fw, sizeof(uint8_t*) * (fw->fw_blocks + 1));
		if (fw->fw == NULL) {
			perror("Can't reallocate fw->fw-blocklist");
			exit(EXIT_FAILURE);
		}

		fw->fw[fw->fw_blocks] = malloc(len + 4);
		if (fw->fw[fw->fw_blocks] == NULL) {
			perror("Can't allocate memory for fw->fw-block");
			exit(EXIT_FAILURE);
		}

		fw->fw[fw->fw_blocks][0] = (fw->fw_blocks >> 8) & 0xff;
		fw->fw[fw->fw_blocks][1] = fw->fw_blocks & 0xff;
		fw->fw[fw->fw_blocks][2] = (len >> 8) & 0xff;
		fw->fw[fw->fw_blocks][3] = len & 0xff;

		r = read(fd, buf, len * 2);
		if (r < 0) {
			perror("read");
			exit(EXIT_FAILURE);
		} else if (r < len * 2) {
			fprintf(stderr, "short read, aborting (%d < %d)\n", r, len * 2);
			exit(EXIT_FAILURE);
		}

		for (i = 0; i < r; i+=2) {
			if ((!validate_nibble(buf[i])) ||
			    (!validate_nibble(buf[i+1]))) {
				fprintf(stderr, "Firmware file not valid!\n");
				exit(EXIT_FAILURE);
			}

			fw->fw[fw->fw_blocks][(i/2) + 4] = (ascii_to_nibble(buf[i]) & 0xf)<< 4;
			fw->fw[fw->fw_blocks][(i/2) + 4] |= ascii_to_nibble(buf[i+1]) & 0xf;
		}

		fw->fw_blocks++;
		if (debug)
			printf("Firmware block %d with length %u read.\n", fw->fw_blocks, len);
	} while(r > 0);

	if (fw->fw_blocks == 0) {
		fprintf(stderr, "Firmware file not valid!\n");
		exit(EXIT_FAILURE);
	}

	printf("Firmware with %d blocks successfully read.\n", fw->fw_blocks);

	return fw;
}

void firmware_free(struct firmware *fw)
{
	int i;

	for (i = 0; i < fw->fw_blocks; i++)
		free(fw->fw[i]);

	free(fw->fw);
	free(fw);
}
