/*
 * Description :  Xilinx Virtual Cable Server for Raspberry Pi
 *
 * See Licensing information at End of File.
 */


#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <time.h>
#include <stdint.h>

#include <sys/mman.h>
#include <fcntl.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/tcp.h>
#include <netinet/in.h>
#include <pthread.h>

#include <pigpio.h>
#include <signal.h>

#define ERROR_JTAG_INIT_FAILED -1
#define ERROR_OK 1

#define INP_GPIO(g) do { gpioSetMode(g, PI_INPUT); } while (0)
#define OUT_GPIO(g) do { gpioSetMode(g, PI_OUTPUT); } while (0)

static uint32_t bcm2835gpio_xfer(int n, uint32_t tms, uint32_t tdi);
static int bcm2835gpio_read(void);
static void bcm2835gpio_write(int tck, int tms, int tdi);

static int bcm2835gpio_init(void);
static int bcm2835gpio_quit(void);

/* GPIO numbers for each signal. Negative values are invalid */
static int tck_gpio = 11;
static int tms_gpio = 25;
static int tdi_gpio = 10;
static int tdo_gpio = 9;

/* Transition delay coefficients */
static unsigned int jtag_delay = 50;

static uint32_t bcm2835gpio_xfer(int n, uint32_t tms, uint32_t tdi)
{
	uint32_t tdo = 0;

	for (int i = 0; i < n; i++) {
		bcm2835gpio_write(0, tms & 1, tdi & 1);
		tdo |= bcm2835gpio_read() << i;
		bcm2835gpio_write(1, tms & 1, tdi & 1);
		tms >>= 1;
		tdi >>= 1;
	}
	return tdo;
}

static int bcm2835gpio_read(void)
{
	return gpioRead(tdo_gpio);
}

static void bcm2835gpio_write(int tck, int tms, int tdi)
{
	gpioWrite(tck_gpio, tck);
	gpioWrite(tms_gpio, tms);
	gpioWrite(tdi_gpio, tdi);

	for (unsigned int i = 0; i < jtag_delay; i++)
		asm volatile ("");
}

static void stop_running(int sig)
{
	gpioTerminate();
}

static int bcm2835gpio_init(void)
{
	if (gpioInitialise() < 0)
	{
		return ERROR_JTAG_INIT_FAILED;
	}

	gpioSetSignalFunc(SIGINT, stop_running);

	INP_GPIO(tdo_gpio);

	gpioWrite(tdi_gpio, 0);
	gpioWrite(tck_gpio, 0);
	gpioWrite(tms_gpio, 1);

	OUT_GPIO(tdi_gpio);
	OUT_GPIO(tck_gpio);
	OUT_GPIO(tms_gpio);

	return ERROR_OK;
}

static int verbose = 0;

static int sread(int fd, void *target, int len) {
   unsigned char *t = target;
   while (len) {
      int r = read(fd, t, len);
      if (r <= 0)
         return r;
      t += r;
      len -= r;
   }
   return 1;
}

int handle_data(int fd) {
	const char xvcInfo[] = "xvcServer_v1.0:2048\n";

	do {
		char cmd[16];
		unsigned char buffer[2048], result[1024];
		memset(cmd, 0, 16);

		if (sread(fd, cmd, 2) != 1)
			return 1;

		if (memcmp(cmd, "ge", 2) == 0) {
			if (sread(fd, cmd, 6) != 1)
				return 1;
			memcpy(result, xvcInfo, strlen(xvcInfo));
			if (write(fd, result, strlen(xvcInfo)) != strlen(xvcInfo)) {
				perror("write");
				return 1;
			}
			if (verbose) {
				printf("%u : Received command: 'getinfo'\n", (int)time(NULL));
				printf("\t Replied with %s\n", xvcInfo);
			}
			break;
		} else if (memcmp(cmd, "se", 2) == 0) {
			if (sread(fd, cmd, 9) != 1)
				return 1;
			memcpy(result, cmd + 5, 4);
			if (write(fd, result, 4) != 4) {
				perror("write");
				return 1;
			}
			if (verbose) {
				printf("%u : Received command: 'settck'\n", (int)time(NULL));
				printf("\t Replied with '%.*s'\n\n", 4, cmd + 5);
			}
			break;
		} else if (memcmp(cmd, "sh", 2) == 0) {
			if (sread(fd, cmd, 4) != 1)
				return 1;
			if (verbose) {
				printf("%u : Received command: 'shift'\n", (int)time(NULL));
			}
		} else {

			fprintf(stderr, "invalid cmd '%s'\n", cmd);
			return 1;
		}

		int len;
		if (sread(fd, &len, 4) != 1) {
			fprintf(stderr, "reading length failed\n");
			return 1;
		}

		int nr_bytes = (len + 7) / 8;
		if (nr_bytes * 2 > sizeof(buffer)) {
			fprintf(stderr, "buffer size exceeded\n");
			return 1;
		}

		if (sread(fd, buffer, nr_bytes * 2) != 1) {
			fprintf(stderr, "reading data failed\n");
			return 1;
		}
		memset(result, 0, nr_bytes);

		if (verbose) {
			printf("\tNumber of Bits  : %d\n", len);
			printf("\tNumber of Bytes : %d \n", nr_bytes);
			printf("\n");
		}

		bcm2835gpio_write(0, 1, 1);

		int bytesLeft = nr_bytes;
		int bitsLeft = len;
		int byteIndex = 0;
		uint32_t tdi, tms, tdo;

		while (bytesLeft > 0) {
			tms = 0;
			tdi = 0;
			tdo = 0;
			if (bytesLeft >= 4) {
				memcpy(&tms, &buffer[byteIndex], 4);
				memcpy(&tdi, &buffer[byteIndex + nr_bytes], 4);

				tdo = bcm2835gpio_xfer(32, tms, tdi);
				memcpy(&result[byteIndex], &tdo, 4);

				bytesLeft -= 4;
				bitsLeft -= 32;
				byteIndex += 4;

				if (verbose) {
					printf("LEN : 0x%08x\n", 32);
					printf("TMS : 0x%08x\n", tms);
					printf("TDI : 0x%08x\n", tdi);
					printf("TDO : 0x%08x\n", tdo);
				}

			} else {
				memcpy(&tms, &buffer[byteIndex], bytesLeft);
				memcpy(&tdi, &buffer[byteIndex + nr_bytes], bytesLeft);

				tdo = bcm2835gpio_xfer(bitsLeft, tms, tdi);
				memcpy(&result[byteIndex], &tdo, bytesLeft);

				bytesLeft = 0;

				if (verbose) {
					printf("LEN : 0x%08x\n", bitsLeft);
					printf("TMS : 0x%08x\n", tms);
					printf("TDI : 0x%08x\n", tdi);
					printf("TDO : 0x%08x\n", tdo);
				}
				break;
			}
		}

		bcm2835gpio_write(0, 1, 0);

		if (write(fd, result, nr_bytes) != nr_bytes) {
			perror("write");
			return 1;
		}

	} while (1);
	/* Note: Need to fix JTAG state updates, until then no exit is allowed */
	return 0;
}

int main(int argc, char **argv) {
   int i;
   int s;
   int c;

   struct sockaddr_in address;

   opterr = 0;

   while ((c = getopt(argc, argv, "vc:m:i:o:")) != -1)
      switch (c) {
      case 'v':
         verbose = 1;
      case '?':
         fprintf(stderr, "usage: %s [-v] [-c 0~31] [-m 0~31] [-i 0~31] [-o 0~31]\n", *argv);
         return 1;
      case 'c':
         tck_gpio = atoi(optarg);
      case 'm':
         tms_gpio = atoi(optarg);
      case 'i':
         tdi_gpio = atoi(optarg);
      case 'o':
         tdo_gpio = atoi(optarg);
      }

   if (bcm2835gpio_init() < 1) {
      fprintf(stderr,"Failed in bcm2835gpio_init()\n");
      return -1;
   }

   fprintf(stderr,"XVCPI initialized with: \n");
   fprintf(stderr,"  tck:gpio[%d], tms:gpio[%d], tdi:gpio[%d], tdo:gpio[%d]\n", tck_gpio,tms_gpio,tdi_gpio,tdo_gpio);

   s = socket(AF_INET, SOCK_STREAM, 0);

   if (s < 0) {
      perror("socket");
      gpioTerminate();
      return 1;
   }

   i = 1;
   setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &i, sizeof i);

   address.sin_addr.s_addr = INADDR_ANY;
   address.sin_port = htons(2542);
   address.sin_family = AF_INET;

   if (bind(s, (struct sockaddr*) &address, sizeof(address)) < 0) {
      perror("bind");
      gpioTerminate();
      return 1;
   }

   if (listen(s, 0) < 0) {
      perror("listen");
      gpioTerminate();
      return 1;
   }

   fd_set conn;
   int maxfd = 0;

   FD_ZERO(&conn);
   FD_SET(s, &conn);

   maxfd = s;

   while (1) {
      fd_set read = conn, except = conn;
      int fd;

      if (select(maxfd + 1, &read, 0, &except, 0) < 0) {
         perror("select");
         break;
      }

      for (fd = 0; fd <= maxfd; ++fd) {
         if (FD_ISSET(fd, &read)) {
            if (fd == s) {
               int newfd;
               socklen_t nsize = sizeof(address);

               newfd = accept(s, (struct sockaddr*) &address, &nsize);

               if (verbose)
                  printf("connection accepted - fd %d\n", newfd);
               if (newfd < 0) {
                  perror("accept");
               } else {
            	  int flag = 1;
            	  int optResult = setsockopt(newfd,
            			  	  	  	  	  	 IPPROTO_TCP,
            			  	  	  	  	  	 TCP_NODELAY,
            			  	  	  	  	  	 (char *)&flag,
            			  	  	  	  	  	 sizeof(int));
            	  if (optResult < 0)
            		  perror("TCP_NODELAY error");
                  if (newfd > maxfd) {
                     maxfd = newfd;
                  }
                  FD_SET(newfd, &conn);
               }
            }
            else if (handle_data(fd)) {

               if (verbose)
                  printf("connection closed - fd %d\n", fd);
               close(fd);
               FD_CLR(fd, &conn);
            }
         }
         else if (FD_ISSET(fd, &except)) {
            if (verbose)
               printf("connection aborted - fd %d\n", fd);
            close(fd);
            FD_CLR(fd, &conn);
            if (fd == s)
               break;
         }
      }
   }

   gpioTerminate();
   return 0;
}

/*
 * This work, "xvcpi.c", is a derivative of "xvcServer.c" (https://github.com/Xilinx/XilinxVirtualCable)
 * by Avnet and is used by Xilinx for XAPP1251.
 *
 * "xvcServer.c" is licensed under CC0 1.0 Universal (http://creativecommons.org/publicdomain/zero/1.0/)
 * by Avnet and is used by Xilinx for XAPP1251.
 *
 * "xvcServer.c", is a derivative of "xvcd.c" (https://github.com/tmbinc/xvcd)
 * by tmbinc, used under CC0 1.0 Universal (http://creativecommons.org/publicdomain/zero/1.0/).
 *
 * Portions of "xvcpi.c" are derived from OpenOCD (http://openocd.org)
 *
 * "xvcpi.c" is licensed under CC0 1.0 Universal (http://creativecommons.org/publicdomain/zero/1.0/)
 * by Derek Mulcahy.*
 */
