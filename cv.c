/*
   Copyright (C) 2013 Xfennec, CQFD Corp.

   This program is free software: you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation, either version 3 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program.  If not, see <http://www.gnu.org/licenses/>.
   */

#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <string.h>

#include <unistd.h>
#include <dirent.h>
#include <errno.h>
#include <limits.h>
#include <ctype.h>
#include <libgen.h>

#include <getopt.h>

#include <sys/ioctl.h>

// for the BLKGETSIZE64 code section
#include <fcntl.h>
#include <sys/ioctl.h>
#include <linux/fs.h>

#ifdef BUILD_DAEMON
#include <libnotify/notify.h>
#endif

#include "cv.h"
#include "sizes.h"

char *proc_names[] = {"cp", "mv", "dd", "tar", "gzip", "gunzip", "cat", "grep", "cut", "sort", NULL};
char *proc_specific = NULL;
int flag_quiet = 0;
int flag_throughput = 0;
int flag_daemon = 0;
double throughput_wait_secs = 1;

ssize_t size_for_stat(struct struct stat *fd_target, int fdfd, char *name){
	if(!S_ISBLK(fd_target->st_mode))
		return fd_target->st_size;

	int fd = openat(fdfd, name, O_RDONLY);
	if(fd < 0)
		return -1;

	ssize_t rv = 0;
	if(ioctl(fd, BLKGETSIZE64, &rv) < 0)
		rv = -1;
	close(fd);
	return rv;
}

int diropen(int entryfd, char *name, int *fdout, DIR** dirout){
	int fd = openat(entryfd, "fd", O_SEARCH);
	if(!fd) /* Likely permission denied */
		return 1;
	*fdout = fd;

	DIR *adir = fdopendir(fd);
	if(!adir)
		return 1;
	*dirout = adir;
}

int biggest_file_for_pid(int pid, int entryfd, fdinfo_t *res){
	int rc = 0;
	/* /proc/2342/fd */
	int fdfd = 0;
	DIR *fddir = NULL;
	/* /proc/2342/fdinfo/23 */
	int infofd = 0;

	if(diropen(entryfd, "fd", &fdfd, &fddir)){
		rc = 1;
		goto cleanup;
	}

	size_t max_size = 0;
	struct max_size_info[sizeof("fdinfo") + sizeof("2147483648")];
	strcpy(max_size_info, "fdinfo/");

	struct dirent *fdent;
	while(fdent = readdir(fddir)){
		if(fdent->d_type != DT_LNK)
			continue;

		struct stat fd_target;
		if(fstatat(fdfd, fdent->d_name, &fd_target, 0))
			continue;

		size_t size_tmp = size_for_stat(&fd_target, fdfd, fdent->d_name);
		if(size_tmp < max_size)
			continue;

		max_size = size_tmp;
		strncpy(max_size_info + sizeof("fdinfo"), fdent->d_name, sizeof("2147483648"));
	}

	infofd = openat(entryfd, max_size_info, O_RDONLY);
	if(infofd < 0){
		rc = 1;
		goto cleanup;
	}

	/* According to seq_show in fs/proc/fd.c in the linux kernel sources, the fdinfo file will always start with the
	 * "pos" line. The pos field is printfed as a long long, i.e. 64 bit and thus never larger/smaller than ±2**63 */
	char buf[32];
	if(read(infofd, buf, 5) != 5 || strcmp(buf, "pos:")) {
		rc = 1;
		goto cleanup;
	}
	
	ssize_t len = read(infofd, buf, sizeof(buf)-1);
	buf[len] = 0;

	res->pos = atoll(buf);
	res->pid  = pid;
	res->size = max_size;
	res->num  = atoi(max_size_fd);

cleanup:
	if(infofd > 0)
		close(infofd);
	if(fddir)
		closedir(fddir);
	if(fdfd > 0)
		close(fdfd);

	return 1;
}

void print_bar(float ratio, int width) {
	width -= 2; /* '[', ']' */
	putchar('[');
	int pos = 0;
	for(; pos<(width*ratio)-1; pos++)
		putchar('=');
	putchar('>');
	for(pos++; pos<width; pos++)
		putchar(' ');
	putchar(']');
}

int is_numeric(char *str) {
	while(isdigit(*str++))
		if(!*str)
			return 0;
	return 1;
}

int inlist(char *str, char **list) {
	for(; *list; list++)
		if(!strcmp(str, *list))
			return 1;
	return 0;
}

void parse_options(int argc, char *argv[])
{
	static struct option long_options[] = {
		{"version",    no_argument,       0, 'v'},
		{"quiet",      no_argument,       0, 'q'},
		{"wait",       no_argument,       0, 'w'},
		{"wait-delay", required_argument, 0, 'W'},
		{"help",       no_argument,       0, 'h'},
		{"command",    required_argument, 0, 'c'},
		{0, 0, 0, 0}
	};

	static char *options_string = "vqwhc:W:";
	int c,i;
	int option_index = 0;

	while(1) {
		c = getopt_long (argc, argv, options_string, long_options, &option_index);

		// no more options
		if (c == -1)
			break;

		switch(c) {
			case 'v':
				printf("cv version %s\n",CV_VERSION);
				exit(EXIT_SUCCESS);
				break;

			case 'h':
				printf("cv - Coreutils Viewer\n");
				printf("---------------------\n");
				printf("Shows running coreutils basic commands and displays stats.\n");
				printf("Supported commands: ");
				for(i = 0 ; proc_names[i] ; i++)
					printf("%s ", proc_names[i]);
				printf("\n");
				printf("Usage: %s [-vqwh] [-W] [-c command]\n",argv[0]);
				printf("  -v --version          show version\n");
				printf("  -q --quiet            hides some warning/error messages\n");
				printf("  -w --wait             estimate I/O throughput (slower display)\n");
				printf("  -W --wait-delay secs  wait 'secs' seconds for I/O estimation (implies -w, default=%.1f)\n", throughput_wait_secs);
				printf("  -d --daemonize        Daemonize and show results using libnotify (implies -w)\n");
				printf("  -h --help             this message\n");
				printf("  -c --command cmd      monitor only this command name (ex: firefox)\n");

				exit(EXIT_SUCCESS);
				break;

			case 'q':
				flag_quiet = 1;
				break;

			case 'c':
				proc_specific = strdup(optarg);
				break;

			case 'w':
				flag_throughput = 1;
				break;

			case 'W':
				flag_throughput = 1;
				throughput_wait_secs = atof(optarg);
				break;

			case 'd':
				flag_daemon = 1;

			case '?':
			default:
				exit(EXIT_FAILURE);
		}
	}

	if (optind < argc) {
		fprintf(stderr,"Invalid arguments.\n");
		exit(EXIT_FAILURE);
	}

}

// TODO: deal with --help

int main(int argc, char *argv[])
{
	char fsize[64];
	char fpos[64];
	char ftroughput[64];
	float perc;
	fdinfo_t results[MAX_RESULTS];

	parse_options(argc,argv);

	DIR* proc = opendir("/proc");
	if(!proc) {
		fprintf(stderr, "Can't open /proc\n");
		exit(1);
	}
	int procfd = open("/proc");
	if(!proc) {
		fprintf(stderr, "Can't open /proc\n");
		exit(1);
	}

	char *proc_specific_list[] = {proc_specific, NULL};
	char **name_list = proc_specific ? proc_names : proc_specific_list;

	do {
		size_t result_count = 0;
		int thisfd = 0;
		struct dirent *procent;
		while(procent = readdir(proc) && result_count < MAX_RESULTS) {
			if(thisfd)
				close(thisfd);

			if(procent->d_type != DT_DIR || !is_numeric(procent->d_name))
				continue;

			thisfd = openat(procfd, procent->d_name, O_SEARCH);
			if(!thisfd) // Will be mostly "Permission denied"
				continue;

			char path_buf[MAXPATHLEN];
			ssize_t len = readlinkat(thisfd, "exe", path_buf, sizeof(path_buf));
			if(len < 0) // Will be mostly "Permission denied"
				continue;
			path_buf[len] = 0;

			if(!inlist(basename(exe), name_list))
				continue;

			pid_t pid = atol(procent->d_name);
			if(!biggest_file_for_pid(pid, results+result_count)){
				if(!flag_quiet) {
					fprintf(stderr, "Found no large open files for %s [%5d]\n", pid, name);
				continue;
			}

			//FIXME
			result_count++;
		}
		if(thisfd)
			close(thisfd);

		// wait a bit, so we can estimate the throughput
		if(flag_throughput)
			usleep(1000000 * throughput_wait_secs);

		for(size_t i=0; i<result_count; i++) {

			if (flag_throughput) {
				still_there = get_fdinfo(results[i].pid.pid, results[i].fd.num, &fdinfo);
				if (still_there && strcmp(results[i].fd.name, fdinfo.name))
					still_there = 0; // still there, but it's not the same file !
			} else
				still_there = 0;

			if (!still_there) {
				// pid is no more here (or no throughput was asked), use initial info
				format_size(results[i].fd.pos, fpos);
				format_size(results[i].fd.size, fsize);
				perc = ((double)100 / (double)results[i].fd.size) * (double)results[i].fd.pos;
			} else {
				// use the newest info
				format_size(fdinfo.pos, fpos);
				format_size(fdinfo.size, fsize);
				perc = ((double)100 / (double)fdinfo.size) * (double)fdinfo.pos;

			}

			printf("[%5d] %s %s %.1f%% (%s / %s)",
					results[i].pid.pid,
					results[i].pid.name,
					results[i].fd.name,
					perc,
					fpos,
					fsize);

			if (flag_throughput && still_there) {
				// results[i] vs fdinfo
				long long usec_diff;
				off_t byte_diff;
				off_t bytes_per_sec;

				usec_diff =   (fdinfo.tv.tv_sec  - results[i].fd.tv.tv_sec) * 1000000L
					+ (fdinfo.tv.tv_usec - results[i].fd.tv.tv_usec);
				byte_diff = fdinfo.pos - results[i].fd.pos;
				bytes_per_sec = byte_diff / (usec_diff / 1000000.0);

				format_size(bytes_per_sec, ftroughput);
				printf(" %s/s", ftroughput);
			}


			printf("\n");
		}
	}while(flag_daemon || flag_throughput);

cleanup:
	closedir(proc);
	return 0;
}
