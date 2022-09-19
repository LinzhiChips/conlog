/*
 * conlog.c - Console logger
 *
 * Copyright (C) 2022 Linzhi Ltd.
 *
 * This work is licensed under the terms of the MIT License.
 * A copy of the license can be found in the file COPYING.txt
 */

#include <stddef.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <fcntl.h>
#include <poll.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/wait.h>


#define	BUF_SIZE	8192


static bool process(int in_fd, int out_fd, short events,
    int log_fd, off_t *len, off_t limit)
{
	char buf[BUF_SIZE];
	ssize_t got, wrote;
	ssize_t pos;

	if (events & POLLIN) {
		while (1) {
			got = read(in_fd, buf, BUF_SIZE);
			if (got < 0) {
				if (errno != EAGAIN)
					perror("read");
				break;
			}
			if (!got)
				return 0;
			for (pos = 0; pos < got; pos += wrote) {
				wrote = write(out_fd, buf + pos, got - pos);
				if (wrote <= 0)
					break;
			}
			if (*len + got > limit) {
				if (*len <= limit) {
					fprintf(stderr, "Stopping log\n");
					*len = limit + 1;
				}
				continue;
			}
			for (pos = 0; pos < got; pos += wrote) {
				wrote = write(log_fd, buf + pos, got - pos);
				if (wrote <= 0)
					break;
			}
			*len += got;
		}
	}
	return !(events & (POLLHUP | POLLERR));
}


static void logger(int log_fd, off_t limit, int stdout, int stderr)
{
	struct pollfd fds[2];
	off_t len = 0;


	if (fcntl(stdout, F_SETFL, O_NONBLOCK) < 0 ||
	    fcntl(stderr, F_SETFL, O_NONBLOCK) < 0) {
		perror("fcntl(O_NONBLOCK");
		exit(1);
	}

	while (1) {
		int *first = NULL;
		int n, got;

		n = 0;
		if (stdout >= 0) {
			fds[n].fd = stdout;
			fds[n].events = POLLIN;
			n++;
			first = &stdout;
		}
		if (stderr >= 0) {
			fds[n].fd = stderr;
			fds[n].events = POLLIN;
			n++;
			if (!*first)
				first = &stderr;
		}
		if (!n)
			_exit(0);

		got = poll(fds, n, -1);
		if (got < 0) {
			perror("poll");
			exit(1);
		}
		if (!got)
			continue;
		if (!process(fds[0].fd, *first == stdout ? 1 : 2,
		    fds[0].revents, log_fd, &len, limit))
			*first = -1;
		if (n == 2 &&
		    !process(fds[1].fd, 2, fds[1].revents, log_fd, &len, limit))
			stderr = -1;
	}
}


static void command(char **argv, int stdout, int stderr)
{
	if (dup2(stdout, 1) < 0 || dup2(stderr, 2) < 0) {
		perror("dup2");
		exit(1);
	}
	execvp(*argv, argv);
	perror(*argv);
	exit(1);
}


static void decode(int status, const char *name)
{
	if (WIFEXITED(status))
		fprintf(stderr, "%s: exit %d\n", name, WEXITSTATUS(status));
	else if (WIFSIGNALED(status))
		fprintf(stderr, "%s: signal %s (%d)\n",
		    name, strsignal(WTERMSIG(status)), WTERMSIG(status));
	else
		fprintf(stderr, "%s: status %d\n", name, status);
}


static int run(const char *log, off_t limit, char **argv)
{
	int stdout[2];
	int stderr[2];
	int fd;
	int pid_logger, pid_cmd;
	int pid;
	int status;

	if (pipe(stdout) < 0 || pipe(stderr) < 0) {
		perror("pipe");
		exit(1);
	}

	fd = open(log, O_CREAT | O_TRUNC | O_WRONLY);
	if (fd < 0) {
		perror(log);
		exit(1);
	}

	pid_logger = fork();
	if (pid_logger < 0) {
		perror("fork");
		exit(1);
	}

	if (!pid_logger) {
		(void) close(stdout[1]);
		(void) close(stderr[1]);
		(void) close(0);
		logger(fd, limit, stdout[0], stderr[0]);
	}
	(void) close(stdout[0]);
	(void) close(stderr[0]);

	pid_cmd = fork();
	if (pid_cmd < 0) {
		perror("fork");
		exit(1);
	}

	if (!pid_cmd)
		command(argv, stdout[1], stderr[1]);
	(void) close(stdout[1]);
	(void) close(stderr[1]);

	pid = wait(&status);
	if (pid < 0) {
		perror("wait");
		exit(1);
	}
	if (pid == pid_logger) {
		decode(status, "logger");
		pid = wait(&status);
		if (pid < 0) {
			perror("wait");
			exit(1);
		}
	}

	if (status)
		decode(status, *argv);
	return status;
}


static void usage(const char *name)
{
	fprintf(stderr, "usage: %s [-o bytes[kM]] logfile command ...\n", name);
	exit(1);
}


int main(int argc, char **argv)
{
	long limit = -1;
	char *end;
	int c;

	while ((c = getopt(argc, argv, "o:")) != EOF)
		switch (c) {
		case 'o':
			limit = strtoul(optarg, &end, 0);
			if (end[0] && end[1])
				usage(*argv);
			switch (*end) {
			case 'M':
				limit *= 1024;
				/* fall through */
			case 'k':
				limit *= 1024;
				break;
			case 0:
				break;
			default:
				usage(*argv);
			}
			break;
		default:
			usage(*argv);
                }

	if (argc - optind < 2)
		usage(*argv);

	return run(argv[optind], limit, argv + optind + 1);
}
