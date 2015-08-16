#ifndef PROGRESS_SIZES_H
#define PROGRESS_SIZES_H

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define DIM(x) (sizeof(x)/sizeof(*(x)))

void format_size(uint64_t size, char *result);

#endif
