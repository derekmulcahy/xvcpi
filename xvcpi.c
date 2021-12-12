/*
 * Description :  Xilinx Virtual Cable Server for Raspberry Pi
 *
 * See Licensing information at End of File.
 */


#include <bcm_host.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <netinet/tcp.h>
#include <netinet/in.h>
#include <sys/mman.h>
#include <sys/socket.h>

// https://www.raspberrypi.com/documentation/computers/raspberry-pi.html#peripheral-addresses
#define DO_PADS
#ifdef DO_PADS
#define BCM2835_PADS_GPIO_0_27		(bcm2835_peri_base + 0x100000)
#define BCM2835_PADS_GPIO_0_27_OFFSET	(0x2c / 4)
#endif

/* GPIO setup macros */
#define MODE_GPIO(g) (*(pio_base+((g)/10))>>(((g)%10)*3) & 7)
#define INP_GPIO(g) do { *(pio_base+((g)/10)) &= ~(7<<(((g)%10)*3)); } while (0)
#define SET_MODE_GPIO(g, m) do { /* clear the mode bits first, then set as necessary */ \
      INP_GPIO(g);						\
      *(pio_base+((g)/10)) |=  ((m)<<(((g)%10)*3)); } while (0)
#define OUT_GPIO(g) SET_MODE_GPIO(g, 1)

#define GPIO_SET (*(pio_base+7))  /* sets   bits which are 1, ignores bits which are 0 */
#define GPIO_CLR (*(pio_base+10)) /* clears bits which are 1, ignores bits which are 0 */
#define GPIO_LEV (*(pio_base+13)) /* current level of the pin */

static int dev_mem_fd;
static volatile uint32_t *pio_base;

static bool     bcm2835gpio_init(void);
static int      bcm2835gpio_read(void);
static void     bcm2835gpio_write(int tck, int tms, int tdi);
static uint32_t bcm2835gpio_xfer(int n, uint32_t tms, uint32_t tdi);

/* GPIO numbers for each signal. Negative values are invalid */
static int tck_gpio = 11;
static int tms_gpio = 25;
static int tdi_gpio = 10;
static int tdo_gpio = 9;

static int verbose = 0;

/* Transition delay coefficients */
#define JTAG_DELAY (40)
static unsigned int jtag_delay = JTAG_DELAY;

static int bcm2835gpio_read(void)
{
   return !!(GPIO_LEV & 1<<tdo_gpio);
}

static void bcm2835gpio_write(int tck, int tms, int tdi)
{
   uint32_t set = tck<<tck_gpio | tms<<tms_gpio | tdi<<tdi_gpio;
   uint32_t clear = !tck<<tck_gpio | !tms<<tms_gpio | !tdi<<tdi_gpio;

   GPIO_SET = set;
   GPIO_CLR = clear;

   for (unsigned int i = 0; i < jtag_delay; i++)
      asm volatile ("");
}

static uint32_t bcm2835gpio_xfer(int n, uint32_t tms, uint32_t tdi)
{
   uint32_t tdo = 0;

   for (int i = 0; i < n; i++) {
      bcm2835gpio_write(0, tms & 1, tdi & 1);
      bcm2835gpio_write(1, tms & 1, tdi & 1);
      tdo |= bcm2835gpio_read() << i;
      tms >>= 1;
      tdi >>= 1;
   }
   return tdo;
}

static bool bcm2835gpio_init(void)
{
   unsigned int bcm2835_peri_base = bcm_host_get_peripheral_address();
   unsigned int bcm2835_peri_size = bcm_host_get_peripheral_size();
   unsigned int bcm2835_gpio_offset = 0x200000;

   dev_mem_fd = open("/dev/mem", O_RDWR | O_SYNC);
   if (dev_mem_fd < 0) {
      perror("open");
      return false;
   }

   if (verbose) {
      printf("address=%08x size=%08x\n", bcm_host_get_peripheral_address, bcm_host_get_peripheral_size);
   }
   pio_base = mmap(NULL, bcm2835_peri_size, PROT_READ | PROT_WRITE,
            MAP_SHARED, dev_mem_fd, bcm2835_peri_base + bcm2835_gpio_offset);

   if (pio_base == MAP_FAILED) {
      perror("mmap");
      close(dev_mem_fd);
      return false;
   }

#ifdef DO_PADS
   static volatile uint32_t *pads_base;
   pads_base = mmap(NULL, sysconf(_SC_PAGE_SIZE), PROT_READ | PROT_WRITE,
            MAP_SHARED, dev_mem_fd, BCM2835_PADS_GPIO_0_27);

   if (pads_base == MAP_FAILED) {
      perror("mmap");
      close(dev_mem_fd);
      return false;
   }

   /* set 4mA drive strength, slew rate limited, hysteresis on */
   // https://www.scribd.com/doc/101830961/GPIO-Pads-Control2
   // https://www.raspberrypi.com/documentation/computers/raspberry-pi.html#gpio-pads-control
   pads_base[BCM2835_PADS_GPIO_0_27_OFFSET] = 0x5a000008 + 1;
#endif

   /*
    * Configure TDO as an input, and TDI, TCK, TMS
    * as outputs.  Drive TDI and TCK low, and TMS high.
    */
   INP_GPIO(tdo_gpio);

   GPIO_CLR = 1<<tdi_gpio | 1<<tck_gpio;
   GPIO_SET = 1<<tms_gpio;

   OUT_GPIO(tdi_gpio);
   OUT_GPIO(tck_gpio);
   OUT_GPIO(tms_gpio);

   bcm2835gpio_write(0, 1, 0);

   return true;
}

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

   while ((c = getopt(argc, argv, "vd:")) != -1) {
      switch (c) {
      case 'v':
         verbose = 1;
         break;
      case 'd':
         jtag_delay = atoi(optarg);
         if (jtag_delay < 0)
             jtag_delay = JTAG_DELAY;
         break;
      case '?':
         fprintf(stderr, "usage: %s [-v]\n", *argv);
         return 1;
      }
   }
   if (verbose)
      printf("jtag_delay=%d\n", jtag_delay);

   if (!bcm2835gpio_init()) {
      fprintf(stderr,"Failed in bcm2835gpio_init()\n");
      return -1;
   }

   s = socket(AF_INET, SOCK_STREAM, 0);

   if (s < 0) {
      perror("socket");
      return 1;
   }

   i = 1;
   setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &i, sizeof i);

   address.sin_addr.s_addr = INADDR_ANY;
   address.sin_port = htons(2542);
   address.sin_family = AF_INET;

   if (bind(s, (struct sockaddr*) &address, sizeof(address)) < 0) {
      perror("bind");
      return 1;
   }

   if (listen(s, 0) < 0) {
      perror("listen");
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
