/*
   Copyright (C) 2014 Xfennec, CQFD Corp.

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
#include <signal.h>
#include <stdarg.h>
#include <curses.h>

#include <getopt.h>

#include <sys/ioctl.h>

// for the BLKGETSIZE64 code section
#include <fcntl.h>
#include <sys/ioctl.h>
#include <linux/fs.h>

#include "cv.h"
#include "sizes.h"
#include "hlist.h"

char *proc_names[] = {"cp", "mv", "dd", "tar", "gzip", "gunzip", "cat",
    "grep", "fgrep", "egrep", "cut", "sort", "xz", "md5sum", "sha1sum",
    "sha224sum", "sha256sum", "sha384sum", "sha512sum", NULL
};
char *proc_specifiq = NULL;
WINDOW *mainwin;
signed char flag_quiet = 0;
signed char flag_debug = 0;
signed char flag_throughput = 0;
signed char flag_monitor = 0;
signed char flag_monitor_continous = 0;
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

void nprintf(char *format, ...)
{
va_list args;

va_start(args, format);
if (flag_monitor || flag_monitor_continous)
    vw_printw(mainwin, format, args);
else
    vprintf(format, args);
va_end(args);
}

void nfprintf(FILE *file, char *format, ...) {
va_list args;

va_start(args, format);
if (flag_monitor || flag_monitor_continous)
    vw_printw(mainwin, format, args);
else
    vfprintf(file, format, args);
va_end(args);
}

void nperror(const char *s) {
if (flag_monitor || flag_monitor_continous)
    printw("%s:%s", s, strerror(errno));
else
    perror(s);
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
int pid_count=0;

proc=opendir(PROC_PATH);
if(!proc) {
    nperror("opendir");
    nfprintf(stderr,"Can't open %s\n",PROC_PATH);
    exit(EXIT_FAILURE);
}

while((direntp = readdir(proc)) != NULL) {
    snprintf(fullpath_dir, MAXPATHLEN, "%s/%s", PROC_PATH, direntp->d_name);

    if(stat(fullpath_dir, &stat_buf) == -1) {
        if (flag_debug)
            nperror("stat (find_pids_by_binary_name)");
        continue;
    }

    if((S_ISDIR(stat_buf.st_mode) && is_numeric(direntp->d_name))) {
        snprintf(fullpath_exe, MAXPATHLEN, "%s/exe", fullpath_dir);
        len=readlink(fullpath_exe, exe, MAXPATHLEN);
        if(len != -1)
            exe[len] = 0;
        else {
            // Will be mostly "Permission denied"
            //~ nperror("readlink");
            continue;
        }

        if(!strcmp(basename(exe), bin_name)) {
            pid_list[pid_count].pid=atol(direntp->d_name);
            strcpy(pid_list[pid_count].name, bin_name);
            pid_count++;
            if(pid_count==max_pids)
                break;
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
    nperror("opendir");
    nfprintf(stderr,"Can't open %s\n",path_dir);
    return 0;
}

while((direntp = readdir(proc)) != NULL) {
    snprintf(fullpath, MAXPATHLEN, "%s/%s", path_dir, direntp->d_name);
    if(stat(fullpath, &stat_buf) == -1) {
        if (flag_debug)
            nperror("stat (find_fd_for_pid)");
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
    //~ nperror("readlink");
    return 0;
}

if(stat(fd_info->name, &stat_buf) == -1) {
    //~ printf("[debug] %i - %s\n",pid,fd_info->name);
    if (flag_debug)
        nperror("stat (get_fdinfo)");
    return 0;
}

if(S_ISBLK(stat_buf.st_mode)) {
    int fd;

    fd = open(fd_info->name, O_RDONLY);

    if (fd < 0) {
        if (flag_debug)
            nperror("open (get_fdinfo)");
        return 0;
    }

    if (ioctl(fd, BLKGETSIZE64, &fd_info->size) < 0) {
        if (flag_debug)
            nperror("ioctl (get_fdinfo)");
        close(fd);
        return 0;
    }

    close(fd);
} else {
    fd_info->size = stat_buf.st_size;
}

fd_info->pos = 0;

snprintf(fdpath, MAXPATHLEN, "%s/%d/fdinfo/%d", PROC_PATH, pid, fdnum);
fp = fopen(fdpath, "rt");
gettimeofday(&fd_info->tv, &tz);

if(!fp) {
    if (flag_debug)
        nperror("fopen (get_fdinfo)");
    return 0;
}

while(fgets(line, LINE_LEN - 1, fp) != NULL) {
    line[4]=0;
    if(!strcmp(line, "pos:")) {
        fd_info->pos = atoll(line + 5);
        break;
    }
}

fclose(fp);
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
    {"version",           no_argument,       0, 'v'},
    {"quiet",             no_argument,       0, 'q'},
    {"debug",             no_argument,       0, 'd'},
    {"wait",              no_argument,       0, 'w'},
    {"wait-delay",        required_argument, 0, 'W'},
    {"monitor",           no_argument,       0, 'm'},
    {"monitor-continous", no_argument,       0, 'M'},
    {"help",              no_argument,       0, 'h'},
    {"command",           required_argument, 0, 'c'},
    {0, 0, 0, 0}
};

static char *options_string = "vqdwmMhc:W:";
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
            printf("Usage: %s [-vqwmMh] [-W] [-c command]\n",argv[0]);
            printf("  -v --version            show version\n");
            printf("  -q --quiet              hides all messages\n");
            printf("  -d --debug              shows all warning/error messages\n");
            printf("  -w --wait               estimate I/O throughput and ETA (slower display)\n");
            printf("  -W --wait-delay secs    wait 'secs' seconds for I/O estimation (implies -w, default=%.1f)\n", throughput_wait_secs);
            printf("  -m --monitor            loop while monitored processes are still running\n");
            printf("  -M --monitor-continous  like monitor but never stop (similar to watch %s)\n", argv[0]);
            printf("  -h --help               this message\n");
            printf("  -c --command cmd        monitor only this command name (ex: firefox)\n");

            exit(EXIT_SUCCESS);
            break;

        case 'q':
            flag_quiet = 1;
            break;

        case 'd':
            flag_debug = 1;
            break;

        case 'c':
            proc_specifiq = strdup(optarg);
            break;

        case 'w':
            flag_throughput = 1;
            break;

        case 'm':
            flag_monitor = 1;
            break;

        case 'M':
            flag_monitor_continous = 1;
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

nprintf(" eta ");
if (p->tm_yday)
    nprintf("%d day%s, ", p->tm_yday, p->tm_yday > 1 ? "s" : "");
nprintf("%d:%02d:%02d", p->tm_hour, p->tm_min, p->tm_sec);
}

void copy_and_clean_results(result_t *results, int result_count, char copy) {
static result_t old_results[MAX_RESULTS];
static int old_result_count = 0;

if(copy) {
    int i;
    for (i = 0; i < old_result_count; ++i) {
        int j;
        char found = 0;
        for (j = 0; j < result_count; ++j) {
            if (results[j].pid.pid == old_results[i].pid.pid) {
                found = 1;
                results[j].hbegin = old_results[i].hbegin;
                results[j].hend = old_results[i].hend;
                results[j].hsize = old_results[i].hsize;
                break;
            }
        }
        if (!found)
            free_hlist(old_results[i].hbegin);
    }
}
else {
    memcpy(old_results, results, sizeof(old_results));
    old_result_count = result_count;
  }
}

int monitor_processes(int *nb_pid) {
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
float perc;
result_t results[MAX_RESULTS];
signed char still_there;

pid_count = 0;

if(!proc_specifiq) {
    for(i = 0 ; proc_names[i] ; i++) {
        pid_count += find_pids_by_binary_name(proc_names[i],
                                              pidinfo_list + pid_count,
                                              MAX_PIDS - pid_count);
        if(pid_count >= MAX_PIDS) {
            nfprintf(stderr, "Found too much procs (max = %d)\n",MAX_PIDS);
            break;
        }
    }
} else {
    pid_count += find_pids_by_binary_name(proc_specifiq,
                                          pidinfo_list + pid_count,
                                          MAX_PIDS - pid_count);
}

*nb_pid = pid_count;

if(!pid_count) {
    if(flag_quiet)
        return 0;
    if (flag_monitor || flag_monitor_continous) {
        clear();
	refresh();
    }
    nfprintf(stderr,"No command currently running: ");
    for(i = 0 ; proc_names[i] ; i++) {
        nfprintf(stderr,"%s, ", proc_names[i]);
    }
    nfprintf(stderr,"exiting.\n");
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
        nprintf("[%5d] %s inactive/flushing/streaming/...\n",
                pidinfo_list[i].pid,
                pidinfo_list[i].name);
        continue;
    }

    // We've our biggest_fd now, let's store the result
    results[result_count].pid = pidinfo_list[i];
    results[result_count].fd = biggest_fd;
    results[result_count].hbegin = NULL;
    results[result_count].hend = NULL;
    results[result_count].hsize = 0;

    result_count++;
}

// wait a bit, so we can estimate the throughput
if (flag_throughput)
    usleep(1000000 * throughput_wait_secs);
if (flag_monitor || flag_monitor_continous) {
    clear();
    refresh();
}
copy_and_clean_results(results, result_count, 1);
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

    nprintf("[%5d] %s %s %.1f%% (%s / %s)",
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
        results[i].hsize += add_to_hlist(&results[i].hbegin, &results[i].hend, results[i].hsize, byte_diff / (usec_diff / 1000000.0));
        bytes_per_sec = get_hlist_average(results[i].hbegin, results[i].hsize);

        format_size(bytes_per_sec, ftroughput);
        nprintf(" %s/s", ftroughput);
        if (bytes_per_sec && fdinfo.size - fdinfo.pos > 0) {
            print_eta((fdinfo.size - fdinfo.pos) / bytes_per_sec);
        }
    }


    nprintf("\n");

    // Need to work on window width when using screen/watch/...
    //~ printf("    [");
    //~ print_bar(perc, ws.ws_col-6);
    //~ printf("]\n");
}
copy_and_clean_results(results, result_count, 0);
return 0;
}

void int_handler(int sig) {
  if(flag_monitor || flag_monitor_continous)
    endwin();
  exit(0);
}


int main(int argc, char *argv[])
{
pid_t nb_pid;
struct winsize ws;

parse_options(argc,argv);

// ws.ws_row, ws.ws_col
ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws);
if(flag_monitor || flag_monitor_continous) {
    if((mainwin = initscr()) == NULL ) {
        fprintf(stderr, "Error initialising ncurses.\n");
        exit(EXIT_FAILURE);
    }
    if (!flag_throughput) {
      flag_throughput = 1;
      throughput_wait_secs = 1;
    }
    set_hlist_size(throughput_wait_secs);
    signal(SIGINT, int_handler);
    do {
        monitor_processes(&nb_pid);
	refresh();
	if(flag_monitor_continous && !nb_pid) {
	  usleep(1000000 * throughput_wait_secs);
	}
    } while ((flag_monitor && nb_pid) || flag_monitor_continous);
    endwin();
}
else {
    set_hlist_size(throughput_wait_secs);
    monitor_processes(&nb_pid);
}
return 0;
}
