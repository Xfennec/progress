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

#include "sizes.h"

// Thanks to Jonathan Leffler for this code

#define DIM(x) (sizeof(x)/sizeof(*(x)))

static const char     *sizes[]   = { "EiB", "PiB", "TiB", "GiB", "MiB", "KiB", "B" };
static const uint64_t  exbibytes = 1024ULL * 1024ULL * 1024ULL *
                                   1024ULL * 1024ULL * 1024ULL;

void format_size(uint64_t size, char *result)
{
uint64_t multiplier;
int i;

multiplier = exbibytes;

for (i = 0 ; i < DIM(sizes) ; i++, multiplier /= 1024) {
    if (size < multiplier)
        continue;
    if (size % multiplier == 0)
        sprintf(result, "%" PRIu64 " %s", size / multiplier, sizes[i]);
    else
        sprintf(result, "%.1f %s", (float) size / multiplier, sizes[i]);
    return;
}

strcpy(result, "0");
return;
}
