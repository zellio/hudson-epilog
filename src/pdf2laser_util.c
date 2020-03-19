#include "pdf2laser_util.h"
#include <errno.h>         // for errno, EAGAIN, EINTR
#include <inttypes.h>      // for PRId64, PRIu64, int64_t
#include <stddef.h>        // for size_t, NULL
#include <stdio.h>         // for perror, printf
#ifdef __linux
#include <sys/sendfile.h>  // for sendfile
#endif
#include <sys/stat.h>      // for fstat, stat
#include <unistd.h>        // for ssize_t
#include <stdlib.h>        // for calloc
#include <stdio.h>         // for vsnprintf
#include <stdarg.h>

int pdf2laser_sendfile(int out_fd, int in_fd)
{
#ifdef __linux
	struct stat file_stat;
	if (fstat(in_fd, &file_stat)) {
		perror("Error stating file");
		return -1;
	}

	ssize_t bs = 0;
	size_t bytes_sent = 0;
	size_t count = file_stat.st_size;

	while (bytes_sent < count) {
		if ((bs = sendfile(out_fd, in_fd, NULL, count - bytes_sent)) <= 0) {
			if (errno == EINTR || errno == EAGAIN)
				continue;
			perror("sendfile failed");
			return errno;
		}
		bytes_sent += bs;
	}
#else
	char buffer[102400];
	size_t rc;
	while ((rc = read(in_fd, buffer, 102400)) > 0)
		write(out_fd, buffer, rc);
#endif
	return 0;
}

char *pdf2laser_format_string(char *template, ...)
{
	va_list ap;

	va_start(ap, template);
	ssize_t s_length = vsnprintf(NULL, 0, template, ap);
	va_end(ap);

	if (s_length < 0) {
		return NULL;
	}

	s_length += 1;

	char *s = calloc(s_length, sizeof(char));
	if (s == NULL) {
		return NULL;
	}

	va_start(ap, template);
	s_length = vsnprintf(s, s_length, template, ap);
	va_end(ap);

	if (s_length < 0) {
		free(s);
		return NULL;
	}

	return s;
}
