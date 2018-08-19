#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "reduction.h"

int num_reducefn = 5;
char *list_reducefn[] = {"sum", "sos", "max", "min", "sort"};

void insertionSort(int len, int *arr) {
    int i, j;
    
    i = 1;
    
    while(i < len) {
        j = i;
        while(j > 0 && arr[j-1] > arr[j]) {
            int temp = arr[j];
            
            arr[j] = arr[j-1];
            arr[j-1] = temp;
            
            j--;
        }
        i++;
    }
}

uint32_t reduction_sum(int len, uint32_t *buf) {
	uint32_t sum = 0;

	int i;
	for(i = 0; i < len; i++) {
		sum += buf[i];
	}
	return sum;
}

uint32_t reduction_sos(int len, uint32_t *buf) {
	uint32_t sum = 0;

	int i;
	for(i = 0; i < len; i++) {
		sum += buf[i] * buf[i];
	}
	return sum;
}

uint32_t reduction_max(int len, uint32_t *buf) {
	uint32_t max = buf[0];

	int i;
	for(i = 1; i < len; i++) {
		if(max < buf[i]) {
			max = buf[i];
		}
	}
	return max;
}

uint32_t reduction_min(int len, uint32_t *buf) {
	uint32_t min = buf[0];

	int i;
	for(i = 1; i < len; i ++) {
		if(min > buf[i]) {
			min = buf[i];
		}
	}
	return min;
}

void reduction_sort(int len, uint32_t *buf) {
	(void) insertionSort(len, (int *) buf);
}

/* 
	Return reduced arr of val fater preforming reduce_fn on given data
	Param: flag = 1 then for final reduction used for SOS
*/
uint32_t reduction_handler(int reduce_fn, int reduce_size, uint32_t *data, int flag) {
	if(reduce_fn == 0) {
		return reduction_sum(reduce_size, data);
	} else if(reduce_fn == 1) {
		if(flag) {
			return reduction_sum(reduce_size, data);
		}
		return reduction_sos(reduce_size, data);
	} else if(reduce_fn == 2) {
		return reduction_max(reduce_size, data);
	} else if(reduce_fn == 3) {
		return reduction_min(reduce_size, data);
	} else {
		return -1;
	}
}

uint32_t *reduction_sort_handler(int reduce_size, uint32_t *data) {
	uint32_t *reduced = malloc(reduce_size * sizeof(uint32_t));
	if(reduced == NULL) {
		fprintf(stderr, "ERROR: malloc failed at recv_handler!\n");
		return NULL;
	}

	memcpy(reduced, data, reduce_size * sizeof(uint32_t));
	reduction_sort(reduce_size, reduced); // can add different sorting algorithms
	return reduced; // remember to free!!!;
}