	#include <stdio.h>
	#include <stdlib.h>
	#include <unistd.h>
	#include <string.h>
	#include <stdint.h>
	#include <time.h>

	void sleepMs(uint32_t ms) {
		struct timespec ts;
		ts.tv_sec = 0 + (ms / 1000);
		ts.tv_nsec = 1000 * 1000 * (ms % 1000);
		nanosleep(&ts, NULL);
	}

	int main() {
		int pipe_fds[2];
		int n;
		char buf[0x101] = {0};
		pid_t pid;
		
		pipe(pipe_fds);

		char *cmd[] = {"/home/nikolai/motivational_abyss/playground/nw_analysis/hooklib/sr", NULL};

		if ((pid = fork()) == 0) { /* child */
			dup2(pipe_fds[1], 1); // set stdout of the process to the write end of the pipe
			execvp(cmd[0], cmd); // execute the program.
			fflush(stdout);
			perror(cmd[0]); // only reached in case of error
			exit(0);
		} else if (pid == -1) { /* failed */
			perror("fork");
			exit(1);
		} else { /* parent */

			while (1) {
				sleepMs(1500); // Wait a bit to let the child program run a little
				printf("Trying to read every 1500ms\n");
				if ((n = read(pipe_fds[0], buf, 0x100)) >= 0) { // Try to read stdout of the child process from the read end of the pipe
					buf[n] = 0; /* terminate the string */
					fprintf(stdout, "Got:\n%s", buf);
				} else {
					fprintf(stderr, "read failed\n");
					perror("read");
				}
			}
		}
	}
