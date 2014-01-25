#include <stdlib.h>
#include <stdio.h>

void runpipe();

int
main(int argc, char **argv)
{
	int pid, status;
	int fd[2];

	pipe(fd);

	switch (pid = fork()) {

	case 0: /* child */
		runpipe(fd);
		exit(0);

	default: /* parent */
		while ((pid = wait(&status)) != -1)
			fprintf(stderr, "process %d exits with %d\n", pid, WEXITSTATUS(status));
		break;

	case -1:
		perror("fork");
		exit(1);
	}
	exit(0);
}

char *cmd1[] = { "/bin/ls", "-al", "/", 0 };
char *cmd2[] = { "/usr/bin/tr", "a-z", "A-Z", 0 };

void
runpipe(int pfd[])
{
	int pid;

	switch (pid = fork()) {

	case 0: /* child */
		dup2(pfd[0], 0);
		close(pfd[1]);	/* the child does not need this end of the pipe */
		execvp(cmd2[0], cmd2);
		perror(cmd2[0]);

	default: /* parent */
		dup2(pfd[1], 1);
		close(pfd[0]);	/* the parent does not need this end of the pipe */
		execvp(cmd1[0], cmd1);
		perror(cmd1[0]);

	case -1:
		perror("fork");
		exit(1);
	}
}
