#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <unistd.h>
#include <sys/wait.h>
#include <string.h>

void example1() {
   pid_t my_pid, parent_pid, child_pid;
   int status;

/* get and print my pid and my parent's pid. */

   my_pid = getpid();
   parent_pid = getppid();
   printf("\nParent: my pid is %d\n\n", my_pid);
   printf("Parent: my parent's pid is %d\n\n", parent_pid);

/* print error message if fork() fails */
   if((child_pid = fork()) < 0 )
   {
      perror("fork failure");
      exit(1);
   }

/* fork() == 0 for child process */

   if(child_pid == 0)
   {  printf("\nChild: I am a new-born process!\n\n");
      my_pid = getpid();    parent_pid = getppid();
      printf("Child: my pid is: %d\n\n", my_pid);
      printf("Child: my parent's pid is: %d\n\n", parent_pid);
      printf("Child: I will sleep 3 seconds and then execute - date - command \n\n");

      sleep(3); 
      printf("Child: Now, I woke up and am executing date command \n\n");
      execl("/bin/date", "date", 0, 0);
      perror("execl() failure!\n\n");

      printf("This print is after execl() and should not have been executed if execl were successful! \n\n");

      _exit(1);
   }
/*
 * parent process
 */
   else
   {
      printf("\nParent: I created a child process.\n\n");
      printf("Parent: my child's pid is: %d\n\n", child_pid);
      system("ps -acefl | grep ercal");  printf("\n \n");
      wait(&status); /* can use wait(NULL) since exit status
                        from child is not used. */
      printf("\n Parent: my child is dead. I am going to leave.\n \n ");
   }
}

int example2() {
	int filedes[2];
	int file_rd = fileno(stdin);
	int file_wr = fileno(stdout);
	
	int ch;
	
	int pipe_rd;
	int pipe_wr;
	
	pipe(filedes);
	
	// reading pipe_rd accesses data written to pipe_wr
	pipe_wr = filedes[1];
	pipe_rd = filedes[0];
	
	if (fork()) {
		/* child */
		/* stdin replaced by pipe output */
		dup2(pipe_rd, file_rd);
		
		close(pipe_wr);
		
		while ((ch = fgetc(stdin)) > 0) {
			fprintf(stdout, "%c", ch-1);
		}
		
		fprintf(stderr, "child: the last was %d\n", ch);
		close(pipe_rd);
	} else {
		/* parent */
		/* stdout replaced by pipe input */
		dup2(pipe_wr, file_wr);
		
		close(pipe_rd);
		
		while ((ch=fgetc(stdin))>0) {
			fprintf(stdout, "%c", ch+1);
		}
		
		fprintf(stderr, "parent: the last was %d\n", ch);
		
		close(pipe_wr);
	}
}



int example3() {
	int pipe_read[2], pipe_write[2], ch, n;
	char buf[0x2000];
	
	pipe(pipe_read);
	pipe(pipe_write);
	
	pid_t pid;
	
	char *cmd[] = {"/home/nikolai/motivational_abyss/playground/nw_analysis/hooklib/Stockfish/src/stockfish"};
	char *uciCommands[] = {"uci\n", "isready\n", "position startpos moves\n", "go infinite\n", "stop\n"};

	switch (pid = fork()) {
		case 0: /* child */
			dup2(pipe_write[1], 1); // set the stdout of the child to the write end of the pipe
			dup2(pipe_read[0], 0); // set the stdin to the read end of the pipe
			execvp(cmd[0], 0);
			perror(cmd[0]);

		default: /* parent */
			if ((n = read(pipe_write[0], buf, 4049)) >= 0) {
				buf[n] = 0;
				if (!strstr(buf, "Stockfish")) {
					fprintf(stderr, "No ``Stockfish`` needle in greeting\n");
					return -1;
				} else
					fprintf(stdout, "Got ``Stockfish`` header\n");
			}
			
			if ((n = write(pipe_read[1], uciCommands[0], strlen(uciCommands[0])) == -1))
				perror("write()");
			
			if ((n = read(pipe_write[0], buf, 4049)) >= 0) {
				buf[n] = 0;	/* terminate the string */
				if (!strstr(buf, "uciok")) {
					fprintf(stderr, "No uciok\n");
					return -1;
				} else
					fprintf(stdout, "Got uciok\n");
			}
			
			if ((n = write(pipe_read[1], uciCommands[1], strlen(uciCommands[1])) == -1))
				perror("write()");
				
			if ((n = read(pipe_write[0], buf, 100)) >= 0) {
				buf[n] = 0;	/* terminate the string */
				if (!strstr(buf, "readyok")) {
					fprintf(stderr, "No readyok\n");
					return -1;
				} else
					fprintf(stdout, "Got readyok\n");
			}
			
			if ((n = write(pipe_read[1], uciCommands[2], strlen(uciCommands[2])) == -1))
				perror("write()");
			
			sleep(1);
			
			if ((n = write(pipe_read[1], uciCommands[3], strlen(uciCommands[3])) == -1))
				perror("write()");
				
			printf("Calculating move\n");
			sleep(3);
			
			if ((n = write(pipe_read[1], uciCommands[4], strlen(uciCommands[4])) == -1))
				perror("write()");
				
			sleep(1);
				
			if ((n = read(pipe_write[0], buf, 0x2000)) >= 0) {
				buf[n] = 0;	/* terminate the string */
				char *ptr;
				if (!(ptr = strstr(buf, "bestmove "))) {
					fprintf(stderr, "No bestmove in %s\n", buf);
					return -1;
				} else {
					fprintf(stdout, "Best move is ``%s``\n", ptr);
				}
			}
			
		case -1:
			perror("fork");
			exit(1);
	}
}

int main(int argc, char **argv) {
	example3();
}
