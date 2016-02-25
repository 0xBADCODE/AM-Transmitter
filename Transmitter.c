/* FTDI Transmitter, AM-OOK
 * Xeon 2016
 *
 * gcc ftdi_bitbang.c -o ftdi_bitbang -lftdi
 * 
 * under-clocking the signal seems to cause bells to ring...
 */

#include <stdio.h>
#include <string.h>
#include <math.h>
#include <signal.h>
#include <ftdi.h>

#define FTDI_VID 0x0403
#define FTDI_PID 0x6001

/* define some 8 bit masks to set pins high */
#define STOP 	0x00 /* 00000000 */
#define PIN_TX  0x01 /* 00000001 */
#define PIN_RX  0x02 /* 00000010 */
#define PIN_RTS 0x04 /* 00000100 */
#define PIN_CTS 0x08 /* 00001000 */
#define PIN_DTR 0x10 /* 00010000 */
#define PIN_DSR 0x20 /* 00100000 */
#define PIN_DCD 0x40 /* 01000000 */
#define PIN_RI  0x80 /* 10000000 */

#define BITRATE 2000 /* 2kbps */
#define FUDGE_FACTOR 1.425 /* compensate for chatty USB protocol */
#define REPEAT 1
#define RFID_HEADER 0x1FF
#define RFID_PREAMBLE

struct ftdi_context ftdic;
unsigned char x = PIN_TX, clk = STOP;
unsigned int w = 0;

static volatile int keepRunning = 1;

usage()
{
	printf("Usage: ftdi_bitbang [wsb]\n"
			"\tw\tinvert bits\n"
			"\ts\tunder-run clock attack\n"
			"\tb\tstring to tx\n"
			"\tr\trfid/8H10D format\n");
	exit(-1);
}

void ftdi_check()
{
	printf("\nFTDI context: %p\n", &ftdic);
	printf("ftdi chip type:%d\n", ftdic.type);
	printf("baudrate: %d\n", ftdic.baudrate);
	printf("readbuffer: %d\n", *ftdic.readbuffer);
	printf("readbuffer offset: %d\n", ftdic.readbuffer_offset);
	printf("readbuffer chunksize: %d\n", ftdic.readbuffer_chunksize);
	printf("readbuffer remaining: %d\n", ftdic.readbuffer_remaining);
	printf("write buffer chunksize: %d\n", ftdic.writebuffer_chunksize);
	printf("max_packet_size: %d\n", ftdic.max_packet_size);
}

void intHandler()
{
	keepRunning = 0;
}

void print_binary(unsigned int n)
{
	int b;
	for (b = 7; b >= 0; b--)
		printf("%d", (n >> b) & 0x1 ? 1 : 0);
	return;
}

int parity(int b)
{
	int n;
	for(n = 0; b > 0; b >>= 1)
		n += b & 0x1;
	return n % 2;
}

void ftdi_fatal (char *str)
{
	fprintf(stderr, "%s: %s\n", str, ftdi_get_error_string(&ftdic));
	ftdi_usb_close(&ftdic);
	//ftdi_free(&ftdic);
	exit(-1);
}

int switch_carrier(char x)
{
	/* x = 0 to turn carrier off, x = 1 to turn carrier on */
	usleep(500000);
	if(ftdi_write_data(&ftdic, &x, 1) < 0)
		ftdi_fatal("Can't write to device\n");
	usleep(500000);
	return 0;
}

int tx(char x)
{
	if(w) x ^= INVERT_TXD;
	if(ftdi_write_data(&ftdic, &x, 1) < 0)
		ftdi_fatal("Can't write to device\n");
//	printf("%d", x); //DEBUG
	usleep(round((1e6 / (double)(BITRATE * FUDGE_FACTOR))));
	return 0;
}

int manc_tx(char x)
{
	if(w) x ^= INVERT_TXD;
	if(x)
	{
		if(ftdi_write_data(&ftdic, &x, 1) < 0)
			ftdi_fatal("Can't write to device\n");
	//	printf("%d", x); //DEBUG
		x ^= INVERT_TXD;
		usleep(round((1e6 / (double)(BITRATE * FUDGE_FACTOR))));
		if(ftdi_write_data(&ftdic, &x, 1) < 0)
			ftdi_fatal("Can't write to device\n");
	//	printf("%d", x); //DEBUG
	}
	else
	{
		if(ftdi_write_data(&ftdic, &x, 1) < 0)
			ftdi_fatal("Can't write to device\n");
	//	printf("%d", x); //DEBUG
		x ^= INVERT_TXD;
		usleep(round((1e6 / (double)(BITRATE * FUDGE_FACTOR))));
		if(ftdi_write_data(&ftdic, &x, 1) < 0)
			ftdi_fatal("Can't write to device\n");
	//	printf("%d", x); //DEBUG
	}
	usleep(round((1e6 / (double)(BITRATE * FUDGE_FACTOR))));
	return 0;
}

int clock_attack()
{
	signal(SIGINT, intHandler);

	printf("Starting clock attack. Under-running clock signal...\n");
	switch_carrier(1);

	while(keepRunning)
	{
		clk ^= INVERT_TXD;
		if(ftdi_write_data(&ftdic, &clk, 1) < 0)
			ftdi_fatal("Can't write to device\n");
	//	printf("%d", clk); //DEBUG
		usleep(1e6 / (double)BITRATE);
	}
	switch_carrier(0);
	printf("\nCaught interupt, stopping clock attack.\n");
	return 0;
}

int send_code(char *code)
{
	printf("\nSending code: '%s'\n", code);
	switch_carrier(1);

	int i, j, n = 0;

	while(n++ < REPEAT)
	{
		for(i = 0; i < strlen(code); i++) //FIX ESCAPE 0x00, 0xa and 0x20!
		{
			for(j = 7; j >= 0; j--)
				tx((code[i] >> j) & 0x1);
		}
	}
	switch_carrier(0);
	printf("\nSent %d bits successfully %d times.\n", i * 8, REPEAT);
	return 0;
}

int rfid(char *code)
{
	int i, j, n;
	char row_pbit[strlen(code)], col_pbit[4];

	memset(&row_pbit, 0, sizeof(row_pbit));
	memset(&col_pbit, 0, sizeof(col_pbit));

	printf("\nCalculating parity bits...\n");

	//calc parity bit for each row
	for(i = 0; i < strlen(code); i++)
		row_pbit[i] = parity(code[i]);

	//calc parity bit for each col
	for(j = 0; j < sizeof col_pbit; j++)
	{
		for(i = 0, n = 0; i < sizeof row_pbit; i++)
			n += (code[i] >> j) & 0x1;
		col_pbit[sizeof col_pbit - 1 - j] = n % 2;
	}

//	for(i = 0; i < sizeof row_pbit; i++) printf("%d", row_pbit[i]); //DEBUG
//	for(i = 0; i < sizeof col_pbit; i++) printf("%d", col_pbit[i]); //DEBUG

	switch_carrier(1);
	printf("\nSending header...\n");
	printf("Sending code: '%s'\n", code);

	n = 0;
	while(n++ < REPEAT)
	{
		/* send RFID_HEADER */
		for(j = 8; j >= 0; j--)
			manc_tx((RFID_HEADER >> j) & 0x1);

		/* send nibble + row parity bit */
		for(i = 0; i < sizeof row_pbit; i++)
		{
			for(j = 3; j >= 0; j--)
				manc_tx((code[i] >> j) & 0x1);
			manc_tx(row_pbit[i]);
		}

		/* send col parity bits and stop bit */
		for(j = sizeof col_pbit - 1; j >= 0; j--)
			manc_tx(col_pbit[j]);
		manc_tx(0);
	}
	printf("\nSent %lu bits successfully %d times.\n",
		9 + sizeof col_pbit + sizeof row_pbit + sizeof row_pbit * 4 + 1, REPEAT);

	switch_carrier(0);
	return 0;
}

int main(int argc, char *argv[])
{
	int t;

	/* Initialize context for subsequent function calls */
	printf("Initializing FTDI chip...");
	ftdi_init(&ftdic);
	printf("done.\n");

	/* Open FTDI device based on FT232R vendor & product IDs */
	if(ftdi_usb_open(&ftdic, FTDI_VID, FTDI_PID) < 0)
		ftdi_fatal("Can't open device");

	/* Enable bitbang mode */
	printf("Enabling bitbang mode.\n");
	//ftdi_enable_bitbang(&ftdic, PIN_TX); // DEPRECATED
	if(ftdi_set_bitmode(&ftdic, PIN_TX, BITMODE_BITBANG) < 0)
		ftdi_fatal("Can't enable bitbang mode");

	if(!ftdic.bitbang_enabled)
		ftdi_fatal("Bitbang mode not enabled");

	printf("Bitrate: %dbps\n", BITRATE);

	while ((t = getopt (argc, argv, "wsr:b:h")) != -1)
	{
		switch (t)
		{
			case 'w':
				w ^= 0x1;
				if(w) printf("Inverting bits...\n");
			break;

			case 'b': send_code(optarg);
			continue;

			case 'r': rfid(optarg);
			continue;

			case 's': clock_attack();
			continue;

			case 'h': usage();

			default: usage();
		}
	}

	ftdi_usb_close(&ftdic);

//	ftdi_check();

	ftdi_deinit(&ftdic);

	//printf("\nDestroying FTDI context: %p\n", &ftdic);
	//ftdi_free(&ftdic);

	return 0;
}
