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
#include <fnmatch.h>
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

char *proc_names[] = {"cp", "mv", "dd", "tar", "gzip", "gunzip", "cat", "grep", "fgrep", "egrep", "cut", "sort"};
char *proc_specifiq = NULL;
char *dir_filter = NULL;
signed char flag_quiet = 0, flag_verbose = 0, flag_glob = 0, flag_full = 0,
            flag_icase = 0;
signed char flag_throughput = 0;
double throughput_wait_secs = 1;
long long size_filter = -1;

int find_fd_for_pid(pid_t pid, int *fd_list, int max_fd, char **dir_names, int num_dirs, long long min_size);

signed char is_numeric(char *str)
{
while(*str) {
    if(!isdigit(*str))
        return 0;
    str++;
}
return 1;
}

int find_pids_by_binary_name(char **bin_names, int num_names, 
                             char **dir_names, int num_dirs,
                             pidinfo_t *pid_list, int max_pids, long long min_size)
{
DIR *proc;
struct dirent *direntp;
struct stat stat_buf;
char fullpath_dir[MAXPATHLEN + 1];
char fullpath_exe[MAXPATHLEN + 1];
char exe[MAXPATHLEN + 1];
ssize_t len;
int pid_count=0, cmdlfd=-1, res=-1, cur_name=0;
char *pnext=NULL, *bin_name=NULL;

proc=opendir(PROC_PATH);
if(!proc) {
    perror("opendir");
    fprintf(stderr,"Can't open %s\n",PROC_PATH);
    exit(EXIT_FAILURE);
}

while((direntp = readdir(proc)) != NULL) {
    for (cur_name=0; cur_name < num_names; cur_name++) {
        bin_name=bin_names[cur_name];
        snprintf(fullpath_dir, MAXPATHLEN, "%s/%s", PROC_PATH, direntp->d_name);

        if(stat(fullpath_dir, &stat_buf) == -1) {
            if (!flag_quiet)
                fprintf(stderr, "stat (find_pids_by_binary_name): %s: %s\n", strerror(errno), fullpath_dir);
            continue;
        }

        if((S_ISDIR(stat_buf.st_mode) && is_numeric(direntp->d_name))) {
            snprintf(fullpath_exe, MAXPATHLEN, "%s/exe", fullpath_dir);
            len=readlink(fullpath_exe, exe, MAXPATHLEN);
            if(len != -1)
                exe[len] = 0;
            else {
                if (flag_verbose)
                    // Will be mostly "Permission denied"
                    fprintf(stderr, "readlink: %s: %s\n", strerror(errno), fullpath_exe);
                continue;
            }

            if (flag_icase)
                for (pnext=exe; pnext<exe+len-1; pnext++)
                    *pnext=tolower(*pnext);

            res=-1;
            if (flag_glob)
                res = fnmatch(bin_name, basename(exe), FNM_PATHNAME|FNM_PERIOD);
            else
                res = strcmp(basename(exe), bin_name);
            if(res==0) {
                pid_list[pid_count].pid=atol(direntp->d_name);
                strcpy(pid_list[pid_count].name, basename(exe));

                if ((num_dirs <= 0 && min_size <= 0) || find_fd_for_pid(pid_list[pid_count].pid, NULL, 0, dir_names, num_dirs, min_size) > 0)
                    pid_count++;

                if(pid_count==max_pids) goto leave;
                continue;
            }

            // In case the binary name is different then $0
            snprintf(fullpath_exe, MAXPATHLEN, "%s/cmdline", fullpath_dir);
            cmdlfd = open(fullpath_exe, O_RDONLY);
            if (cmdlfd < 0) {
                // Will be mostly "Permission denied"
                if (flag_verbose)
                    fprintf(stderr, "open: %s: %s\n", strerror(errno), fullpath_exe);
            } else {
                len = read(cmdlfd, exe, MAXPATHLEN);
                if (len < 0) {
                    fprintf(stderr, "read: %s: %s\n", strerror(errno), fullpath_exe);
                    close(cmdlfd);
                } else {
                    exe[len]=0;
                    close(cmdlfd);

                    if (flag_icase)
                        for (pnext=exe; pnext<exe+len-1; pnext++)
                            *pnext=tolower(*pnext);

                    if (flag_full) {
                        // cmdline is null seperated, convert to spaces for
                        // fnmatch.
                        pnext = exe;
                        while (pnext && pnext < exe+len-1) {
                            pnext = strchr(pnext, '\0');
                            if (pnext) *pnext = ' ';
                        }
                        pnext = exe;
                    } else {
                        pnext = basename(exe);
                    }

                    res=-1;
                    if (flag_glob)
                        res = fnmatch(bin_name, pnext, FNM_PERIOD);
                    else
                        res = strcmp(pnext, bin_name);
                    if(res==0) {
                        pid_list[pid_count].pid=atol(direntp->d_name);

                        if (flag_full) {
                            // re-null terminate so basename doesn't get
                            // confused.
                            pnext = strchr(exe, ' ');
                            if (pnext) *pnext = 0;
                        }
                        strcpy(pid_list[pid_count].name, basename(exe));

                        if ((num_dirs <= 0 && min_size <= 0) || find_fd_for_pid(pid_list[pid_count].pid, NULL, 0, dir_names, num_dirs, min_size) > 0)
                            pid_count++;

                        if(pid_count==max_pids) goto leave;
                        continue;
                    }
                }
            }
        }
    }
}

leave:
closedir(proc);
return pid_count;
}

int find_fd_for_pid(pid_t pid, int *fd_list, int max_fd, char **dir_names, int num_dirs, long long min_size)
{
DIR *proc;
struct dirent *direntp;
char path_dir[MAXPATHLEN + 1];
char fullpath[MAXPATHLEN + 1];
char link_dest[MAXPATHLEN + 1];
struct stat stat_buf;
int count = 0, i = 0;
ssize_t len;

snprintf(path_dir, MAXPATHLEN, "%s/%d/fd", PROC_PATH, pid);

proc=opendir(path_dir);
if(!proc) {
    fprintf(stderr, "opendir: %s: %s\n", strerror(errno), path_dir);
    return 0;
}

while((direntp = readdir(proc)) != NULL) {
    snprintf(fullpath, MAXPATHLEN, "%s/%s", path_dir, direntp->d_name);
    if(stat(fullpath, &stat_buf) == -1) {
        if (!flag_quiet)
            fprintf(stderr, "stat (find_fd_for_pid): %s: %s\n", strerror(errno), fullpath);
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

    if (min_size > 0 && stat_buf.st_size < min_size) continue;

    // OK, we've found a potential interesting file.
    if (num_dirs == 0) {
        if (fd_list) fd_list[count] = atoi(direntp->d_name);
        count++;
    } else {
        for (i=0; i < num_dirs; i++) {
            if (realpath(dir_names[i], fullpath) == NULL) {
                if (flag_verbose)
                    fprintf(stderr, "realpath: %s: %s\n", strerror(errno), dir_names[i]);
                continue;
            }
            if (strstr(link_dest, fullpath) == link_dest) {
                if (fd_list) {
                    fd_list[count] = atoi(direntp->d_name);
                    if(count == max_fd) break;
                }
                count++;
            }
        }
    }
    //~ printf("[debug] %s\n",link_dest);
    if(fd_list && count == max_fd)
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
    if (flag_verbose)
        fprintf(stderr, "readlink: %s: %s\n", strerror(errno), fdpath);
    return 0;
}

if(stat(fd_info->name, &stat_buf) == -1) {
    //~ printf("[debug] %i - %s\n",pid,fd_info->name);
    if (!flag_quiet)
        fprintf(stderr, "stat (get_fdinfo): %s: %s\n", strerror(errno), fd_info->name);
    return 0;
}

if(S_ISBLK(stat_buf.st_mode)) {
    int fd;

    fd = open(fd_info->name, O_RDONLY);

    if (fd < 0) {
        if (!flag_quiet)
            fprintf(stderr, "open (get_fdinfo): %s: %s\n", strerror(errno), fd_info->name);
        return 0;
    }

    if (ioctl(fd, BLKGETSIZE64, &fd_info->size) < 0) {
        if (!flag_quiet)
            fprintf(stderr, "ioctl (get_fdinfo): %s: %s\n", strerror(errno), fd_info->name);
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
        fprintf(stderr, "fopen (get_fdinfo): %s: %s\n", strerror(errno), fdpath);
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
    {"verbose",    no_argument,       0, 'V'},
    {"wait",       no_argument,       0, 'w'},
    {"wait-delay", required_argument, 0, 'W'},
    {"help",       no_argument,       0, 'h'},
    {"command",    required_argument, 0, 'c'},
    {"glob",       no_argument,       0, 'g'},
    {"full",       no_argument,       0, 'f'},
    {"case-insensitively",no_argument,0, 'i'},
    {"directory",  required_argument, 0, 'D'},
    {"size",       required_argument, 0, 'S'},
    {0, 0, 0, 0}
};

static char *options_string = "vqVwhc:W:gfiD:S:";
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
            for(i = 0 ; i < sizeof(proc_names)/sizeof(proc_names[0]); i++)
                printf("%s ", proc_names[i]);
            printf("\n");
            printf("Usage: %s [-vqVwhgfi] [-W] [-c command] [-D dir] [-S bytes]\n",argv[0]);
            printf("  -v --version          show version\n");
            printf("  -q --quiet            hides some warning/error messages\n");
            printf("  -V --verbose          print non-exceptional errors (eg permission denied)\n");
            printf("  -w --wait             estimate I/O throughput and ETA (slower display)\n");
            printf("  -W --wait-delay secs  wait 'secs' seconds for I/O estimation (implies -w, default=%.1f)\n", throughput_wait_secs);
            printf("  -h --help             this message\n");
            printf("  -c --command cmd      monitor only these commands name (ex: firefox,wget)\n");
            printf("  -g --glob             use fnmatch() when matching process names instead of strcmp()\n");
            printf("  -f --full             match against the entire command line instead of just the command name, implies --glob\n");
            printf("                        Note that this is an exact match, use -g and \\*pattern\\* to get a substring match\n");
            printf("  -i --case-insensitive match case insensitively\n");
            printf("  -D --directory dir    filter results to processes handling files in dir, comma separated\n");
            printf("  -S --size bytes       filter results to processes handling files larger than bytes\n");

            exit(EXIT_SUCCESS);
            break;

        case 'q':
            flag_quiet = 1;
            break;

        case 'V':
            flag_verbose = 1;
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

        case 'g':
            flag_glob = 1;
            break;

        case 'f':
            flag_full = 1;
            // Since we are doing an exact match --full isn't useful without
            // --glob so imply it for now. Note that this doesn't enable
            // substring match, the user still needs to wrap their search
            // string in asterisks.
            flag_glob = 1;
            break;

        case 'i':
            flag_icase = 1;
            break;

        case 'D':
            dir_filter = optarg;
            break;

        case 'S':
            size_filter = atoi(optarg);
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

// Split on sep, put pointers int dest.
// Modifies src by replacing sep with '\0', up to len_dest occurrences only.
int split(char *dest[], int len_dest, char *src, char sep) {
    int i=0;
    char *pnext = src;
    if (pnext);
    dest[0] = pnext;
    i++;

    if (flag_icase) {
        for (;*pnext; pnext++) *pnext=tolower(*pnext);
        pnext = dest[0];
    }

    while ((pnext = strchr(pnext, sep))) {
        *pnext = 0;
        if (i==len_dest) break;
        dest[i++] = ++pnext;
    }
    return i;
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
char *specifiq_batch[MAX_PIDS];
signed char still_there;
char *dir_names[MAX_PIDS];
int num_dirs=0;

parse_options(argc,argv);

// ws.ws_row, ws.ws_col
ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws);

pid_count = 0;

if (dir_filter) {
    num_dirs = split(dir_names, sizeof(dir_names)/sizeof(dir_names[0]),
                     dir_filter, ',');
}

if(!proc_specifiq) {
    pid_count = find_pids_by_binary_name(proc_names,
                                          sizeof(proc_names)/sizeof(proc_names[0]),
                                          dir_names, num_dirs,
                                          pidinfo_list,
                                          MAX_PIDS, size_filter);
    if(pid_count >= MAX_PIDS) {
        fprintf(stderr, "Found too much procs (max = %d)\n",MAX_PIDS);
    }
} else {
    i = split(specifiq_batch, sizeof(specifiq_batch)/sizeof(specifiq_batch[0]),
              proc_specifiq, ',');

    pid_count = find_pids_by_binary_name(specifiq_batch, i,
                                          dir_names, num_dirs,
                                          pidinfo_list,
                                          MAX_PIDS, size_filter);

    if(pid_count >= MAX_PIDS) {
        fprintf(stderr, "Found too much procs (max = %d)\n",MAX_PIDS);
    }
}


if(!pid_count) {
    if(flag_quiet)
        return 0;

    fprintf(stderr,"No command currently running: ");
    if (proc_specifiq) {
        for (j=0; j < i; j++) {
            fprintf(stderr,"%s, ", specifiq_batch[j]);
        }
    } else {
        for(i = 0 ; i < sizeof(proc_names)/sizeof(proc_names[0]); i++) {
            fprintf(stderr,"%s, ", proc_names[i]);
        }
    }
    fprintf(stderr,"exiting.\n");
    return 0;
}

result_count = 0;

for(i = 0 ; i < pid_count ; i++) {
    fd_count = find_fd_for_pid(pidinfo_list[i].pid, fdnum_list, MAX_FD_PER_PID, dir_names, num_dirs, size_filter);

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
