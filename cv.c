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

/* for O_PATH */
#define __USE_GNU
/* for the BLKGETSIZE64 code section */
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
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
double throughput_wait_secs = 1;
int flag_daemon = 0;

ssize_t size_for_stat(struct stat *fd_target, int fdfd, char *name){
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
	int fd = openat(entryfd, name, O_RDONLY | O_DIRECTORY);
	if(fd < 0) /* Likely permission denied */
		return 1;
	*fdout = fd;

	DIR *adir = fdopendir(fd);
	if(!adir)
		return 2;
	*dirout = adir;
	return 0;
}

int biggest_file_for_entry(int entryfd, fdinfo_t *res){
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
	char max_size_info[sizeof("fdinfo") + sizeof("2147483648")];
	strcpy(max_size_info, "fdinfo/");
	ino_t  max_size_inode = 0;

	struct dirent *fdent;
	while((fdent = readdir(fddir))){
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
		max_size_inode = fd_target.st_ino;
	}

	infofd = openat(entryfd, max_size_info, O_RDONLY);
	if(infofd < 0){
		rc = 1;
		goto cleanup;
	}

	/* According to seq_show in fs/proc/fd.c in the linux kernel sources, the fdinfo file will always start with the
	 * "pos" line. The pos field is printfed as a long long, i.e. 64 bit and thus never larger/smaller than ±2**63 */
	char buf[32];
	if((read(infofd, buf, 5) != 5) || strncmp(buf, "pos:", 4)) {
		rc = 1;
		goto cleanup;
	}
	
	ssize_t len = read(infofd, buf, sizeof(buf)-1);
	buf[len] = 0;

	res->pos  = atoll(buf);
	res->size = max_size;
	res->inode= max_size_inode;
	res->num  = atoi(max_size_info+sizeof("fdinfo"));
	gettimeofday(&res->tv, NULL);

	char *fbuf = malloc(MAXPATHLEN);
	if(!fbuf) {
		rc = 2;
		goto cleanup;
	}

	if(readlinkat(fdfd, max_size_info+sizeof("fdinfo"), fbuf, MAXPATHLEN) < 0) {
		free(fbuf);
		rc = 1;
		goto cleanup;
	}
	res->filename = fbuf;

cleanup:
	if(infofd > 0)
		close(infofd);
	if(fddir)
		closedir(fddir);
	if(fdfd > 0)
		close(fdfd);

	return rc;
}

int is_numeric(char *str) {
	while(isdigit(*str++))
		if(!*str)
			return 1;
	return 0;
}

int inlist(char *str, char **list) {
	while(*list)
		if(!strcmp(str, *list++))
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
#ifdef BUILD_DAEMON
		{"daemonize",  no_argument,       0, 'd'},
#endif
		{"help",       no_argument,       0, 'h'},
		{"command",    required_argument, 0, 'c'},
		{0, 0, 0, 0}
	};

#ifdef BUILD_DAEMON
	static char *options_string = "vqwdhc:W:";
#else
	static char *options_string = "vqwhc:W:";
#endif
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
#ifdef BUILD_DAEMON
				printf("  -d --daemonize        Daemonize and show results using libnotify (implies -w)\n");
#endif
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
				break;
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

int compare_results(const void *a, const void *b){
	return ((fdinfo_t*)b)->pid - ((fdinfo_t*)a)->pid;
}

int main(int argc, char *argv[]) {
	int rc = 0;
	fdinfo_t results1[MAX_RESULTS];
	fdinfo_t results2[MAX_RESULTS];
	fdinfo_t *new_results = results1;
	fdinfo_t *old_results = results2;
	size_t old_result_count = 0;

	parse_options(argc,argv);

#ifdef BUILD_DAEMON
	notify_init("cv");
#endif

	int procfd = 0;
	DIR* procdir = NULL;
	if(diropen(AT_FDCWD, "/proc", &procfd, &procdir)) {
		fprintf(stderr, "Can't open /proc: %s (%d)\n", strerror(errno), errno);
		rc = 1;
		goto cleanup;
	}

	char *proc_specific_list[] = {proc_specific, NULL};
	char **name_list = proc_specific ? proc_specific_list : proc_names;

	while(1) {
		/* Search /proc */
		size_t new_result_count = 0;
		int thisfd = 0;
		struct dirent *procent;
		while((procent = readdir(procdir)) && new_result_count < MAX_RESULTS) {
			if(thisfd)
				close(thisfd);

			if(procent->d_type != DT_DIR || !is_numeric(procent->d_name))
				continue;

			thisfd = openat(procfd, procent->d_name, O_RDONLY | O_DIRECTORY);
			if(thisfd < 0) /* Will be mostly "Permission denied" */
				continue;

			char path_buf[MAXPATHLEN];
			ssize_t len = readlinkat(thisfd, "exe", path_buf, sizeof(path_buf));
			if(len < 0) /* Will be mostly "Permission denied" */
				continue;
			path_buf[len] = 0;

			if(!inlist(basename(path_buf), name_list))
				continue;

			fdinfo_t *result = new_results+new_result_count;
			result->pid = atol(procent->d_name);
			result->procname = strdup(basename(path_buf));
			if(biggest_file_for_entry(thisfd, new_results+new_result_count)) {
				if(!flag_quiet)
					fprintf(stderr, "Found no large open files for %s [%5d]\n", result->procname, result->pid);
				continue;
			}

			new_result_count++;
		}
		if(thisfd)
			close(thisfd);
		rewinddir(procdir);
		
		/* Depending on the order in which the kernel returns the entries of /proc, this might not be necessary. */
		qsort(new_results, sizeof(new_results)/sizeof(fdinfo_t), sizeof(fdinfo_t), compare_results);

		/* Print results, merging old and new lists on the way */
		fdinfo_t *old_result = old_results;
		fdinfo_t *new_result = new_results;
#ifdef BUILD_DAEMON
		if(new_result_count == 0) {
			while(old_result < old_results+old_result_count){ /* process terminated */
				notify_notification_close(old_result->notification, NULL);
				g_object_unref(G_OBJECT(old_result->notification));
				old_result++;
			}
		}
#endif
		while(new_result < new_results+new_result_count) {
			off_t throughput = -1;
#ifdef BUILD_DAEMON
			new_result->notification = NULL;
#endif
			if(old_result < old_results+old_result_count) {
				while(new_result->pid > old_result->pid) { /* process terminated */
#ifdef BUILD_DAEMON
					notify_notification_close(old_result->notification, NULL);
					g_object_unref(G_OBJECT(old_result->notification));
#endif
					old_result++;
				}

				if(new_result->pid == old_result->pid) {
					if(new_result->inode == old_result->inode){
						uint64_t time_delta = (new_result->tv.tv_sec  - old_result->tv.tv_sec)
							+ (new_result->tv.tv_usec - old_result->tv.tv_usec)/1000000L;
						throughput = (new_result->pos - old_result->pos)/time_delta;
#ifdef BUILD_DAEMON
						new_result->notification = old_result->notification;
#endif
					}

					if(old_result < old_results+(old_result_count-1))
						old_result++;
				}
			}

			char fsize[32];
			char fpos[32];
			format_size(new_result->pos, fpos, sizeof(fpos));
			format_size(new_result->size, fsize, sizeof(fsize));
			float ratio = (float)new_result->pos/new_result->size;

			char strbuf[MAXPATHLEN];
			int pos = 0;
			pos += snprintf(strbuf, sizeof(strbuf)-pos, "[%5d] %s %s %.1f%% (%s/%s",
					new_result->pid,
					new_result->procname,
					new_result->filename,
					ratio*100,
					fpos,
					fsize);

			free(new_result->filename);

			char fthroughput[32];
			if(throughput < 0) {
				pos += snprintf(strbuf+pos, sizeof(strbuf)-pos, ")");
			} else {
				format_size(throughput, fthroughput, sizeof(fthroughput));
				pos += snprintf(strbuf+pos, sizeof(strbuf)-pos, " @ %s/s)", fthroughput);
			}

			if(!flag_daemon) {
				printf("%s\n", strbuf);
#ifdef BUILD_DAEMON
			} else {
				NotifyNotification *notf = new_result->notification;
				if(!notf) {
					notf = notify_notification_new("cv", strbuf, NULL);
					notify_notification_set_hint(notf, "synchronous", g_variant_new_string("volume"));
				}else{
					notify_notification_update(notf, "cv", strbuf, NULL);
				}
				notify_notification_set_hint(notf, "value", g_variant_new_int32(ratio*100));
				notify_notification_show(notf, NULL);
				new_result->notification = notf;
#endif
			}

			new_result++;
		}

		/* Exchange pointers */
		fdinfo_t *tmpr = new_results;
		new_results = old_results;
		old_results = tmpr;
		old_result_count = new_result_count;

		if(!flag_daemon && !flag_throughput)
			break;
		flag_throughput = !flag_throughput;
		usleep(1000000 * throughput_wait_secs);
	}

cleanup:
#ifdef BUILD_DAEMON
	notify_uninit();
#endif
	if(procfd > 0)
		close(procfd);
	if(procdir)
		closedir(procdir);
	return rc;
}
