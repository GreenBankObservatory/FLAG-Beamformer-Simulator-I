#include <poll.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "fifo.h"

const int MAX_CMD_LEN = 64;

int command_fifo = -1;

int open_fifo(char *fifo_loc)
{
	command_fifo = open(fifo_loc, O_RDONLY | O_NONBLOCK);
	if (command_fifo<0)
	{
		fprintf(stderr, "vegas_fits_writer: Error opening control fifo %s\n", fifo_loc);
		perror("open");
		exit(1);
	}

	return command_fifo;
}

cmd_t check_cmd()
{
	char cmd[MAX_CMD_LEN];

// 	fprintf(stderr, "command_fifo: %d\n", command_fifo);
	
	struct pollfd pfd[2];
        pfd[1].fd = command_fifo;
        pfd[1].events = POLLIN;
        pfd[0].fd = fileno(stdin);
        pfd[0].events = POLLIN;
		// ?, num file desc, timeout
        int rv = poll(pfd, 2, 1000);
        if (rv==0)
        {
// 			fprintf(stderr, "rv == 0 :(\n");
            return INVALID; //????
        }
        else if (rv<0)
        {
            if (errno!=EINTR)
            {
                perror("poll");
            }
//             fprintf(stderr, "rv < 0 :(\n");
            return INVALID; //????
        }

        // clear the command
        memset(cmd, 0, MAX_CMD_LEN);
		int i;
        for (i=0; i<2; ++i)
        {
            rv = 0;
            if (pfd[i].revents & POLLIN)
            {
                if (read(pfd[i].fd, cmd, MAX_CMD_LEN-1)<1)
				{
// 					fprintf(stderr, "read failed :(\n");
// 					perror("read");
//                     return INVALID;
				}
                else
                {
// 					fprintf(stderr, "read success :(\n");
                    rv = 1;
                    break;
                }
            }
        }

        if (pfd[0].revents==POLLHUP)
			fprintf(stderr, "POLLHUP :(\n");

		if (rv==0)
        {
// 			fprintf(stderr, "rv == 0 again :(\n");
            return INVALID;
        }
        else if (rv<0)
        {
// 			fprintf(stderr, "rv < 0 again :(\n");
            if (errno==EAGAIN)
            {
                return INVALID;
            }
            else
            {
                perror("read");
                return INVALID;
            }
        }

        // Truncate at newline
        // TODO: allow multiple commands in one read?
        char *ptr = strchr(cmd, '\n');
        if (ptr!=NULL)
        {
            *ptr='\0';
        }

        // Process the command
        if (strncasecmp(cmd,"START",MAX_CMD_LEN)==0)
        {
			return START;
        }
        else if (strncasecmp(cmd,"STOP",MAX_CMD_LEN)==0)
		{
			return STOP;
		}
		else if (strncasecmp(cmd,"QUIT",MAX_CMD_LEN)==0)
		{
			return QUIT;
		}
        else
        {
            // Unknown command
            return INVALID;
        }

        
}