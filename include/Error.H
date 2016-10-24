/*
 * Copyright (c) 2015 Santiago Bock
 *
 * See the file LICENSE.txt for copying permission.
 */

#ifndef ERROR_H_
#define ERROR_H_

#include "Types.H"

#include <limits>

//-------------------------------------------------------------------------
// Defines
//-------------------------------------------------------------------------

#define MAX_MSG_SIZE 1024
#define error(msg, ...) {fprintf(stderr, "%s:%u: ", __FILE__, __LINE__); sprintf(msg_buffer, msg, ## __VA_ARGS__); print_error(msg_buffer);}
#define warn(msg, ...) {fprintf(stderr, "%s:%u: ", __FILE__, __LINE__); sprintf(msg_buffer, msg, ## __VA_ARGS__); print_warn(msg_buffer);}

#define debug(fmt, ...) \
		do { \
			if (DEBUG) { \
				if (timestamp >= debugStart) { \
					debug2_timestamp = timestamp; \
					fprintf(stdout, "%lu: %s.%s", timestamp, name.c_str(), __func__); \
					fprintf(stdout, fmt, ## __VA_ARGS__); \
					fprintf(stdout, "\n"); \
					fflush(stdout);\
				} \
			} \
		} while (0)

/*
 * debug2() is to be used when timestamp is not a defined variable in the current scope. Instead, it uses
 * debug2_timestamp, which holds the value of timestamp during the previous invocation ot debug().
 */
#define debug2(fmt, ...) \
		do { \
			if (DEBUG) { \
				if (debug2_timestamp < std::numeric_limits<uint64>::max()){ \
					fprintf(stdout, "%lu: ", debug2_timestamp); \
					fprintf(stdout, fmt, ## __VA_ARGS__); \
					fprintf(stdout, "\n"); \
					fflush(stdout);\
				} \
			} \
		} while (0)

/*
 * myassert works the same as assert but prints the current timestamp by calling engine->getTimestamp()
 */
#ifdef	NDEBUG
#define myassert(expr)		(static_cast<void>(0))
#else /* NDEBUG.  */
#define myassert(expr)					\
  ((expr)								\
   ? static_cast<void>(0)				\
   : print_assert(engine->getTimestamp(), __STRING(expr), __FILE__, __LINE__, __PRETTY_FUNCTION__))
#endif /* NDEBUG.  */

//-------------------------------------------------------------------------
// Global variables
//-------------------------------------------------------------------------

extern char msg_buffer[MAX_MSG_SIZE];
extern uint64 debug2_timestamp;


//-------------------------------------------------------------------------
// Function Prototypes
//-------------------------------------------------------------------------

void print_error(char *msg);
void print_warn(char *msg);
void print_assert(uint64 timestamp, const char *assertion, const char *file, unsigned line, const char *function) __attribute__ ((noreturn));

#endif /* ERROR_H_ */
