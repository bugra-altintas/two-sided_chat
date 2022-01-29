/*
 * Mitchell Tasman
 * December 1987
 * Modified by Marvin Solomon, October 1989.
 * Program totroll.c
 *
 * Testing program to test the "troll" (q.v.)
 * Sends messages via the troll to a another process 
 * The other process is supposed to be fromtroll.c,
 * which just prints what it gets.
 */
#ifndef lint
static char *rcsid = "$Header: /var/home/solomon/640/troll/RCS/totroll.c,v 3.2 1991/04/13 22:38:01 solomon Distrib solomon $";
#endif

#include <stdio.h>
#include <string.h>
#include <sys/param.h>
#include <sys/types.h>
#include <sys/signal.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <ctype.h>
#include <netdb.h>

int errno;

typedef struct MyMessage {
	long contents;
} MyMessage;

/* interval between message sends */
struct timeval timeout = {
	1L, /* seconds */
	0L, /* microseconds */
};

int qflag;  /* quiet */

/* for lint */
void bzero(), bcopy(), exit(), perror();
double atof();
#define Printf if (!qflag) (void)printf
#define Fprintf (void)fprintf

main(argc,argv)
int argc;
char *argv[];
{

	int sock;	/* a socket for sending messages*/
	MyMessage message;
	struct hostent *host;
	u_short port;
	struct sockaddr_in trolladdr, localaddr;
	fd_set selectmask;
	int counter, n;
	int arg;

	/* process arguments */

	for (arg=1; arg<argc && argv[arg][0]=='-'; arg++) {
		char *p;
		for (p=argv[arg]+1; *p; p++) switch (*p) {
			case 'i': {
				double fsecs = 1.0;

				if (isdigit(p[1])) {
					fsecs = atof(p+1);
					p += strlen(p)-1;
				}
				else if (arg < argc-1 && isdigit(argv[arg+1][0])) {
					fsecs = atof(argv[++arg]);
				}
				else usage(argv[0]);
				timeout.tv_sec = fsecs;
				fsecs -= timeout.tv_sec;
				timeout.tv_usec = 1000000*fsecs;
				break;
			}
			case 'q': qflag++;
				break;
			default: usage(argv[0]);
		}
	}
					
	if (argc-arg != 3) usage(argv[0]);

	/* get troll address and port ... */

	if ((host = gethostbyname(argv[arg])) == NULL) {
		Fprintf(stderr, "%s: Unknown troll host '%s'\n",argv[0],argv[arg]);
		exit(1);
	}  

	port = atoi(argv[arg+1]);
	if (port < 1024 || port > 0xffff) {
		Fprintf(stderr, "%s: Bad troll port %d (must be between 1024 and %d)\n",
			argv[0], port, 0xffff);
		exit(1);
	}

	bzero ((char *)&trolladdr, sizeof trolladdr);
	trolladdr.sin_family = AF_INET;
	bcopy(host->h_addr, (char*)&trolladdr.sin_addr, host->h_length);
	trolladdr.sin_port = htons(port);

	/* get local port ... */

	port = atoi(argv[arg+2]);
	if (port < 1024 || port > 0xffff) {
		Fprintf(stderr, "%s: Bad local port %d (must be between 1024 and %d)\n",
			argv[0], port, 0xffff);
		exit(1);
	}

	/* create a socket for sending... */

	if ((sock = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
		perror("totroll socket");
		exit(1);
	}
	FD_ZERO(&selectmask);
	FD_SET(sock, &selectmask);

	/* ... and bind its local address and the port*/
	bzero((char *)&localaddr, sizeof localaddr);
	localaddr.sin_family = AF_INET;
	localaddr.sin_addr.s_addr = INADDR_ANY; /* let the kernel fill this in */
	localaddr.sin_port = htons(port);
	if (bind(sock, (struct sockaddr *)&localaddr, sizeof localaddr) < 0) {
		perror("client bind");
		exit(1);
	}

	/* Main loop */

	counter = 0;

	for(;;) {
		sleep(timeout.tv_sec);
		message.contents = counter++;
		errno = 0;
		Printf(">>> sending message content=%d\n",message.contents);
		n = sendto(sock, (char *)&message, sizeof message, 0,
						(struct sockaddr *)&trolladdr, sizeof trolladdr);
		if (n!=sizeof message) {
			perror("totroll sendto");
			exit(1);
		}
	}
} 

usage(prog)
char *prog;
{
	Fprintf(stderr, "usage: %s [-i <seconds> ]", prog);
	Fprintf(stderr, " <trollhost> <trollport> <localport> \n");
	exit(1);
}


