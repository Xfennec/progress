#ifndef CV_MAIN_H
#define CV_MAIN_H

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/param.h>
#include <sys/time.h>

#include "hlist.h"

#define CV_VERSION         "0.5"

#define PROC_PATH       "/proc"
#define MAX_PIDS        32
#define MAX_RESULTS     32
#define MAX_FD_PER_PID  512
#define LINE_LEN        256
//~ #define MINMUM_SIZE     8192

typedef struct fdinfo_t {
    int num;
    off_t size;
    off_t pos;
    char name[MAXPATHLEN + 1];
    struct timeval tv;
} fdinfo_t;

typedef struct pidinfo_t {
    pid_t pid;
    char name[MAXPATHLEN + 1];
} pidinfo_t;

typedef struct result_t {
    pidinfo_t pid;
    fdinfo_t fd;
    hlist *hbegin;
    hlist *hend;
    int hsize;
} result_t;

#endif
