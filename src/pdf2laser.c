/// pdf2laser.c --- tool for printing to Epilog Fusion laser cutters

// Copyright (C) 2015-2020 Zachary Elliott <contact@zell.io>
// Copyright (C) 2011-2015 Trammell Hudson <hudson@osresearch.net>
// Copyright (C) 2008-2011 AS220 Labs <brandon@as220.org>
// Copyright (C) 2002-2008 Andrews & Arnold Ltd <info@aaisp.net.uk>

// Authors: Andrew & Arnold Ltd <info@aaisp.net.uk>
//          Brandon Edens <brandon@as220.org>
//          Trammell Hudson <hudson@osresearch.net>
//          Zachary Elliott <contact@zell.io>
// URL: https://github.com/zellio/pdf2laser
// Version: 0.5.1

/// Commentary:

//

/// License:

// This program is free software: you can redistribute it and/or modify it
// under the terms of the GNU General Public License as published by the Free
// Software Foundation, either version 3 of the License, or (at your option)
// any later version.

// This program is distributed in the hope that it will be useful, but WITHOUT
// ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
// FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
// more details.

// You should have received a copy of the GNU General Public License along
// with this program.  If not, see <http://www.gnu.org/licenses/>.

/// Code:

#define _POSIX_C_SOURCE 200809L
#define _XOPEN_SOURCE 700

#include "pdf2laser.h"
#include <dirent.h>                // for closedir, opendir, readdir, DIR, dirent
#include <ghostscript/gserrors.h>  // for gs_error_Quit
#include <ghostscript/iapi.h>      // for gsapi_delete_instance, gsapi_exit, gsapi_init_with_args, gsapi_new_instance, gsapi_set_arg_encoding, gsapi_set_stdio, GSDLLCALL, GS_ARG_ENCODING_UTF8
#include <libgen.h>                // for basename
#include <limits.h>                // for PATH_MAX
#include <stdbool.h>               // for false, bool, true
#include <stddef.h>                // for size_t, NULL
#include <stdint.h>                // for int32_t, uint8_t
#include <stdio.h>                 // for perror, snprintf, fclose, fopen, FILE, fileno, fwrite, printf, fflush, fprintf, fread, stdin, stderr
#include <stdlib.h>                // for calloc, free, getenv, mkdtemp
#include <string.h>                // for strndup, strnlen, strncmp, strrchr
#include <sys/stat.h>              // for stat, S_ISREG
#include <unistd.h>                // for unlink, rmdir
#include "config.h"                // for FILENAME_NCHARS, GS_ARG_NCHARS, DEBUG, TMP_DIRECTORY
#include "pdf2laser_cli.h"         // for pdf2laser_optparse
#include "pdf2laser_generator.h"   // for generate_eps, generate_pjl, generate_ps
#include "pdf2laser_printer.h"     // for printer_send
#include "pdf2laser_util.h"        // for pdf2laser_sendfile
#include "type_preset_file.h"      // for preset_file_t, preset_file_create
#include "type_print_job.h"        // for print_job_t, print_job_to_string, print_job_create
#include "type_raster.h"           // for raster_t

FILE *fh_vector;
static int GSDLLCALL gsdll_stdout(__attribute__ ((unused)) void *minst, const char *str, int len)
{
	size_t rc = fwrite(str, 1, len, fh_vector);
	fflush(fh_vector);
	return rc;
}

/**
 * Execute ghostscript feeding it an ecapsulated postscript file which is then
 * converted into a bitmap image. As a byproduct output of the ghostscript
 * process is redirected to a .vector file which will contain instructions on
 * how to perform a vector cut of lines within the postscript.
 *
 * @param filename_bitmap the filename to use for the resulting bitmap file.
 * @param filename_eps the filename to read in encapsulated postscript from.
 * @param filename_vector the filename that will contain the vector
 * information.
 * @param bmp_mode a string which is one of bmp16m, bmpgray, or bmpmono.
  * @param resolution the encapsulated postscript resolution.
 *
 * @return Return true if the execution of ghostscript succeeds, false
 * otherwise.
 */
static bool execute_ghostscript(print_job_t *print_job, const char * const filename_bitmap, const char * const filename_eps, const char * const filename_vector, const char * const bmp_mode)
{
	int gs_argc = 8;
	char *gs_argv[8];

	gs_argv[0] = "gs";
	gs_argv[1] = "-q";
	gs_argv[2] = "-dBATCH";
	gs_argv[3] = "-dNOPAUSE";

	gs_argv[4] = calloc(64, sizeof(char));
	snprintf(gs_argv[4], 64, "-r%d", print_job->raster->resolution);

	gs_argv[5] = calloc(GS_ARG_NCHARS, sizeof(char));
	snprintf(gs_argv[5], GS_ARG_NCHARS, "-sDEVICE=%s", bmp_mode);

	gs_argv[6] = calloc(GS_ARG_NCHARS + 13, sizeof(char));
	snprintf(gs_argv[6], GS_ARG_NCHARS + 13, "-sOutputFile=%s", filename_bitmap);

	gs_argv[7] = strndup(filename_eps, FILENAME_NCHARS);

	fh_vector = fopen(filename_vector, "w");

	int32_t rc;

	void *minst = NULL;
	rc = gsapi_new_instance(&minst, NULL);

	if (rc < 0)
		return false;

	rc = gsapi_set_arg_encoding(minst, GS_ARG_ENCODING_UTF8);
	if (rc == 0) {
		gsapi_set_stdio(minst, NULL, gsdll_stdout, NULL);
		rc = gsapi_init_with_args(minst, gs_argc, gs_argv);
	}

    int32_t rc2 = gsapi_exit(minst);
    if ((rc == 0) || (rc2 == gs_error_Quit))
		rc = rc2;

	fclose(fh_vector);

	gsapi_delete_instance(minst);

	if (rc)
		return false;

	return true;
}

static char *append_directory(char *base_directory, char *directory_name)
{
	static const char *path_template = "%s/%s";

	size_t path_length = 1;
	path_length += snprintf(NULL, 0, path_template, base_directory, directory_name);

	char *s = calloc(path_length, sizeof(char));
	snprintf(s, path_length, path_template, base_directory, directory_name);

	return s;
}

static int pdf2laser_load_presets(preset_file_t ***preset_files, size_t *preset_files_count) {
	char *search_dirs[3];
	search_dirs[0] = strndup("/usr/lib/pdf2laser/presets", 27);
	search_dirs[1] = strndup("/etc/pdf2laser/presets", 23);

	size_t homedir_len = strnlen(getenv("HOME"), PATH_MAX) + strnlen(".pdf2laser/presets", 19) + 2;
	search_dirs[2] = calloc(homedir_len, sizeof(char));
	snprintf(search_dirs[2], homedir_len, "%s/.pdf2laser/presets", getenv("HOME"));

	size_t preset_file_count = 0;
	size_t preset_file_index = 0;
	struct dirent *directory_entry;

	for (size_t index = 0; index < 3; index += 1) {
		DIR *preset_dir = opendir(search_dirs[index]);
		if (preset_dir == NULL) {
			if (DEBUG) {
				perror("opendir failed");
			}
			continue;
		}
		while ((directory_entry = readdir(preset_dir))) {
			char *preset_file_path = append_directory(search_dirs[index], directory_entry->d_name);

			struct stat preset_file_stat;
			if (stat(preset_file_path, &preset_file_stat))
				continue;

			if (!S_ISREG(preset_file_stat.st_mode))
				continue;

			preset_file_count += 1;

			free(preset_file_path);
		}
		closedir(preset_dir);
	}

	*preset_files_count = preset_file_count;
	*preset_files = calloc(preset_file_count, sizeof(preset_file_t*));
	for (size_t index = 0; index < 3; index += 1) {
		DIR *preset_dir = opendir(search_dirs[index]);
		if (preset_dir == NULL) {
			if (DEBUG) {
                  perror("opendir failed");
			}
			continue;
		}
		while ((directory_entry = readdir(preset_dir))) {
			char *preset_file_path = append_directory(search_dirs[index], directory_entry->d_name);

			struct stat preset_file_stat;
			if (stat(preset_file_path, &preset_file_stat))
				continue;

			if (!S_ISREG(preset_file_stat.st_mode))
				continue;

			if (preset_file_index < preset_file_count) {
				(*preset_files)[preset_file_index] = preset_file_create(preset_file_path);
				preset_file_index += 1;
			}

			free(preset_file_path);
		}
		closedir(preset_dir);
	}

	for (size_t index = 0; index < 3; index += 1)
		free(search_dirs[index]);

	return  0;
}

/**
 * Main entry point for the program.
 *
 * @param argc The number of command line options passed to the program.
 * @param argv An array of strings where each string represents a command line
 * argument.
 * @return An integer where 0 represents successful termination, any other
 * value represents an error code.
 */
int main(int argc, char *argv[])
{
	// Create tmp working directory
	char tmpdir_template[FILENAME_NCHARS] = { '\0' };
	snprintf(tmpdir_template, FILENAME_NCHARS, "%s/%s.XXXXXX", TMP_DIRECTORY, basename(argv[0]));
	char *tmpdir_name = mkdtemp(tmpdir_template);

	if (tmpdir_name == NULL) {
		perror("mkdtemp failed");
		return false;
	}

	// Default host is defined in config.h, and can be overridden at configuration time, e.g.
	//   ./configure DEFAULT_HOST=foo
	/* char *host = DEFAULT_HOST; */

	preset_file_t **preset_files;
	size_t preset_files_count;
	pdf2laser_load_presets(&preset_files, &preset_files_count);

	print_job_t *print_job = print_job_create();

	// Process command line options
	pdf2laser_optparse(print_job, preset_files, preset_files_count, argc, argv);

	const char *source_filename = print_job->source_filename;

	char *source_basename = strndup(print_job->source_filename, FILENAME_NCHARS);
	source_basename = basename(source_basename);

	// If no job name is specified, use just the filename if there
	if (!print_job->name) {
		print_job->name = strndup(source_basename, FILENAME_NCHARS);
	}

	// Report the settings on stdout
	printf("Configured values:\n%s\n", print_job_to_string(print_job));

	char *target_base = strndup(source_basename, FILENAME_NCHARS - 8);
	char *last_dot = strrchr(target_base, '.');
	if (last_dot != NULL)
		*last_dot = '\0';

	char target_basename[FILENAME_NCHARS - 8] = { '\0' };
	char target_bitmap[FILENAME_NCHARS] = { '\0' };
	char target_eps[FILENAME_NCHARS] = { '\0' };
	char target_pdf[FILENAME_NCHARS] = { '\0' };
	char target_pjl[FILENAME_NCHARS] = { '\0' };
	char target_ps[FILENAME_NCHARS] = { '\0' };
	char target_vector[FILENAME_NCHARS] = { '\0' };

	snprintf(target_basename, FILENAME_NCHARS - 8, "%s/%s", tmpdir_name, target_base);
	snprintf(target_bitmap, FILENAME_NCHARS, "%s.bmp", target_basename);
	snprintf(target_eps, FILENAME_NCHARS, "%s.eps", target_basename);
	snprintf(target_pdf, FILENAME_NCHARS, "%s.pdf", target_basename);
	snprintf(target_pjl, FILENAME_NCHARS, "%s.pjl", target_basename);
	snprintf(target_ps, FILENAME_NCHARS, "%s.ps", target_basename);
	snprintf(target_vector, FILENAME_NCHARS, "%s.vector", target_basename);

	FILE *fh_bitmap;
	FILE *fh_pdf;
	FILE *fh_ps;
	FILE *fh_pjl;
	FILE *fh_vector;

	fh_pdf = fopen(target_pdf, "w");
	if (!fh_pdf) {
		perror(target_pdf);
		return 1;
	}

	if (strncmp(source_filename, "stdin", 5) == 0) {
		uint8_t buffer[102400];
		size_t rc;
		while ((rc = fread(buffer, 1, 102400, stdin)) > 0)
			fwrite(buffer, 1, rc, fh_pdf);
	}
	else {
		FILE *fh_source = fopen(source_filename, "r");
		pdf2laser_sendfile(fileno(fh_pdf), fileno(fh_source));
		fclose(fh_source);
	}

	fclose(fh_pdf);

	if (!generate_ps(target_pdf, target_ps)) {
		perror("Error converting pdf to postscript.");
		return 1;
	}

	if (!print_job->debug) {
		/* Debug is disabled so remove generated pdf file. */
		if (unlink(target_pdf)) {
			perror(target_pdf);
		}
	}

	fh_ps = fopen(target_ps, "r");
	if (!fh_ps) {
		perror("Error opening postscript file.");
		return 1;
	}

	/* Open the encapsulated postscript file for writing. */
	FILE * const fh_eps = fopen(target_eps, "w");
	if (!fh_eps) {
		perror(target_eps);
		return 1;
	}

	/* Convert postscript to encapsulated postscript. */
	if (!generate_eps(print_job, fh_ps, fh_eps)) {
		perror("Error converting postscript to encapsulated postscript.");
		fclose(fh_eps);
		return 1;
	}

	/* Cleanup after encapsulated postscript creation. */
	fclose(fh_eps);
	if (fh_ps != stdin) {
		fclose(fh_ps);
		if (unlink(target_ps)) {
			perror(target_ps);
		}
	}

	const char * const raster_string =
		print_job->raster->mode == 'c' ? "bmp16m" :
		print_job->raster->mode == 'g' ? "bmpgray" :
		"bmpmono";


	fprintf(stderr, "execute_ghostscript\n");
	if(!execute_ghostscript(print_job,
							target_bitmap,
							target_eps,
							target_vector,
							raster_string)) {
		perror("Failure to execute ghostscript command.\n");
		return 1;
	}

	/* Open file handles needed by generation of the printer job language
	 * file.
	 */
	fh_bitmap = fopen(target_bitmap, "r");
	fh_vector = fopen(target_vector, "r");
	fh_pjl = fopen(target_pjl, "w");
	if (!fh_pjl) {
		perror(target_pjl);
		return 1;
	}

	/* Execute the generation of the printer job language (pjl) file. */
	if (!generate_pjl(print_job, fh_bitmap, fh_pjl, fh_vector)) {
		perror("Generation of pjl file failed.\n");
		fclose(fh_pjl);
		return 1;
	}

	/* Close open file handles. */
	fclose(fh_bitmap);
	fclose(fh_pjl);
	fclose(fh_vector);

	/* Cleanup unneeded files provided that debug mode is disabled. */
	if (!print_job->debug) {
		if (unlink(target_bitmap)) {
			perror(target_bitmap);
		}
		if (unlink(target_eps)) {
			perror(target_eps);
		}
		if (unlink(target_vector)) {
			perror(target_vector);
		}
	}

	/* Open printer job language file. */
	fh_pjl = fopen(target_pjl, "r");
	if (!fh_pjl) {
		perror(target_pjl);
		return 1;
	}

	printf("Generated values:\n%s\n", print_job_to_string(print_job));

	/* Send print job to printer. */
	if (!printer_send(print_job->host, fh_pjl, print_job->name)) {
		perror("Could not send pjl file to printer.\n");
		return 1;
	}

	fclose(fh_pjl);
	if (!print_job->debug) {
		if (unlink(target_pjl)) {
			perror(target_pjl);
		}
	}

	if (!print_job->debug) {
		if (rmdir(tmpdir_name) == -1) {
			perror("rmdir failed: ");
			return 1;
		}
	}

	return 0;
}
