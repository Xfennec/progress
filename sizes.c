#include <ctype.h>

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

long long htob(char *src) {
    uint64_t multiplier=1;
    int i;
    char *p=src;

    while (*p && !isalpha(*p)) p++;
    if (!*p) return atoll(src);

    for (i=DIM(sizes)-1; i >= 0; i--) {
        if (toupper(*p) == sizes[i][0]) {
            return multiplier*atoll(src);
        }
        multiplier = 1024*multiplier;
    }

    return atoll(src);
}
