#include "goonwc.h"
#include <getopt.h>
#include <stdio.h>

int main(int argc, char *argv[]) {
	char *startup_cmd = NULL;

	int c;
	while ((c = getopt(argc, argv, "s:h")) != -1) {
		switch (c) {
		case 's':
			startup_cmd = optarg;
			break;
		default:
			printf("Usage: %s [-s startup command]\n", argv[0]);
			return 0;
		}
	}
	if (optind < argc) {
		printf("Usage: %s [-s startup command]\n", argv[0]);
		return 0;
	}

	goonwc_server server = {0};
	if (!server_init(&server)) {
		return 1;
	}

	server_run(&server, startup_cmd);
	server_cleanup(&server);
	return 0;
}
