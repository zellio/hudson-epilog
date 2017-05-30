#ifndef __PDF2LASER_VECTOR_LIST_H__
#define __PDF2LASER_VECTOR_LIST_H__ 1

#include <inttypes.h>
#include <stdlib.h>
#include <stdbool.h>
#include <math.h>
#include <stdio.h>

#include "pdf2laser_vector.h"

#ifdef __cplusplus
extern "C" {
#endif
#if 0
}
#endif

typedef struct vector_list vector_list_t;
struct vector_list {
	vector_t *head;
	vector_t *tail;
	int32_t length;
	int32_t pass;
	int32_t power;
	int32_t speed;
};

vector_list_t *vector_list_create(void);
vector_list_t *vector_list_append(vector_list_t *self, vector_t *vector);
vector_list_t *vector_list_stats(vector_list_t *self);
vector_t *vector_list_find_closest(vector_list_t *list, point_t *point);
vector_t *vector_list_remove(vector_list_t *self, vector_t *vector);
vector_list_t *vector_list_optimize(vector_list_t *self);
bool vector_list_contains(vector_list_t *self, vector_t *vector);

#ifdef __cplusplus
};
#endif

#endif
