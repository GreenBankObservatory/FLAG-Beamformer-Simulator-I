#ifndef FIFO_H
#define FIFO_H

typedef enum cmd {
	INVALID = -1,
	START,
	STOP,
	QUIT
} cmd_t;

int open_fifo(char *fifo_loc);
cmd_t check_cmd();

#endif