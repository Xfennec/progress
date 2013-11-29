#ifndef _MAIN_H
#define _MAIN_H

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/param.h>

#define CV_VERSION         "0.2"

#define PROC_PATH       "/proc"
#define MAX_PIDS        32
#define MAX_FD_PER_PID  512
#define LINE_LEN        256
//~ #define MINMUM_SIZE     8192

typedef struct fdinfo_t {
    off_t size;
    off_t pos;
    char name[MAXPATHLEN + 1];
} fdinfo_t;

typedef struct pidinfo_t {
    pid_t pid;
    char name[MAXPATHLEN + 1];
} pidinfo_t;


#endif
