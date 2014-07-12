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
#include <time.h>

#include <getopt.h>

#include <sys/ioctl.h>

// for the BLKGETSIZE64 code section
#include <fcntl.h>
#include <sys/ioctl.h>
#include <linux/fs.h>

#include "cv.h"
#include "sizes.h"

char *proc_names[] = {"cp", "mv", "dd", "tar", "gzip", "gunzip", "cat", "grep", "fgrep", "egrep", "cut", "sort", NULL};
char *proc_specifiq = NULL;
signed char flag_quiet = 0;
signed char flag_throughput = 0;
double throughput_wait_secs = 1;

signed char is_numeric(char *str)
{
while(*str) {
    if(!isdigit(*str))
        return 0;
    str++;
}
return 1;
}

int find_pids_by_binary_name(char *bin_name, pidinfo_t *pid_list, int max_pids)
{
DIR *proc;
struct dirent *direntp;
struct stat stat_buf;
char fullpath_dir[MAXPATHLEN + 1];
char fullpath_exe[MAXPATHLEN + 1];
char exe[MAXPATHLEN + 1];
ssize_t len;
int pid_count=0, cmdlfd=-1;

proc=opendir(PROC_PATH);
if(!proc) {
    perror("opendir");
    fprintf(stderr,"Can't open %s\n",PROC_PATH);
    exit(EXIT_FAILURE);
}

while((direntp = readdir(proc)) != NULL) {
    snprintf(fullpath_dir, MAXPATHLEN, "%s/%s", PROC_PATH, direntp->d_name);

    if(stat(fullpath_dir, &stat_buf) == -1) {
        if (!flag_quiet)
            perror("stat (find_pids_by_binary_name)");
        continue;
    }

    if((S_ISDIR(stat_buf.st_mode) && is_numeric(direntp->d_name))) {
        snprintf(fullpath_exe, MAXPATHLEN, "%s/exe", fullpath_dir);
        len=readlink(fullpath_exe, exe, MAXPATHLEN);
        if(len != -1)
            exe[len] = 0;
        else {
            // Will be mostly "Permission denied"
            //~ perror("readlink");
            continue;
        }

        if(!strcmp(basename(exe), bin_name)) {
            pid_list[pid_count].pid=atol(direntp->d_name);
            strcpy(pid_list[pid_count].name, bin_name);
            pid_count++;
            if(pid_count==max_pids)
                break;
            continue;
        }

        // In case the binary name is different then $0
        snprintf(fullpath_exe, MAXPATHLEN, "%s/cmdline", fullpath_dir);
        cmdlfd = open(fullpath_exe, O_RDONLY);
        if (cmdlfd < 0) {
            // Will be mostly "Permission denied"
            //~ perror("open");
        } else {
            len = read(cmdlfd, exe, MAXPATHLEN);
            if (len < 0) {
                perror("read");
                close(cmdlfd);
            } else {
                exe[len]=0;
                close(cmdlfd);

                if (!strcmp(basename(exe), bin_name)) {
                    pid_list[pid_count].pid=atol(direntp->d_name);
                    strcpy(pid_list[pid_count].name, bin_name);
                    pid_count++;
                    if(pid_count==max_pids)
                        break;
                    continue;
                }
            }
        }
    }
}

closedir(proc);
return pid_count;
}

int find_fd_for_pid(pid_t pid, int *fd_list, int max_fd)
{
DIR *proc;
struct dirent *direntp;
char path_dir[MAXPATHLEN + 1];
char fullpath[MAXPATHLEN + 1];
char link_dest[MAXPATHLEN + 1];
struct stat stat_buf;
int count = 0;
ssize_t len;

snprintf(path_dir, MAXPATHLEN, "%s/%d/fd", PROC_PATH, pid);

proc=opendir(path_dir);
if(!proc) {
    perror("opendir");
    fprintf(stderr,"Can't open %s\n",path_dir);
    return 0;
}

while((direntp = readdir(proc)) != NULL) {
    snprintf(fullpath, MAXPATHLEN, "%s/%s", path_dir, direntp->d_name);
    if(stat(fullpath, &stat_buf) == -1) {
        if (!flag_quiet)
            perror("stat (find_fd_for_pid)");
        continue;
    }

    // if not a regular file or a block device
    if(!S_ISREG(stat_buf.st_mode) && !S_ISBLK(stat_buf.st_mode))
        continue;

    // try to read link ...
    len=readlink(fullpath, link_dest, MAXPATHLEN);
    if(len != -1)
        link_dest[len] = 0;
    else
        continue;

    // try to stat link target (invalid link ?)
    if(stat(link_dest, &stat_buf) == -1)
        continue;

    // OK, we've found a potential interesting file.

    fd_list[count++] = atoi(direntp->d_name);
    //~ printf("[debug] %s\n",fullpath);
    if(count == max_fd)
        break;
}

closedir(proc);
return count;
}


signed char get_fdinfo(pid_t pid, int fdnum, fdinfo_t *fd_info)
{
struct stat stat_buf;
char fdpath[MAXPATHLEN + 1];
char line[LINE_LEN];
ssize_t len;
FILE *fp;
struct timezone tz;

fd_info->num = fdnum;

snprintf(fdpath, MAXPATHLEN, "%s/%d/fd/%d", PROC_PATH, pid, fdnum);

len=readlink(fdpath, fd_info->name, MAXPATHLEN);
if(len != -1)
    fd_info->name[len] = 0;
else {
    //~ perror("readlink");
    return 0;
}

if(stat(fd_info->name, &stat_buf) == -1) {
    //~ printf("[debug] %i - %s\n",pid,fd_info->name);
    if (!flag_quiet)
        perror("stat (get_fdinfo)");
    return 0;
}

if(S_ISBLK(stat_buf.st_mode)) {
    int fd;

    fd = open(fd_info->name, O_RDONLY);

    if (fd < 0) {
        if (!flag_quiet)
            perror("open (get_fdinfo)");
        return 0;
    }

    if (ioctl(fd, BLKGETSIZE64, &fd_info->size) < 0) {
        if (!flag_quiet)
            perror("ioctl (get_fdinfo)");
        return 0;
    }
} else {
    fd_info->size = stat_buf.st_size;
}

fd_info->pos = 0;

snprintf(fdpath, MAXPATHLEN, "%s/%d/fdinfo/%d", PROC_PATH, pid, fdnum);
fp = fopen(fdpath, "rt");
gettimeofday(&fd_info->tv, &tz);

if(!fp) {
    if (!flag_quiet)
        perror("fopen (get_fdinfo)");
    return 0;
}

while(fgets(line, LINE_LEN - 1, fp) != NULL) {
    line[4]=0;
    if(!strcmp(line, "pos:")) {
        fd_info->pos = atoll(line + 5);
        break;
    }
}

return 1;
}

void print_bar(float perc, int char_available)
{
int i;
int num;

num = (char_available / 100.0) * perc;

for(i = 0 ; i < num-1 ; i++) {
    putchar('=');
}
putchar('>');
i++;

for( ; i < char_available ; i++)
    putchar(' ');

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
            printf("  -w --wait             estimate I/O throughput and ETA (slower display)\n");
            printf("  -W --wait-delay secs  wait 'secs' seconds for I/O estimation (implies -w, default=%.1f)\n", throughput_wait_secs);
            printf("  -h --help             this message\n");
            printf("  -c --command cmd      monitor only these commands name (ex: firefox,wget)\n");

            exit(EXIT_SUCCESS);
            break;

        case 'q':
            flag_quiet = 1;
            break;

        case 'c':
            proc_specifiq = strdup(optarg);
            break;

        case 'w':
            flag_throughput = 1;
            break;

        case 'W':
            flag_throughput = 1;
            throughput_wait_secs = atof(optarg);
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

void print_eta(time_t seconds)
{
struct tm *p = gmtime(&seconds);

printf(" eta ");
if (p->tm_yday)
    printf("%d day%s, ", p->tm_yday, p->tm_yday > 1 ? "s" : "");
printf("%d:%02d:%02d", p->tm_hour, p->tm_min, p->tm_sec);
}

// TODO: deal with --help

int main(int argc, char *argv[])
{
int pid_count, fd_count, result_count;
int i,j;
pidinfo_t pidinfo_list[MAX_PIDS];
fdinfo_t fdinfo;
fdinfo_t biggest_fd;
int fdnum_list[MAX_FD_PER_PID];
off_t max_size;
char fsize[64];
char fpos[64];
char ftroughput[64];
struct winsize ws;
float perc;
result_t results[MAX_RESULTS];
signed char still_there;
char *pnext=NULL;

parse_options(argc,argv);

// ws.ws_row, ws.ws_col
ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws);

pid_count = 0;

if(!proc_specifiq) {
    for(i = 0 ; proc_names[i] ; i++) {
        pid_count += find_pids_by_binary_name(proc_names[i],
                                              pidinfo_list + pid_count,
                                              MAX_PIDS - pid_count);
        if(pid_count >= MAX_PIDS) {
            fprintf(stderr, "Found too much procs (max = %d)\n",MAX_PIDS);
            break;
        }
    }
} else {
    //split on comma.
    while (proc_specifiq) {
        pnext = strchr(proc_specifiq, ',');
        if (pnext) *pnext = 0;
        pid_count += find_pids_by_binary_name(proc_specifiq,
                                              pidinfo_list + pid_count,
                                              MAX_PIDS - pid_count);
        if (!pnext) break;
        proc_specifiq = pnext+1;

        if(pid_count >= MAX_PIDS) {
            fprintf(stderr, "Found too much procs (max = %d)\n",MAX_PIDS);
            break;
        }
    }
}


if(!pid_count) {
    if(flag_quiet)
        return 0;

    fprintf(stderr,"No command currently running: ");
    for(i = 0 ; proc_names[i] ; i++) {
        fprintf(stderr,"%s, ", proc_names[i]);
    }
    fprintf(stderr,"exiting.\n");
    return 0;
}

result_count = 0;

for(i = 0 ; i < pid_count ; i++) {
    fd_count = find_fd_for_pid(pidinfo_list[i].pid, fdnum_list, MAX_FD_PER_PID);

    max_size = 0;

    // let's find the biggest opened file
    for(j = 0 ; j < fd_count ; j++) {
        get_fdinfo(pidinfo_list[i].pid, fdnum_list[j], &fdinfo);

        if(fdinfo.size > max_size) {
            biggest_fd = fdinfo;
            max_size = fdinfo.size;
        }
    }

    if(!max_size) { // nothing found
        printf("[%5d] %s inactive/flushing/streaming/...\n",
                pidinfo_list[i].pid,
                pidinfo_list[i].name);
        continue;
    }

    // We've our biggest_fd now, let's store the result
    results[result_count].pid = pidinfo_list[i];
    results[result_count].fd = biggest_fd;

    result_count++;
}

// wait a bit, so we can estimate the throughput
if (flag_throughput)
    usleep(1000000 * throughput_wait_secs);

for (i = 0 ; i < result_count ; i++) {

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
        if (bytes_per_sec && fdinfo.size - fdinfo.pos > 0) {
            print_eta((fdinfo.size - fdinfo.pos) / bytes_per_sec);
        }
    }


    printf("\n");

    // Need to work on window width when using screen/watch/...
    //~ printf("    [");
    //~ print_bar(perc, ws.ws_col-6);
    //~ printf("]\n");

}

return 0;
}
