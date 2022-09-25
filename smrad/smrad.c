#define _POSIX_C_SOURCE	202209L

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/ip.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#include "../temphum/include/temphum.h"

#define CSVFILE		"smrad_%Y%m%d_%H%M%S.csv"
#define ROTATE_MOD	60

#define SYSCALL_TYPE(what, type, badval, ...) ({ \
    type _e = what(__VA_ARGS__); \
    if (badval) { fprintf(stderr, "Error: %s returned %d (%M)\n", #what, _e); abort(); } \
    _e; })

#define SYSCALL(what, ...)	SYSCALL_TYPE(what, int, _e < 0, __VA_ARGS__)

#define LOG(fmt, ...)  fprintf(stderr, fmt "\n", __VA_ARGS__)
#define LOGN(fmt)  fprintf(stderr, fmt "\n")

union sa_in {
  struct sockaddr_in in;
  struct sockaddr sa;
};

int main(void)
{
  FILE *csv_fp = NULL;
  char csv_name[sizeof(CSVFILE) + 10];
  struct timespec last_csv_open = {};

  union sa_in sa = { .in = {
    .sin_family = AF_INET,
    .sin_port = htons(4242),
  }};

  int sock_fd = SYSCALL(socket, AF_INET, SOCK_DGRAM, 0);
  SYSCALL(bind, sock_fd, &sa.sa, sizeof(sa.in));
  SYSCALL(fcntl, sock_fd, F_SETFL, O_NONBLOCK);

  LOGN("started");

  while (1) {
    struct timespec now;
    SYSCALL(clock_gettime, CLOCK_REALTIME, &now);

    if (csv_fp && (now.tv_sec / ROTATE_MOD > last_csv_open.tv_sec / ROTATE_MOD))
    {
      LOG("closing file %s", csv_name);
      fclose(csv_fp);
      csv_fp = NULL;
    }

    struct pollfd pfd = {
      .fd = sock_fd,
      .events = POLLIN,
    };

    int timeout = !csv_fp ? -1 : (
	  (ROTATE_MOD - (now.tv_sec - last_csv_open.tv_sec)) * 1000
	- ((now.tv_nsec - last_csv_open.tv_nsec) / 1000000)) + 1;

    LOG("timeout is %d", timeout);

    switch (SYSCALL(poll, &pfd, 1, timeout))
    {
      case 0:
	LOGN("poll 0");
	continue;

      case 1:
	if (pfd.revents & POLLERR)
	{
	  LOGN("Error on socket, closing.");
	  abort();
	}

	if (!csv_fp)
	{
	  last_csv_open = now;

	  time_t csv_time = now.tv_sec - (now.tv_sec % ROTATE_MOD);
	  strftime(csv_name, sizeof(csv_name), CSVFILE, localtime(&csv_time)); 

	  LOG("opening file %s", csv_name);
	  csv_fp = SYSCALL_TYPE(fopen, FILE *, _e == NULL, csv_name, "a");
	  fprintf(csv_fp, "IP;TIME;TEMP;HUM\n");
	}

	while (1)
	{
	  struct packet rp;
	  union sa_in from;
	  socklen_t flen;
	  ssize_t e = recvfrom(sock_fd, &rp, sizeof(rp), 0, &from.sa, &flen);
	  if (e < 0)
	    if ((errno == EAGAIN) || (errno == EINTR))
	      break;
	    else
	    {
	      LOGN("Error on socket: %M");
	      abort();
	    }

	  if (e != sizeof(struct packet))
	  {
	    LOG("Wrong packet size: %d", e);
	    continue;
	  }

	  LOG("got a packet: %u %u", rp.temp, rp.hum);
	  double temp = -45 + 175 * rp.temp / 65535.0L;
	  double hum = rp.hum / 65535.0L;

	  fprintf(csv_fp, "%s;%llu;%.2lf;%.3lf\n",
	      inet_ntoa(from.in.sin_addr),
	      now.tv_sec, temp, hum);
	}

	fflush(csv_fp);
	break;

      default:
	LOGN("Strange return value of poll()");
	abort();
    }
  }
}
