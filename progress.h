/*
   Copyright (C) 2015 Xfennec, CQFD Corp.

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

#ifndef PROGRESS_MAIN_H
#define PROGRESS_MAIN_H

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/param.h>
#include <sys/time.h>

#include "hlist.h"

#define PROGRESS_VERSION         "0.9"

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
