/* SPDX-License-Identifier: BSD-2-Clause */

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <limits.h>
#include <errno.h>
#include <getopt.h> /* SMELL: portability could be improved if getopt() */
#include <signal.h>
#include <time.h>
#include <assert.h>
#include <stdio.h>

/* SMELL: should better use stderr for debug/error messages, but keeping
	them all together makes reporting easier without timestamps */
/* TODO: timestamp messages to help correlate events with client */
static int debug;

static void regress_sighandler(int signo)
{
	signal(signo, SIG_DFL);
	fflush(stdout);
	raise(signo);
}

static int handle_connection(int pid, int client_socket, int client_id)
{
	int r;
	int client_exit = 0;
	char buffer[PIPE_BUF]; /* BUG: arbitrary, but almost avoid partial */
	clock_t start = 0, end; /* SMELL: initialized to calm compiler */

	printf("%d: connect %d\n", pid, client_id);
	do {
		int keep_lf = 0; /* SMELL: only supported by a few commands */
		ssize_t n = read(client_socket, buffer, 8);

		if (debug)
			printf("%d: DEBUG: last read %zd\n", pid, n);

		if (n <= 0) {
			if (n) {
				int saved_errno = errno;
				r = close(client_socket);
				if (r < 0)
					printf("%d: close(%d), %s\n",
						pid, client_socket,
						strerror(errno));
				printf("%d: pipe? %zd %d %s\n", pid, n, r,
							strerror(saved_errno));
				errno = 0;
			}
			if (debug && errno) {
				printf("%d: DEBUG: read %s\n", pid,
							strerror(errno));
				errno = 0;
			}
			break;
		}

		assert(n > 0);
		if ((n == 8) && !memcmp(buffer, "aaaaaaaa", 8)) {
			/* SMELL: should handle short read better */
			n = read(client_socket, buffer + 8, PIPE_BUF - 8);
			if (n != PIPE_BUF - 8)
				printf("%d: read(%zd) %s\n",
					pid, n, strerror(errno));
			if (n < 0) {
				errno = 0;
				break;
			} else if (errno) {
				if (debug)
					printf("%d: DEBUG: read %s\n", pid,
							strerror(errno));
				errno = 0;

			}
			keep_lf = 0;
			/* SMELL: full of garbage, dangerous to print */
			strcpy(buffer, "big (SYN)");
		} else {
			/* SMELL: could break with partial data requests */
			assert(n > 2);
			if (buffer[n - 2] == '\r')
				keep_lf = 2;
			else if (buffer[n - 1] == '\n')
				keep_lf = 1;
			else
				keep_lf = 0;
			buffer[n - keep_lf] = '\0';
		}

		printf("%d: command %s\n", pid, buffer);
		if (!memcmp(buffer, "ping", 4) || !memcmp(buffer, "big", 3)) {
			size_t s = (*buffer == 'b') ? PIPE_BUF : (size_t)n;

			/* SMELL: yes; really */
			assert(s < INT_MAX);
			assert((buffer[0] == 'p' && (keep_lf < n && n < 8)) ||
				buffer[0] == 'b');
			assert(!(buffer[0] == 'b' && keep_lf));

			buffer[1] = 'o';
			switch (keep_lf) {
				case 1:
					buffer[n - keep_lf] = '\n';
					break;
				case 2:
					buffer[n - keep_lf] = '\r';
					break;
			}
			/* TODO: should handle better partial/short write */
			n = write(client_socket, buffer, s);
			if (n < 0 || n != (ssize_t)s) {
				printf("%d: write(%zu) pipe? %zd %s\n",
					pid, s, n, strerror(errno));
				break;
			}
			if (s == PIPE_BUF && (n = write(client_socket, buffer, PIPE_BUF)) < 0) {
				printf("%d: write(PIPE_BUF) %zd %s\n",
					pid, n, strerror(errno));
				break;
			}
			if (debug)
				printf("%d: DEBUG: last write %zd\n", pid, n);
		} else if (!memcmp(buffer, "close", 5)) {
			r = close(client_socket);
			printf("%d: close %d\n", pid, r);
			client_exit = 1;
		} else if (!memcmp(buffer, "bug", 3)) {
			/* SMELL: could be abused by clients, atoi is unsafe */
			int i = atoi(buffer + 3);

			sleep((i < 0) ? 0 : (unsigned)i);
			client_exit = 1;
		} else if (!memcmp(buffer, "busy", 4)) {
			/* SMELL: could be abused by clients, atoi is unsafe */
			int i = atoi(buffer + 4);

			sleep((i < 0) ? 0 : (unsigned)i);
			/* TODO: should handle partial/short write */
			/* BUG: sprintf error not handled */
			n = write(client_socket, buffer,
					(size_t)sprintf(buffer, "%d\n", i));
			if (n < 0) {
				printf("%d: write() pipe? %zd %s\n",
					pid, n, strerror(errno));
				errno = 0;
			}
			if (debug)
				printf("%d: DEBUG: last write %zd\n", pid, n);
		} else if (!memcmp(buffer, "fork", 4)) {
			/* BUG: this leaks sockets and shouldn't hijack
				client_socket but instead report through
				parent */
			int ipc[2];
			pid_t child_pid;
			unsigned busy = 0;

			/* BUG: could be abused by client, unsafe */
			if (n > 4) {
				int i = atoi(buffer + 4);
				busy = (i < 0) ? 0 : (unsigned)i;
			}

			/* BUG: no report to peer on failure */
			if (pipe(ipc) < 0) {
				printf("%d: pipe! %s\n", pid, strerror(errno));
				errno = 0;
				continue;
			}

			child_pid = fork();
			if ((int)child_pid == -1) {
				printf("%d: fork! %s\n", pid, strerror(errno));
				errno = 0;
				continue;
			} else if (!child_pid) {
				pid_t my_pid = getpid();
				int s, t;

				if (debug)
					start = clock();

				close(ipc[0]);

				printf("%d: start\n", (int)my_pid);
				s = sprintf(buffer, "%d\n", my_pid);
				if (s < 0) {
					printf("%d: sprintf %s\n",
						(int)my_pid, strerror(errno));
					errno = 0;
					s = 0;
				}

				sleep(busy);
				n = write(client_socket, buffer, (size_t)s);
				t = (n != s);

				if (t) {
					printf("%d: write(%d) %zd %s\n",
						(int)my_pid, s, n,
						strerror(errno));
					errno = 0;
				}

				/* BUG: a failure will block the parent */
				if (write(ipc[1], &t, sizeof(t)) <= 0)
					printf("%d: pipe %s\n", (int)my_pid,
						strerror(errno));

				if (debug) {
					int i;
					printf("%d: DEBUG: last write %zd\n",
						(int)my_pid, n);
					end = clock();
					assert(start <= end && (end - start) <= INT_MAX);
					i = (int)(end - start);
					printf("%d: DEBUG: stop %d\n",
						(int)my_pid, i);
				}

				/* BUG: this status is lost */
				exit(t);
			} else {
				int status;

				close(ipc[1]);

				read(ipc[0], &status, sizeof(status));
				close(ipc[0]);

				printf("%d: child %d %d\n", pid,
						 (int)child_pid, status);
			}
		}
	} while (!client_exit);

	return !client_exit;
}

static int usage(const char *process_name)
{
	printf("%s [--debug] <options>\n", process_name);
	printf("A buggy TCP server meant for mocking common client/server\n");
	printf("failures in TCP based HTTP configurations\n\n");
	printf("Use a process per worker model that could block the\n");
	printf("listen socket and result in a backlog of requests.\n");
	printf("Allow flags to tweak how many workers will be fast\n");
	printf("an how much extra slowdown to apply.\n");
	printf("\n");
	printf("\t-D, --debug\t\tenable debugging messages\n");
	printf("\t-L, --leak\t\tleak filehandles to workers (0, 0x3)\n");
	printf("\t    --linger\t\tnumber of seconds to linger (0)\n");
	printf("\t-s, --slow\t\tadditional <slowdown> in seconds (0)\n");
	printf("\t-m, --max-workers\thow many fast workers allowed (3000)\n");
	printf("\t-h, --help\t\tthis help\n");
	return 0;
}

int main(int argc, char *argv[])
{
	int listen_socket;
	uint16_t port_number = 7777;
	struct sockaddr_in client_addr, server_addr = {0};
	int r;
	struct linger lin = {1, 0};
	pid_t pid = getpid();
	unsigned slow_listener = 0;
	char server_type[8] = {'s', 'i', 'm', 'p', 'l', 'e', '\0', 0};
	int max_workers = 3000; /* BUG: incorrectly sized for capacity */
	int leak = 0;

	static struct option long_options[] = {
		{"debug", no_argument, NULL, 'D'},
		{"leak", optional_argument, NULL, 'L'},
		{"help", no_argument, NULL, 'h'},
		{"slow", required_argument, NULL, 's'},
		{"max-workers", required_argument, NULL, 'm'},
		{"listen-queue", required_argument, NULL, 'l'},
		{"linger", required_argument, NULL, 1},
		{NULL, 0, NULL, 0}
	};

	while ((r = getopt_long(argc, argv, "DL:l:m:s:", long_options, NULL)) >= 0) {
		switch(r) {
			case 1: /* linger */
				lin.l_linger = atoi(optarg);
				break;
			case 'D':
				debug = 1;
				break;
			case 'L':
				leak = optarg ? atoi(optarg) : 0x3;
				if (!(leak & 0x1) && !(leak & 0x2)) {
					printf("incorrect value, using default (0x3)\n");
					leak = 0x3;
				}
				sprintf(server_type, "leaky:%d", leak);
				break;
			case 'l':
				/* TODO */
				break;
			case 'm':
				r = atoi(optarg);
				max_workers = (r < 0) ? 0 : r;
				break;
			case 's':
				r = atoi(optarg);
				slow_listener = (r < 0) ? 0 : (unsigned)r;
				break;
			default:
				return usage(argv[0]);
		}
	}

	/* BUG: prevents wait() and friends to work (at least in Linux) */
	signal(SIGCHLD, SIG_IGN);
	/* BUG: makes detecting broken connections slightly less reliable */
	signal(SIGPIPE, SIG_IGN);
	/* SMELL: only needed for regression testing */
	signal(SIGINT, regress_sighandler);

	/* TODO: support IPv6 */
	listen_socket = socket(AF_INET, SOCK_STREAM, 0);
	if (listen_socket < 0) {
		printf("FATAL: socket %s\n", strerror(errno));
		return 1;
	}

	/* BUG: causes resets in clients, might result in lost data */
	if (lin.l_linger >= 0) {
		if (setsockopt(listen_socket, SOL_SOCKET, SO_LINGER,
				(const void *)&lin, sizeof(lin)) < 0) {
			printf("FATAL: setsockopt(SO_LINGER) %s\n",
						strerror(errno));
			return 1;
		}
	} else if (debug)
		printf("DEBUG: SO_LINGER not enabled %d\n", lin.l_linger);

	/* BUG: when leaky might even randomly cause problems after restart */
	if (setsockopt(listen_socket, SOL_SOCKET, SO_REUSEADDR,
			(const void *)&lin.l_onoff, sizeof(int)) < 0) {
		printf("FATAL: setsockopt(SO_REUSEADDR) %s\n",
						strerror(errno));
		return 1;
	}

#ifdef SO_REUSEPORT
	/* BUG: could trigger partial requests from old retransmits */
	if (setsockopt(listen_socket, SOL_SOCKET, SO_REUSEPORT,
			(const void *)&lin.l_onoff, sizeof(int)) < 0) {
		printf("FATAL: setsockopt(SO_REUSEPORT) %s\n",
						strerror(errno));
		return 1;
	}
#endif

	/* BUG: might be atacked by a third party blocking "workers"
		with slow responses */
	server_addr.sin_family = AF_INET;
	server_addr.sin_addr.s_addr = INADDR_ANY;
	server_addr.sin_port = htons(port_number);
	if (bind(listen_socket,
		(struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
			printf("FATAL: bind %hu %s\n", port_number,
						strerror(errno));
			return 1;
	}

	/* BUG: too many entries here just increases latency */
	if (listen(listen_socket, 3000) < 0) {
		printf("FATAL: listen %s\n", strerror(errno));
		return 1;
	}

	printf("%d: start %s (%u,%d)\n", (int)pid, server_type,
				slow_listener, max_workers);

	while (1) {
		socklen_t client_socket_len = sizeof(client_addr);
		int client_socket;
		pid_t child_pid;
		int num_workers = 0;

		/* SMELL: using blocking sockets and not even events */
		client_socket = accept(listen_socket,
			(struct sockaddr *)&client_addr, &client_socket_len);
		if (client_socket < 0) {
			if (leak) {
				/* we are leaking so maybe there is another
				   server that can take the listener job */
				printf("%d: stop %s\n", (int)pid, strerror(errno));
				break;
			} else {
				printf("%d: FATAL: accept %s\n",
					(int)pid, strerror(errno));
				return 1;
			}
		}
		num_workers++;

		/* BUG: this is incorrect, but it is a tradeoff for
			debugability; nobody should do this but some
			just mask the issue by using threads
			which introduce other problems */
		child_pid = fork();
		if (!child_pid) {
			/* BUG: only async safe calls should be used
				starting here */
			int my_pid = (int)getpid();
			if ((leak & 0x2)) {
				if (debug)
					printf("%d: DEBUG: leak listen socket\n", my_pid);
			} else if (close(listen_socket) < 0) {
				printf("%d: BUG close(%d): %s\n", my_pid,
					listen_socket, strerror(errno));
				errno = 0;
			}

			/* TODOL client_id should support multiple sources */
			r = handle_connection(my_pid, client_socket,
						ntohs(client_addr.sin_port));

			if (r) {
				int e = close(client_socket);
				if (e < 0)
					printf("%d: BUG close(%d): %d %s\n",
						my_pid, client_socket, e,
						strerror(errno));
				printf("%d: abort?\n", my_pid);
			}
			if ((leak & 0x2)) {
				/* this might even work but is not
				   recommended and generally frowned upon
				   as it causes all sorts of races and
				   might break epoll() in Linux.
				   in BSD/macOS, kqueue() would specifically
				   prevent its file handler to be dup() by
				   fork(), probably for the same reason. */
				if (close(client_socket) < 0)
					printf("%d: BUG close(%d): %s\n",
						my_pid, client_socket,
						strerror(errno));
				errno = 0;
				printf("%d: nope!\n", my_pid);
				goto coin_toss;
			}

			/* BUG: nobody checks this status */
			exit(r);
		} else if ((int)child_pid == -1) {
			printf("%d: fork %s\n", (int)pid, strerror(errno));
			if (close(client_socket) < 0)
				printf("%d: BUG close(%d) %s\n",
					(int)pid, client_socket, strerror(errno));

			errno = 0;
			num_workers--;
		} else {
			if ((leak & 0x1)) {
				if (debug)
					printf("%d: DEBUG: leak client socket %d\n",
						(int)pid, client_socket);
			} else {
				r = close(client_socket);
				if (r < 0) {
					printf("%d: BUG close(%d): %d %s\n",
						(int)pid, client_socket, r,
						strerror(errno));
					errno = 0;
				}
			}
coin_toss:
			if (num_workers-- >= max_workers)
				sleep(slow_listener);
		}
	}

	close(listen_socket);

	/* BUG: obviously this status is not correct if leaking */
	return 0;
}
