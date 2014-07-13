#include "sizes.h"

// Thanks to Jonathan Leffler for this code

#define DIM(x) (sizeof(x)/sizeof(*(x)))

static const char *sizes[] = {"B", "kiB",  "MiB", "GiB", "TiB", "PiB", "EiB", NULL};

void format_size(uint64_t size, char *result, size_t result_len) {
	const char **str = sizes;
	uint64_t omult, mult = 1;
	do {
		omult = mult;
		mult <<= 10;
		if(size < mult){
			snprintf(result, result_len, "%.1f%s", (float)size/omult, *str);
			return;
		}
	} while(*++str);
	snprintf(result, result_len, "%.1f%s", (float)size/omult, str[-1]);
}
