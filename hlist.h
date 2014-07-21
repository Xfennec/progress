#ifndef CV_HLIST_H
#define CV_HLIST_H

typedef struct hlist {
  struct hlist *prev;
  struct hlist *next;
  int value;
} hlist;

void set_hlist_size(double throughput_wait_secs);
int add_to_hlist(hlist **begin, hlist **end, int size, int value);
void free_hlist(hlist *begin);
int get_hlist_average(hlist *begin, int size);

#endif
