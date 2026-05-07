/*
Copyright 2005, 2006, 2007 Dennis van Weeren
Copyright 2008, 2009 Jakub Bednarski
Copyright 2012 Till Harbaum

This file is part of Minimig

Minimig is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 3 of the License, or
(at your option) any later version.

Minimig is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <sched.h>
#include <inttypes.h>
#include <ctype.h>
#include <string.h>
#include <time.h>
#include "menu.h"
#include "user_io.h"
#include "input.h"
#include "frame_timer.h"
#include "fpga_io.h"
#include "scheduler.h"
#include "osd.h"
#include "offload.h"
#include "build_meta.h"

void http_server_start();

const char *version = "$VER:" VDATE;

#ifndef BUILD_STAMP
#define BUILD_STAMP "unknown"
#endif

#ifndef BUILD_HASH
#define BUILD_HASH "unknown"
#endif

static void log_startup_event(const char *message)
{
	FILE *log = fopen("/media/fat/mister-http-startup.log", "a");
	if (log)
	{
		time_t now = time(NULL);
		fprintf(log, "[%ld] startup: %s\n", (long)now, message);
		fclose(log);
	}
	fprintf(stderr, "startup: %s\n", message);
	fflush(stderr);
}

int main(int argc, char *argv[])
{
	char startup_msg[1024];
	snprintf(startup_msg, sizeof(startup_msg), "build version=%s stamp=%s hash=%s", version + 5, BUILD_STAMP, BUILD_HASH);
	log_startup_event(startup_msg);
	snprintf(startup_msg, sizeof(startup_msg), "main entry argc=%d argv1=%s argv2=%s", argc,
		(argc > 1) ? argv[1] : "",
		(argc > 2) ? argv[2] : "");
	log_startup_event(startup_msg);

	// Always pin main worker process to core #1 as core #0 is the
	// hardware interrupt handler in Linux.  This reduces idle latency
	// in the main loop by about 6-7x.
	cpu_set_t set;
	CPU_ZERO(&set);
	CPU_SET(1, &set);
	sched_setaffinity(0, sizeof(set), &set);
	log_startup_event("sched_setaffinity complete");

	offload_start();
	log_startup_event("offload_start complete");

	fpga_io_init();
	log_startup_event("fpga_io_init complete");

	DISKLED_OFF;

	printf("\nMinimig by Dennis van Weeren");
	printf("\nARM Controller by Jakub Bednarski");
	printf("\nMiSTer code by Sorgelig\n\n");

	printf("Version %s\n", version + 5);
	printf("Build %s %s\n\n", BUILD_STAMP, BUILD_HASH);

	if (argc > 1) printf("Core path: %s\n", argv[1]);
	if (argc > 2) printf("XML path: %s\n", argv[2]);

	if (!is_fpga_ready(1))
	{
		log_startup_event("fpga not ready at startup");
		printf("\nGPI[31]==1. FPGA is uninitialized or incompatible core loaded.\n");
		printf("Quitting. Bye bye...\n");
		exit(0);
	}

	log_startup_event("FindStorage begin");
	FindStorage();
	log_startup_event("FindStorage complete");
	log_startup_event("user_io_init begin");
	user_io_init((argc > 1) ? argv[1] : "",(argc > 2) ? argv[2] : NULL);
	log_startup_event("user_io_init complete");
	log_startup_event("http_server_start begin");
	http_server_start();
	log_startup_event("http_server_start complete");

#ifdef USE_SCHEDULER
	scheduler_init();
	scheduler_run();
#else
	while (1)
	{
		if (!is_fpga_ready(1))
		{
			fpga_wait_to_reset();
		}

		user_io_poll();
		frame_timer();
		input_poll(0);
		HandleUI();
		OsdUpdate();
	}
#endif
	return 0;
}
