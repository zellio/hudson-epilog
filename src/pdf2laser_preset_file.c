#include "pdf2laser_preset_file.h"
#include <stdlib.h>

preset_file_t *preset_file_create(void)
{
	preset_file_t *preset_file = calloc(1, sizeof(preset_file_t));
	return preset_file;
}

preset_file_t *preset_file_destroy(preset_file_t *self)
{
	free(self);
	return NULL;
}
