/* flasher for HM-CFG-USB
 *
 * Copyright (c) 2013-14 Michael Gernoth <michael@gernoth.net>
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
#include <poll.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/time.h>
#include <libusb-1.0/libusb.h>

#include "hexdump.h"
#include "firmware.h"
#include "version.h"
#include "hmcfgusb.h"

struct recv_data {
	int ack;
};

static int parse_hmcfgusb(uint8_t *buf, int buf_len, void *data)
{
	struct recv_data *rdata = data;

	if (buf_len != 1)
		return 1;

	rdata->ack = buf[0];

	return 1;
}

int main(int argc, char **argv)
{
	const char twiddlie[] = { '-', '\\', '|', '/' };
	struct hmcfgusb_dev *dev;
	struct recv_data rdata;
	uint16_t len;
	struct firmware *fw;
	int block;
	int pfd;
	int debug = 0;

	printf("HM-CFG-USB flasher version " VERSION "\n\n");

	if (argc != 2) {
		if (argc == 1)
			fprintf(stderr, "Missing firmware filename!\n\n");

		fprintf(stderr, "Syntax: %s hmusbif.enc\n\n", argv[0]);
		exit(EXIT_FAILURE);
	}

	fw = firmware_read_firmware(argv[1], debug);
	if (!fw)
		exit(EXIT_FAILURE);

	hmcfgusb_set_debug(debug);

	memset(&rdata, 0, sizeof(rdata));

	dev = hmcfgusb_init(parse_hmcfgusb, &rdata);
	if (!dev) {
		fprintf(stderr, "Can't initialize HM-CFG-USB\n");
		exit(EXIT_FAILURE);
	}

	if (!dev->bootloader) {
		fprintf(stderr, "\nHM-CFG-USB not in bootloader mode, entering bootloader.\n");
		hmcfgusb_enter_bootloader(dev);
		fprintf(stderr, "\nWaiting for device to reappear...\n");

		do {
			sleep(2);
		} while ((dev = hmcfgusb_init(parse_hmcfgusb, &rdata)) == NULL);

		if (!dev->bootloader) {
			fprintf(stderr, "Can't enter bootloader, giving up!\n");
			exit(EXIT_FAILURE);
		}
	}

	printf("\nHM-CFG-USB opened.\n\n");


	printf("Flashing %d blocks", fw->fw_blocks);
	if (debug) {
		printf("\n");
	} else {
		printf(": %c", twiddlie[0]);
		fflush(stdout);
	}

	for (block = 0; block < fw->fw_blocks; block++) {
		len = fw->fw[block][2] << 8;
		len |= fw->fw[block][3];

		len += 4; /* block nr., length */

		if (debug)
			hexdump(fw->fw[block], len, "F> ");

		rdata.ack = 0;
		if (!hmcfgusb_send(dev, fw->fw[block], len, 0)) {
			perror("\n\nhmcfgusb_send");
			exit(EXIT_FAILURE);
		}

		if (debug)
			printf("Waiting for ack...\n");
		do {
			errno = 0;
			pfd = hmcfgusb_poll(dev, 1);
			if ((pfd < 0) && errno) {
				if (errno != ETIMEDOUT) {
					perror("\n\nhmcfgusb_poll");
					exit(EXIT_FAILURE);
				}
			}
			if (rdata.ack) {
				break;
			}
		} while (pfd < 0);

		if (rdata.ack == 2) {
			printf("\n\nFirmware update successfull!\n");
			break;
		}

		if (rdata.ack != 1) {
			fprintf(stderr, "\n\nError flashing block %d, status: %u\n", block, rdata.ack);
			exit(EXIT_FAILURE);
		}

		if (!debug) {
			printf("\b%c", twiddlie[block % sizeof(twiddlie)]);
			fflush(stdout);
		}
	}

	firmware_free(fw);

	hmcfgusb_close(dev);

	return EXIT_SUCCESS;
}
