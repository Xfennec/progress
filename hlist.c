#include <stdio.h>
#include <stdlib.h>
#include <math.h>

#include "hlist.h"

static int max_hlist_size = 2;

void set_hlist_size(double throughput_wait_secs) {
int new_size;

new_size = ceil(10.0 / throughput_wait_secs);
if (new_size > 1)
    max_hlist_size = new_size;
}

int add_to_hlist(hlist **begin, hlist **end, int size, int value) {
int ret;

if (*begin == NULL) {
    if ((*begin = malloc(sizeof(hlist))) == NULL)
        return 0;
    *end = *begin;
    (*begin)->next = NULL;
    (*begin)->prev = NULL;
    ret = 1;
}
else if (size == max_hlist_size) {
    hlist *tmp = (*end)->prev;
    tmp->next = NULL;
    (*end)->next = *begin;
    (*end)->prev = NULL;
    *begin = *end;
    *end = tmp;
    (*begin)->next->prev = *begin;
    ret = 0;
}
else {
    hlist *new = malloc(sizeof(hlist));
    if (!new)
        return 0;
    new->next = *begin;
    new->prev = NULL;
    *begin = new;
    (*begin)->next->prev = *begin;
    ret = 1;
}
(*begin)->value = value;
return ret;
}

void free_hlist(hlist *begin) {
hlist *tmp = begin;

while (begin) {
    tmp = begin->next;
    free(begin);
    begin = tmp;
}
}

int get_hlist_average(hlist *begin, int size) {
unsigned long long avg = 0;

while (begin) {
    avg += begin->value;
    begin = begin->next;
}
return avg / size;
}
