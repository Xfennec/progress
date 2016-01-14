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

#ifndef PROGRESS_HLIST_H
#define PROGRESS_HLIST_H

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
