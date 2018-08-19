
#ifndef REDUCTION_H
#define REDUCTION_H

extern int num_reducefn; // should be seen across multiple c files
extern char *list_reducefn[]; // should be seen across multiple c files

uint32_t reduction_sum(int len, uint32_t *buf);

uint32_t reduction_sos(int len, uint32_t *buf);

uint32_t reduction_max(int len, uint32_t *buf);

uint32_t reduction_min(int len, uint32_t *buf);

void reduction_sort(int len, uint32_t *buf);

uint32_t reduction_handler(int reduce_fn, int reduce_size, uint32_t *data, int flag);

uint32_t *reduction_sort_handler(int reduce_size, uint32_t *data);

#endif
