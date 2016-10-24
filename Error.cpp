/*
 * Copyright (c) 2015 Santiago Bock
 *
 * See the file LICENSE.txt for copying permission.
 */

#include "Error.H"

#include <cstdio>
#include <cstdlib>
#include <cerrno>


char msg_buffer[MAX_MSG_SIZE];
uint64 debug2_timestamp = std::numeric_limits<uint64>::max();


void print_error(char *msg){
	print_warn(msg);
	exit(EXIT_FAILURE);
}
void print_warn(char *msg){
	if (errno == 0){
		fputs(msg, stderr);
		fputs("\n", stderr);
	} else {
		perror(msg);
	}
}

void print_assert(uint64 timestamp, const char *assertion, const char *file, unsigned line, const char *function){
	fprintf(stderr, "%lu: %s:%u: %s: Assertion '%s' failed.\n", timestamp, file, line, function, assertion);
	abort();
}
