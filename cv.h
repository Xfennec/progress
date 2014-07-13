#ifndef _MAIN_H
#define _MAIN_H

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/param.h>
#include <sys/time.h>

#ifdef BUILD_DAEMON
#include <libnotify/notify.h>
#endif

#define CV_VERSION         "0.3"

#define MAX_RESULTS     32
//~ #define MINMUM_SIZE     8192

typedef struct fdinfo_t {
    int pid;
    int num;
    off_t size;
    off_t pos;
    char *filename;
    char *procname;
    ino_t inode;
    struct timeval tv;
#ifdef BUILD_DAEMON
    NotifyNotification *notification;
#endif
} fdinfo_t;

#endif
