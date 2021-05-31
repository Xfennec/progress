/*
   Copyright (C) 2016 Xfennec, CQFD Corp.

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
#include <assert.h>
#include <curses.h>
#include <locale.h>

#include <wordexp.h>
#include <getopt.h>

#include <sys/ioctl.h>

// for the BLKGETSIZE64 code section
#include <fcntl.h>
#include <sys/ioctl.h>
#ifdef __APPLE__
# include <unistd.h>
# include <sys/proc_info.h>
# include <libproc.h>
# include <sys/disk.h>
#endif // __APPLE__
#ifdef __linux__
# include <linux/fs.h>
#endif // __linux__
#ifdef __FreeBSD__
# include <sys/disk.h>
#endif // __FreeBSD__

#ifdef __FreeBSD__
# include <sys/param.h>
# include <sys/queue.h>
# include <sys/socket.h>
# include <sys/sysctl.h>
# include <sys/user.h>
# include <libprocstat.h>
#endif

#include "progress.h"
#include "sizes.h"
#include "hlist.h"

// Given -a will dynamically add values to this list, move it to be a dynamic
// list and generate it at runtime.
static int proc_names_cnt;
static char **proc_names;
char *default_proc_names[] = {"cp", "mv", "dd", "tar", "bsdtar", "cat", "rsync", "scp",
    "grep", "fgrep", "egrep", "cut", "sort", "md5sum", "sha1sum",
    "sha224sum", "sha256sum", "sha384sum", "sha512sum",
#ifdef __FreeBSD__
    "md5", "sha1", "sha224", "sha256", "sha512", "sha512t256", "rmd160",
    "skein256", "skein512", "skein1024",
#endif
    "adb",
    "gzip", "gunzip", "bzip2", "bunzip2", "xz", "unxz", "lzma", "unlzma", "7z", "7za", "zip", "unzip",
    "zcat", "bzcat", "lzcat",
    "coreutils",
    "split",
    "gpg",
#if defined(__APPLE__) || defined(__FreeBSD__)
    "gcp", "gmv", "gdd", "gnutar", "gcat", "gcut", "gsort", "gmd5sum",
    "gsha1sum", "gsha224sum", "gssha256sum", "gsha384sum", "gsha512sum",
#endif
    NULL
};

// static means initialized to 0/NULL (C standard, §6.7.8/10)
static int proc_specifiq_name_cnt;
static char **proc_specifiq_name;
static int ignore_file_list_cnt;
static char **ignore_file_list;
static int proc_specifiq_pid_cnt;
static pid_t *proc_specifiq_pid;

signed char flag_quiet = 0;
signed char flag_debug = 0;
signed char flag_throughput = 0;
signed char flag_monitor = 0;
signed char flag_monitor_continuous = 0;
signed char flag_open_mode = 0;
double throughput_wait_secs = 1;

WINDOW *mainwin;

signed char is_numeric(char *str)
{
while (*str) {
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
if (flag_monitor || flag_monitor_continuous)
    vw_printw(mainwin, format, args);
else
    vprintf(format, args);
va_end(args);
}

void nfprintf(FILE *file, char *format, ...) {
va_list args;

va_start(args, format);
if (flag_monitor || flag_monitor_continuous)
    vw_printw(mainwin, format, args);
else
    vfprintf(file, format, args);
va_end(args);
}

void nperror(const char *s) {
if (flag_monitor || flag_monitor_continuous)
    printw("%s:%s", s, strerror(errno));
else
    perror(s);
}


signed char is_ignored_file(char *str)
{
int i;
for (i = 0 ; i < ignore_file_list_cnt ; i++)
    if (!strcmp(ignore_file_list[i], str))
        return 1;
return 0;
}

#ifdef __APPLE__
int find_pid_by_id(pid_t pid, pidinfo_t *pid_list)
{
char exe[MAXPATHLEN + 1];

exe[0] = '\0';
proc_name(pid, exe, sizeof(exe));
if (exe[0] == '\0')
    return 0;

pid_list[0].pid = pid;
strcpy(pid_list[0].name, exe);
return 1;
}

int find_pids_by_binary_name(char *bin_name, pidinfo_t *pid_list, int max_pids)
{
int pid_count=0;
int nb_processes = proc_listpids(PROC_ALL_PIDS, 0, NULL, 0);
char exe[1024];
pid_t *pids;
int i;

pids = malloc(nb_processes * sizeof(pid_t));
assert(pids != NULL);

proc_listpids(PROC_ALL_PIDS, 0, pids, nb_processes);
for(i = 0; i < nb_processes; ++i) {
    if (pids[i] == 0) {
        continue;
    }
    proc_name(pids[i], exe, sizeof(exe));
    if(!strcmp(exe, bin_name)) {
        pid_list[pid_count].pid=pids[i];
        strcpy(pid_list[pid_count].name, bin_name);
        pid_count++;
        if(pid_count==max_pids)
            break;
    }
}
free(pids);
return pid_count;
}
#endif // __APPLE__
#ifdef __linux__
int find_pid_by_id(pid_t pid, pidinfo_t *pid_list)
{
char fullpath_exe[MAXPATHLEN + 1];
char exe[MAXPATHLEN + 1];
ssize_t len;

snprintf(fullpath_exe, MAXPATHLEN, "%s/%i/exe", PROC_PATH, pid);

len=readlink(fullpath_exe, exe, MAXPATHLEN);
if (len != -1)
    exe[len] = 0;
else
    return 0;

pid_list[0].pid = pid;
strcpy(pid_list[0].name, basename(exe));
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
int pid_count=0;
int res;

proc=opendir(PROC_PATH);
if (!proc) {
    nperror("opendir");
    nfprintf(stderr,"Can't open %s\n",PROC_PATH);
    exit(EXIT_FAILURE);
}

while ((direntp = readdir(proc)) != NULL) {
    snprintf(fullpath_dir, MAXPATHLEN, "%s/%s", PROC_PATH, direntp->d_name);

    if (stat(fullpath_dir, &stat_buf) == -1) {
        if (flag_debug)
            nperror("stat (find_pids_by_binary_name)");
        continue;
    }

    if ((S_ISDIR(stat_buf.st_mode) && is_numeric(direntp->d_name))) {
        res = snprintf(fullpath_exe, MAXPATHLEN, "%s/exe", fullpath_dir);
        if (res < 0) {
            fprintf(stderr, "path is too long: %s\n", fullpath_dir);
            exit(EXIT_FAILURE);
        }
        len=readlink(fullpath_exe, exe, MAXPATHLEN);
        if (len != -1)
            exe[len] = 0;
        else {
            // Will be mostly "Permission denied"
            //~ nperror("readlink");
            continue;
        }

        if (!strcmp(basename(exe), bin_name)) {
            pid_list[pid_count].pid = atol(direntp->d_name);
            strcpy(pid_list[pid_count].name, bin_name);
            pid_count++;
            if(pid_count == max_pids)
                break;
        }
    }
}

closedir(proc);
return pid_count;
}
#endif // __linux__
#ifdef __FreeBSD__
int find_pid_by_id(pid_t pid, pidinfo_t *pid_list)
{
struct procstat *procstat;
struct kinfo_proc *procs;
unsigned int proc_count;

struct kinfo_proc *proc;
char pathname[PATH_MAX];

int i, found = 0;

procstat = procstat_open_sysctl();
assert(procstat != NULL);

procs = procstat_getprocs(procstat, KERN_PROC_PID, pid, &proc_count);
if (procs == NULL)
    goto done;

for (i = 0; i < proc_count; i++) {
    proc = &procs[i];
    procstat_getpathname(procstat, proc, pathname, sizeof(pathname));
    if (strlen(pathname) == 0)
        // kernel thread I guess?
        continue;

    pid_list[0].pid = pid;
    strcpy(pid_list[0].name, basename(pathname));
    found = 1;
    break;
}

procstat_freeprocs(procstat, procs);
done:
procstat_close(procstat);

return found;
}

int find_pids_by_binary_name(char *bin_name, pidinfo_t *pid_list, int max_pids)
{
struct procstat *procstat;
struct kinfo_proc *procs;
unsigned int proc_count;

struct kinfo_proc *proc;
char pathname[PATH_MAX];

int pid_count = 0;
int i;

procstat = procstat_open_sysctl();
assert(procstat != NULL);

procs = procstat_getprocs(procstat, KERN_PROC_PROC, 0, &proc_count);
if (procs == NULL)
    goto done;

for (i = 0; i < proc_count; i++) {
    proc = &procs[i];
    procstat_getpathname(procstat, proc, pathname, sizeof(pathname));
    if (strlen(pathname) == 0)
        // kernel thread I guess? see proc->ki_comm instead
        continue;

    if (!strcmp(basename(pathname), bin_name)) {
        pid_list[pid_count].pid = proc->ki_pid;
        strcpy(pid_list[pid_count].name, bin_name);
        pid_count++;
        if (pid_count == max_pids)
            break;
    }
}

procstat_freeprocs(procstat, procs);
done:
procstat_close(procstat);

return pid_count;
}
#endif // __FreeBSD__

#ifdef __APPLE__
int find_fd_for_pid(pid_t pid, int *fd_list, int max_fd)
{
int count = 0;
int bufferSize = proc_pidinfo(pid, PROC_PIDLISTFDS, 0, 0, 0);
struct stat stat_buf;

if (bufferSize < 0) {
    printf("Error :/, cannot proc_pidinfo\n");
    return 0;
}
struct proc_fdinfo *procFDInfo = (struct proc_fdinfo *)malloc(bufferSize);
assert(procFDInfo != NULL);
proc_pidinfo(pid, PROC_PIDLISTFDS, 0, procFDInfo, bufferSize);
int numberOfProcFDs = bufferSize / PROC_PIDLISTFD_SIZE;
int i;

for(i = 0; i < numberOfProcFDs; i++) {
    if(procFDInfo[i].proc_fdtype == PROX_FDTYPE_VNODE) {
        struct vnode_fdinfowithpath vnodeInfo;
        proc_pidfdinfo(pid, procFDInfo[i].proc_fd, PROC_PIDFDVNODEPATHINFO, &vnodeInfo, PROC_PIDFDVNODEPATHINFO_SIZE);
        if (stat(vnodeInfo.pvip.vip_path, &stat_buf) < 0) {
            if (flag_debug)
                perror("sstat");
            continue;
        }
        if (!S_ISREG(stat_buf.st_mode) && !S_ISBLK(stat_buf.st_mode))
            continue;

        if (is_ignored_file(vnodeInfo.pvip.vip_path))
            continue;

        // OK, we've found a potential interesting file.

        fd_list[count++] = procFDInfo[i].proc_fd;
        //~ printf("[debug] %s\n",vnodeInfo.pvip.vip_path);
        if(count == max_fd)
            break;
    }
}
return count;
}
#endif // __APPLE__
#ifdef __linux__
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
int res;

snprintf(path_dir, MAXPATHLEN, "%s/%d/fd", PROC_PATH, pid);

proc = opendir(path_dir);
if (!proc) {
    nperror("opendir");
    nfprintf(stderr,"Can't open %s\n",path_dir);
    return 0;
}

while ((direntp = readdir(proc)) != NULL) {
    res = snprintf(fullpath, MAXPATHLEN, "%s/%s", path_dir, direntp->d_name);
    if (res < 0) {
        fprintf(stderr, "path is too long: %s/%s\n", path_dir, direntp->d_name);
        exit(EXIT_FAILURE);
    }
    if (stat(fullpath, &stat_buf) == -1) {
        if (flag_debug)
            nperror("stat (find_fd_for_pid)");
        continue;
    }

    // if not a regular file or a block device
    if(!S_ISREG(stat_buf.st_mode) && !S_ISBLK(stat_buf.st_mode))
        continue;

    // try to read link ...
    len = readlink(fullpath, link_dest, MAXPATHLEN);
    if (len != -1)
        link_dest[len] = 0;
    else
        continue;

    // try to stat link target (invalid link ?)
    if (stat(link_dest, &stat_buf) == -1)
        continue;

    if (is_ignored_file(fullpath) || is_ignored_file(link_dest))
        continue;

    // OK, we've found a potential interesting file.

    fd_list[count++] = atoi(direntp->d_name);
    //~ printf("[debug] %s\n",fullpath);
    if (count == max_fd)
        break;
}

closedir(proc);
return count;
}
#endif // __linux__
#ifdef __FreeBSD__
int find_fd_for_pid(pid_t pid, int *fd_list, int max_fd)
{
struct procstat *procstat;
struct kinfo_proc *procs;
unsigned int proc_count;

struct kinfo_proc *proc;
struct filestat *fstat;
struct filestat_list *fstat_list;

int count = 0;
int i;

procstat = procstat_open_sysctl();
assert(procstat != NULL);

procs = procstat_getprocs(procstat, KERN_PROC_PID, pid, &proc_count);
if (procs == NULL)
    goto done;

for (i = 0; i < proc_count; i++) {
    proc = &procs[i];

    fstat_list = procstat_getfiles(procstat, proc, 0);
    if (fstat_list == NULL)
        continue;
    STAILQ_FOREACH(fstat, fstat_list, next) {
        if (fstat->fs_type != PS_FST_TYPE_VNODE)
            continue;
        if (fstat->fs_fd < 0) // usually non-zero fs_uflags: PS_FST_UFLAG_{TEXT,CTTY,...}
            continue;

        struct vnstat vn;
        char errbuf[_POSIX2_LINE_MAX];

        if (procstat_get_vnode_info(procstat, fstat, &vn, errbuf) != 0)
            // see errbuf
            continue;

        if (!(vn.vn_type == PS_FST_VTYPE_VREG || vn.vn_type == PS_FST_VTYPE_VBLK))
            continue;

        if (is_ignored_file(fstat->fs_path))
            continue;

        // OK, we've found a potential interesting file.
        fd_list[count++] = fstat->fs_fd;
        // fstat->fs_offset is looked up once again in get_fdinfo()

        if (count == max_fd)
            break;
    }

    procstat_freefiles(procstat, fstat_list);
}

procstat_freeprocs(procstat, procs);
done:
procstat_close(procstat);

return count;
}
#endif // __FreeBSD__


signed char get_fdinfo(pid_t pid, int fdnum, fdinfo_t *fd_info)
{
struct stat stat_buf;
#ifdef __linux__
char fdpath[MAXPATHLEN + 1];
char line[LINE_LEN];
FILE *fp;
int flags;
#endif
struct timezone tz;

fd_info->num = fdnum;
fd_info->mode = PM_NONE;

#ifdef __APPLE__
struct vnode_fdinfowithpath vnodeInfo;
if (proc_pidfdinfo(pid, fdnum, PROC_PIDFDVNODEPATHINFO, &vnodeInfo, PROC_PIDFDVNODEPATHINFO_SIZE) <= 0)
    return 0;
strncpy(fd_info->name, vnodeInfo.pvip.vip_path, MAXPATHLEN);
#endif // __APPLE__
#ifdef __linux__
ssize_t len;
snprintf(fdpath, MAXPATHLEN, "%s/%d/fd/%d", PROC_PATH, pid, fdnum);

len=readlink(fdpath, fd_info->name, MAXPATHLEN);
if (len != -1)
    fd_info->name[len] = 0;
else {
    //~ nperror("readlink");
    return 0;
}
#endif
#ifdef __FreeBSD__
struct procstat *procstat;
struct kinfo_proc *procs;
unsigned int proc_count;

struct kinfo_proc *proc;
struct filestat *fstat;
struct filestat_list *fstat_list;

int i;

procstat = procstat_open_sysctl();
assert(procstat != NULL);

procs = procstat_getprocs(procstat, KERN_PROC_PID, pid, &proc_count);
if (procs == NULL)
    goto done;

for (i = 0; i < proc_count; i++) {
    proc = &procs[i];

    fstat_list = procstat_getfiles(procstat, proc, 0);
    if (fstat_list == NULL)
        continue;

    gettimeofday(&fd_info->tv, &tz);

    STAILQ_FOREACH(fstat, fstat_list, next) {
        if (fstat->fs_fd != fdnum)
            continue;

        strncpy(fd_info->name, fstat->fs_path, MAXPATHLEN);

        struct vnstat vn;
        char errbuf[_POSIX2_LINE_MAX];

        if (procstat_get_vnode_info(procstat, fstat, &vn, errbuf) != 0)
            // see errbuf
            continue;

        fd_info->pos = fstat->fs_offset;
        // XXX PS_FST_FFLAG_APPEND?
        fd_info->mode = (
            fstat->fs_fflags & (PS_FST_FFLAG_WRITE | PS_FST_FFLAG_READ) ? PM_READWRITE :
            fstat->fs_fflags & PS_FST_FFLAG_WRITE ? PM_WRITE :
            fstat->fs_fflags & PS_FST_FFLAG_READ ? PM_READ :
            0
        );
    }

    procstat_freefiles(procstat, fstat_list);
}

procstat_freeprocs(procstat, procs);
done:
procstat_close(procstat);
#endif // __FreeBSD__

if (stat(fd_info->name, &stat_buf) == -1) {
    //~ printf("[debug] %i - %s\n",pid,fd_info->name);
    if (flag_debug)
        nperror("stat (get_fdinfo)");
    return 0;
}

if (S_ISBLK(stat_buf.st_mode)) {
    int fd;

    fd = open(fd_info->name, O_RDONLY);

    if (fd < 0) {
        if (flag_debug)
            nperror("open (get_fdinfo)");
        return 0;
    }

#ifdef __APPLE__
    uint64_t bc;
    uint32_t bs;

    bs = 0;
    bc = 0;
    if (ioctl(fd, DKIOCGETBLOCKSIZE, &bs) < 0 ||  ioctl(fd, DKIOCGETBLOCKCOUNT, &bc) < 0) {
        if (flag_debug)
            perror("ioctl (get_fdinfo)");
        return 0;
    }
    fd_info->size = bc*bs;
    printf("Size: %lld\n", fd_info->size);
#endif
#ifdef __FreeBSD__
    if (ioctl(fd, DIOCGMEDIASIZE, &fd_info->size) < 0) {
        if (flag_debug)
            nperror("ioctl (get_fdinfo)");
        close(fd);
        return 0;
    }
#endif
#ifdef __linux__
    if (ioctl(fd, BLKGETSIZE64, &fd_info->size) < 0) {
        if (flag_debug)
            nperror("ioctl (get_fdinfo)");
        close(fd);
        return 0;
    }
#endif
    close(fd);
} else {
    fd_info->size = stat_buf.st_size;
}

#ifdef __APPLE__
fd_info->pos = vnodeInfo.pfi.fi_offset;
gettimeofday(&fd_info->tv, &tz);
if (vnodeInfo.pfi.fi_openflags & FREAD)
    fd_info->mode = PM_READ;
if (vnodeInfo.pfi.fi_openflags & FWRITE)
    fd_info->mode = PM_WRITE;
if (vnodeInfo.pfi.fi_openflags & FREAD && vnodeInfo.pfi.fi_openflags & FWRITE)
    fd_info->mode = PM_READWRITE;
#endif // __APPLE__
#ifdef __linux__
flags = 0;
fd_info->pos = 0;

snprintf(fdpath, MAXPATHLEN, "%s/%d/fdinfo/%d", PROC_PATH, pid, fdnum);
fp = fopen(fdpath, "rt");
gettimeofday(&fd_info->tv, &tz);

if (!fp) {
    if (flag_debug)
        nperror("fopen (get_fdinfo)");
    return 0;
}

while (fgets(line, LINE_LEN - 1, fp) != NULL) {
    if (!strncmp(line, "pos:", 4))
        fd_info->pos = atoll(line + 5);
    if (!strncmp(line, "flags:", 6))
        flags = atoll(line + 7);
}

if ((flags & O_ACCMODE) == O_RDONLY)
    fd_info->mode = PM_READ;
if ((flags & O_ACCMODE) == O_WRONLY)
    fd_info->mode = PM_WRITE;
if ((flags & O_ACCMODE) == O_RDWR)
    fd_info->mode = PM_READWRITE;

fclose(fp);
#endif // __linux__
return 1;
}

void print_bar(float perc, int char_available)
{
int i;
int num;

num = (char_available / 100.0) * perc;

for (i = 0 ; i < num-1 ; i++) {
    putchar('=');
}
putchar('>');
i++;

for ( ; i < char_available ; i++)
    putchar(' ');

}


void parse_options(int argc, char *argv[])
{
static struct option long_options[] = {
    {"version",              no_argument,       0, 'v'},
    {"quiet",                no_argument,       0, 'q'},
    {"debug",                no_argument,       0, 'd'},
    {"wait",                 no_argument,       0, 'w'},
    {"wait-delay",           required_argument, 0, 'W'},
    {"monitor",              no_argument,       0, 'm'},
    {"monitor-continuously", no_argument,       0, 'M'},
    {"help",                 no_argument,       0, 'h'},
    {"additional-command",   required_argument, 0, 'a'},
    {"command",              required_argument, 0, 'c'},
    {"pid",                  required_argument, 0, 'p'},
    {"ignore-file",          required_argument, 0, 'i'},
    {"open-mode",            required_argument, 0, 'o'},
    {0, 0, 0, 0}
};

static char *options_string = "vqdwmMha:c:p:W:i:o:";
int c,i;
int option_index = 0;
char *rp;

optind = 1; // reset getopt

while(1) {
    c = getopt_long (argc, argv, options_string, long_options, &option_index);

    // no more options
    if (c == -1)
        break;

    switch (c) {
        case 'v':
            printf("progress version %s\n", PROGRESS_VERSION);
            exit(EXIT_SUCCESS);
            break;

        case 'h':
            printf("progress - Coreutils Viewer\n");
            printf("---------------------\n");
            printf("Shows progress on file manipulations (cp, mv, dd, ...)\n\n");
            printf("Monitored commands (default, you can add virtually anything):\n");
            for(i = 0 ; i < proc_names_cnt ; i++)
                printf("%s ", proc_names[i]);
            printf("\n\n");
            printf("Usage: %s [-qdwmM] [-W secs] [-c command] [-p pid]\n",argv[0]);
            printf("  -q --quiet                   hides all messages\n");
            printf("  -d --debug                   shows all warning/error messages\n");
            printf("  -w --wait                    estimate I/O throughput and ETA (slower display)\n");
            printf("  -W --wait-delay secs         wait 'secs' seconds for I/O estimation (implies -w, default=%.1f)\n", throughput_wait_secs);
            printf("  -m --monitor                 loop while monitored processes are still running\n");
            printf("  -M --monitor-continuously    like monitor but never stop (similar to watch %s)\n", argv[0]);
            printf("  -a --additional-command cmd  add additional command to default command list\n");
            printf("  -c --command cmd             monitor only this command name (ex: firefox)\n");
            printf("  -p --pid id                  monitor only this process ID (ex: `pidof firefox`)\n");
            printf("  -i --ignore-file file        do not report process if using file\n");
            printf("  -o --open-mode {r|w}         report only files opened for read or write\n");
            printf("  -v --version                 show program version and exit\n");
            printf("  -h --help                    display this help and exit\n");
            printf("\n\n");
            printf("Multiple options allowed for: -a -c -p -i. Use PROGRESS_ARGS for permanent arguments.\n");
            exit(EXIT_SUCCESS);
            break;

        case 'q':
            flag_quiet = 1;
            break;

        case 'd':
            flag_debug = 1;
            break;

        case 'i':
            rp = realpath(optarg, NULL);
            ignore_file_list_cnt++;
            ignore_file_list = realloc(ignore_file_list, ignore_file_list_cnt * sizeof(char *));
            assert(ignore_file_list != NULL);
            if (rp)
                ignore_file_list[ignore_file_list_cnt - 1] = rp;
            else
                ignore_file_list[ignore_file_list_cnt - 1] = strdup(optarg); // file does not exist yet, it seems
            break;

        case 'a':
            proc_names_cnt++;
            proc_names = realloc(proc_names, proc_names_cnt * sizeof(char *));
            assert(proc_names != NULL);
            proc_names[proc_names_cnt - 1] = strdup(optarg);
            break;

        case 'c':
            proc_specifiq_name_cnt++;
            proc_specifiq_name = realloc(proc_specifiq_name, proc_specifiq_name_cnt * sizeof(char *));
            assert(proc_specifiq_name != NULL);
            proc_specifiq_name[proc_specifiq_name_cnt - 1] = strdup(optarg);
            break;

        case 'p':
            proc_specifiq_pid_cnt++;
            proc_specifiq_pid = realloc(proc_specifiq_pid, proc_specifiq_pid_cnt * sizeof(pid_t));
            assert(proc_specifiq_pid != NULL);
            proc_specifiq_pid[proc_specifiq_pid_cnt - 1] = atof(optarg);
            break;

        case 'w':
            flag_throughput = 1;
            break;

        case 'm':
            flag_monitor = 1;
            break;

        case 'M':
            flag_monitor_continuous = 1;
            break;

        case 'W':
            flag_throughput = 1;
            throughput_wait_secs = atof(optarg);
            break;

        case 'o':
            if (!strcmp("r", optarg))
                flag_open_mode = PM_READ;
            else if (!strcmp("w", optarg))
                flag_open_mode = PM_WRITE;
            else {
                fprintf(stderr,"Invalid --open-mode option value '%s'.\n", optarg);
                exit(EXIT_FAILURE);
            }
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
struct tm *p;

if (!seconds)
    return;

p = gmtime(&seconds);

nprintf(" remaining ");
if (p->tm_yday)
    nprintf("%d day%s ", p->tm_yday, p->tm_yday > 1 ? "s" : "");
nprintf("%d:%02d:%02d", p->tm_hour, p->tm_min, p->tm_sec);
}

void copy_and_clean_results(result_t *results, int result_count, char copy)
{
static result_t old_results[MAX_RESULTS];
static int old_result_count = 0;

if (copy) {
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

int monitor_processes(int *nb_pid)
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
float perc;
result_t results[MAX_RESULTS];
signed char still_there;
signed char search_all = 1;
static signed char first_pass = 1;

pid_count = 0;
if (!flag_monitor && !flag_monitor_continuous)
    first_pass = 0;


if (proc_specifiq_name_cnt) {
    search_all = 0;
    for (i = 0 ; i < proc_specifiq_name_cnt ; ++i) {
        pid_count += find_pids_by_binary_name(proc_specifiq_name[i],
                                              pidinfo_list + pid_count,
                                              MAX_PIDS - pid_count);
        if(pid_count >= MAX_PIDS) {
            nfprintf(stderr, "Found too much procs (max = %d)\n",MAX_PIDS);
            return 0;
        }
    }
}

if (proc_specifiq_pid) {
    search_all = 0;
    for (i = 0 ; i < proc_specifiq_pid_cnt ; ++i) {
        pid_count += find_pid_by_id(proc_specifiq_pid[i],
                                    pidinfo_list + pid_count);

        if(pid_count >= MAX_PIDS) {
            nfprintf(stderr, "Found too much procs (max = %d)\n",MAX_PIDS);
            return 0;
        }
    }
}

if (search_all) {
    for (i = 0 ; i < proc_names_cnt ; i++) {
        pid_count += find_pids_by_binary_name(proc_names[i],
                                              pidinfo_list + pid_count,
                                              MAX_PIDS - pid_count);
        if(pid_count >= MAX_PIDS) {
            nfprintf(stderr, "Found too much procs (max = %d)\n",MAX_PIDS);
            return 0;
        }
    }
}

*nb_pid = pid_count;

if (!pid_count) {
    if (flag_quiet)
        return 0;
    if (flag_monitor || flag_monitor_continuous) {
        clear();
	refresh();
    }
    if (proc_specifiq_pid_cnt) {
        nfprintf(stderr, "No such pid: ");
        for (i = 0 ; i < proc_specifiq_pid_cnt; ++i) {
            nfprintf(stderr, "%d, ", proc_specifiq_pid[i]);
        }
    }
    if (proc_specifiq_name_cnt)
    {
        nfprintf(stderr, "No such command(s) running: ");
        for (i = 0 ; i < proc_specifiq_name_cnt; ++i) {
            nfprintf(stderr, "%s, ", proc_specifiq_name[i]);
        }
    }
    if (!proc_specifiq_pid && !proc_specifiq_name_cnt) {
        nfprintf(stderr,"No command currently running: ");
        for (i = 0 ; i < proc_names_cnt ; i++) {
            nfprintf(stderr,"%s, ", proc_names[i]);
        }
    }
    nfprintf(stderr,"or wrong permissions.\n");
    first_pass = 0;
    return 0;
}

result_count = 0;

for (i = 0 ; i < pid_count ; i++) {
    fd_count = find_fd_for_pid(pidinfo_list[i].pid, fdnum_list, MAX_FD_PER_PID);

    max_size = 0;

    // let's find the biggest opened file
    for (j = 0 ; j < fd_count ; j++) {
        get_fdinfo(pidinfo_list[i].pid, fdnum_list[j], &fdinfo);

        if (flag_open_mode == PM_READ && fdinfo.mode != PM_READ && fdinfo.mode != PM_READWRITE)
            continue;
        if (flag_open_mode == PM_WRITE && fdinfo.mode != PM_WRITE && fdinfo.mode != PM_READWRITE)
            continue;

        if (fdinfo.size > max_size) {
            biggest_fd = fdinfo;
            max_size = fdinfo.size;
        }
    }

    if (!max_size) { // nothing found
    // this display is the root of too many confusion for the users, let's
    // remove it. And it does not play well with --i option.
/*        nprintf("[%5d] %s inactive/flushing/streaming/...\n",
                pidinfo_list[i].pid,
                pidinfo_list[i].name);*/
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
if (flag_throughput && !first_pass)
    usleep(1000000 * throughput_wait_secs);
if (flag_monitor || flag_monitor_continuous) {
    clear();
}
copy_and_clean_results(results, result_count, 1);
for (i = 0 ; i < result_count ; i++) {

    if (flag_throughput && !first_pass) {
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

    nprintf("[%5d] %s %s\n\t%.1f%% (%s / %s)",
        results[i].pid.pid,
        results[i].pid.name,
        results[i].fd.name,
        perc,
        fpos,
        fsize);

    if (flag_throughput && still_there && !first_pass) {
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
        if (bytes_per_sec && fdinfo.size - fdinfo.pos >= 0) {
            print_eta((fdinfo.size - fdinfo.pos) / bytes_per_sec);
        }
    }


    nprintf("\n\n");

    // Need to work on window width when using screen/watch/...
    //~ printf("    [");
    //~ print_bar(perc, ws.ws_col-6);
    //~ printf("]\n");
}
if (flag_monitor || flag_monitor_continuous) {
    if (!result_count)
        nprintf("No PID(s) currently monitored\n");
    refresh();
}
copy_and_clean_results(results, result_count, 0);
first_pass = 0;
return 0;
}

void int_handler(int sig)
{
if(flag_monitor || flag_monitor_continuous)
    endwin();
exit(0);
}

// Setup the default commands as a dynamic list
void populate_proc_names() {
    int i;
    for(i = 0 ; default_proc_names[i] ; i++) {
        proc_names_cnt++;
        proc_names = realloc(proc_names, proc_names_cnt * sizeof(char *));
        assert(proc_names != NULL);
        proc_names[proc_names_cnt - 1] = default_proc_names[i];
    }
}

int main(int argc, char *argv[])
{
pid_t nb_pid;
struct winsize ws;
wordexp_t env_wordexp;
char *env_progress_args;
char *env_progress_args_full;

populate_proc_names();

env_progress_args = getenv("PROGRESS_ARGS");

if (env_progress_args) {
    int full_len;

    // prefix with (real) argv[0]
    // argv[0] + ' ' + env_progress_args + '\0'
    full_len = strlen(argv[0]) + 1 + strlen(env_progress_args) + 1;
    env_progress_args_full = malloc(full_len * sizeof(char));
    assert(env_progress_args_full != NULL);
    sprintf(env_progress_args_full, "%s %s", argv[0], env_progress_args);

    if (wordexp(env_progress_args_full, &env_wordexp, 0)) {
        fprintf(stderr,"Unable to parse PROGRESS_ARGS environment variable.\n");
        exit(EXIT_FAILURE);
    }
    parse_options(env_wordexp.we_wordc,env_wordexp.we_wordv);
}
parse_options(argc,argv);

// ws.ws_row, ws.ws_col
ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws);
if (flag_monitor || flag_monitor_continuous) {
    setlocale(LC_CTYPE, "");
    if ((mainwin = initscr()) == NULL ) {
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
        if(flag_monitor_continuous && !nb_pid) {
          usleep(1000000 * throughput_wait_secs);
        }
    } while ((flag_monitor && nb_pid) || flag_monitor_continuous);
    endwin();
}
else {
    set_hlist_size(throughput_wait_secs);
    monitor_processes(&nb_pid);
}
return 0;
}
