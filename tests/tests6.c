#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <unistd.h>
#include <sys/wait.h>
#include <string.h>
#include <stdint.h>

// http://www.gnu.org/software/libc/manual/html_node/Process-Creation-Example.html#Process-Creation-Example
// 0 == stdinput
// 1 == stdout
// 2 == stderr

typedef struct {
	int engineOutput;
	int engineInput;
} ENGINE_STREAM_FDS;

static ENGINE_STREAM_FDS psHandles;
static char *uciCommands[] = {"uci\n", "isready\n", "position startpos moves\n", "go infinite\n", "stop\n", "go movetime 500\n"};
static char movesMade[0x200]; // Algebraic, space separated move notation of the current moves made. Example: "e2e4 e7e5 d2d4 h7h8"

int initStockfish();
void startEngingCalculation();
int getEngineMove();
void sleepMs(uint32_t);

void sleepMs(uint32_t ms) {
	struct timespec ts;
	ts.tv_sec = 0 + (ms / 1000);
	ts.tv_nsec = 1000 * 1000 * (ms % 1000);
	nanosleep(&ts, NULL);
}

/*
 * Start the Stockfish engine and maintain a global handle to it's stdout and stdin pseudo fd's
 */
int initStockfish() {
	int pipe_read[2], pipe_write[2], ch, n;
	char buf[0x2000] = {0};
	pid_t pid;

	if (pipe(pipe_read) != 0) {
		perror("pipe");
		return -1;
	}
		
	if (pipe(pipe_write) != 0) {
		perror("pipe");
		return -1;
	}

	char *cmd[] = {"/home/nikolai/motivational_abyss/playground/nw_analysis/hooklib/Stockfish/src/stockfish"};
	char *execArgs[] = { "stockfish", NULL };

	switch (pid = fork()) {
		case 0: /* child */
			dup2(pipe_write[1], 1); // set the stdout of the child to the write end of the pipe
			dup2(pipe_read[0], 0); // set the stdin to the read end of the pipe
			execvp(cmd[0], execArgs); // execute the uci chess engine in child
			perror(cmd[0]);
			_exit(EXIT_FAILURE);

		default: /* parent */
			// set the pipes fd's to a global variable
			psHandles.engineInput = pipe_read[1];
			psHandles.engineOutput = pipe_write[0];
			// close the fds that we do not need
			close(pipe_read[0]);
			close(pipe_write[1]);
		
			if ((n = read(pipe_write[0], buf, 4049)) >= 0) {
				buf[n] = 0; /* terminate the string */
				if (!strstr(buf, "Stockfish")) {
					fprintf(stderr, "No ``Stockfish`` needle in greeting\n");
					return -1;
				} else
					fprintf(stdout, "Got ``Stockfish`` header\n");
			}
			
			if ((n = write(pipe_read[1], uciCommands[0], strlen(uciCommands[0])) == -1))
				perror("write()");
				
			sleepMs(100);
			
			if ((n = read(pipe_write[0], buf, 4049)) >= 0) {
				buf[n] = 0;
				if (!strstr(buf, "uciok")) {
					fprintf(stderr, "No uciok\n");
					return -1;
				} else
					fprintf(stdout, "Got uciok\n");
			}
			
			if ((n = write(pipe_read[1], uciCommands[1], strlen(uciCommands[1])) == -1))
				perror("write()");
				
			sleepMs(100);
				
			if ((n = read(pipe_write[0], buf, 100)) >= 0) {
				buf[n] = 0;
				if (!strstr(buf, "readyok")) {
					fprintf(stderr, "No readyok\n");
					return -1;
				} else
					fprintf(stdout, "Got readyok\n");
			}
			
			if ((n = write(pipe_read[1], uciCommands[2], strlen(uciCommands[2])) == -1))
				perror("write()");
			
			sleepMs(500);
			
			if ((n = write(pipe_read[1], uciCommands[3], strlen(uciCommands[3])) == -1))
				perror("write()");
				
			printf("Calculating move\n");
			sleepMs(2000);
			
			if ((n = write(pipe_read[1], uciCommands[4], strlen(uciCommands[4])) == -1))
				perror("write()");
				
			sleepMs(100);
				
			if ((n = read(pipe_write[0], buf, 0x2000)) >= 0) {
				buf[n] = 0; /* terminate the string */
				char *ptr;
				if (!(ptr = strstr(buf, "bestmove "))) {
					fprintf(stderr, "No bestmove in %s\n", buf);
					return -1;
				} else {
					fprintf(stdout, "Best move is ``%s``\n", ptr);
					// add move to gameState
					strncat(movesMade, ptr+strlen("bestmove "), 5); // move is always 4 bytes (???)
				}
			}
			return 1; //success
			
		case -1:
			perror("fork");
			exit(1);
	}
}

char * quickMove(const char *previousMoves) {
	int pipe_read[2], pipe_write[2], ch, n;
	char buf[0x2000] = {0};
	pid_t pid;

	if (pipe(pipe_read) != 0) {
		perror("pipe");
		return NULL;
	}
		
	if (pipe(pipe_write) != 0) {
		perror("pipe");
		return NULL;
	}

	char *cmd[] = {"/home/nikolai/motivational_abyss/playground/nw_analysis/hooklib/Stockfish/src/stockfish"};
	char *execArgs[] = { "stockfish", NULL };

	switch (pid = fork()) {
		case 0: /* child */
			dup2(pipe_write[1], 1); // set the stdout of the child to the write end of the pipe
			dup2(pipe_read[0], 0); // set the stdin to the read end of the pipe
			execvp(cmd[0], execArgs); // execute the uci chess engine in child
			perror(cmd[0]);
			_exit(EXIT_FAILURE);

		default: /* parent */
			// set the pipes fd's to a global variable
			psHandles.engineInput = pipe_read[1];
			psHandles.engineOutput = pipe_write[0];
			// close the fds that we do not need
			close(pipe_read[0]);
			close(pipe_write[1]);
		
			if ((n = read(pipe_write[0], buf, 4049)) >= 0) {
				buf[n] = 0; /* terminate the string */
				if (!strstr(buf, "Stockfish")) {
					fprintf(stderr, "No ``Stockfish`` needle in greeting\n");
					return NULL;
				} else
					fprintf(stdout, "Got ``Stockfish`` header\n");
			}
			
			if ((n = write(pipe_read[1], uciCommands[0], strlen(uciCommands[0])) == -1))
				perror("write()");
				
			sleepMs(30);
			
			if ((n = read(pipe_write[0], buf, 4049)) >= 0) {
				buf[n] = 0;
				if (!strstr(buf, "uciok")) {
					fprintf(stderr, "No uciok\n");
					return NULL;
				} else
					fprintf(stdout, "Got uciok\n");
			}
			
			if ((n = write(pipe_read[1], uciCommands[1], strlen(uciCommands[1])) == -1))
				perror("write()");
				
			sleepMs(30);
				
			if ((n = read(pipe_write[0], buf, 100)) >= 0) {
				buf[n] = 0;
				if (!strstr(buf, "readyok")) {
					fprintf(stderr, "No readyok\n");
					return NULL;
				} else
					fprintf(stdout, "Got readyok\n");
			}
			
			sleepMs(30);
			
			if (previousMoves != NULL && strlen(previousMoves) != 0) {
				char cmdBuf[0x500] = {0};
				sprintf(cmdBuf, "position startpos moves %s\n", previousMoves);
				if ((n = write(pipe_read[1], cmdBuf, strlen(cmdBuf)) == -1))
					perror("write()");
			} else {
				if ((n = write(pipe_read[1], uciCommands[2], strlen(uciCommands[2])) == -1))
					perror("write()");
			}
			
			sleepMs(30);
			
			if ((n = write(pipe_read[1], uciCommands[5], strlen(uciCommands[5])) == -1))
				perror("write()");
				
			printf("Calculating move\n");
			sleepMs(600);
			
			if ((n = write(pipe_read[1], uciCommands[4], strlen(uciCommands[4])) == -1))
				perror("write()");
				
			sleepMs(50);
				
			if ((n = read(pipe_write[0], buf, 0x2000)) >= 0) {
				buf[n] = 0; /* terminate the string */
				char *ptr;
				if (!(ptr = strstr(buf, "bestmove "))) {
					fprintf(stderr, "No bestmove in %s\n", buf);
					return NULL;
				} else {
					fprintf(stdout, "Best move is ``%s``\n", ptr);
					int cnt = 0; char *helpPtr = (char *)ptr+strlen("bestmove ");
					while (*helpPtr++ != ' ')
						cnt++;
					
					char *retVal = malloc(cnt+1);
					strncpy(retVal, (char *)ptr+strlen("bestmove "), cnt+1); // move is always 4 bytes, except by promotions
					return retVal;
				}
			}
			return NULL; //success
			
		case -1:
			perror("fork");
			exit(1);
	}
}

/* 
 * Send the appropirate uci command to start the engine computation of the current move. This function assumes that there is already 
 * Stockfish running as some process and is therefore not responsible for setting up the engine. This functions assumes that the
 * process has some time to do actually calculate for moves.
 */
void startEngingCalculation() {
	int n;
	
	char buf[0x200] = {0};
	char *cmdFmt = "position startpos moves %s\n";
	sprintf(buf, cmdFmt, movesMade);
	
	if ((n = write(psHandles.engineInput, buf, strlen(buf)) == -1))
		perror("write()");
	
	// sleep for a little time
	sleepMs(200);

	// let em calculate for 500 ms
	if ((n = write(psHandles.engineInput, uciCommands[5], strlen(uciCommands[5])) == -1))
		perror("write()");
}

/* 
 * Stop the current engine calculations and obtain the best move...
 */
int getEngineMove() {
	// sleep for a little time
	char buf [0x2000] = {0};
	int n;
	
	sleepMs(500);
	
	if ((n = write(psHandles.engineInput, uciCommands[4], strlen(uciCommands[4])) == -1))
		perror("write()");
		
	sleepMs(100);
		
	if ((n = read(psHandles.engineOutput, buf, 0x2000)) >= 0) {
		char *ptr;
		if (!(ptr = strstr(buf, "bestmove "))) {
			fprintf(stderr, "No bestmove in %s\n", buf);
			return -1;
		} else {
			fprintf(stdout, "[!] Got move from engine ``%s``\n", ptr);
			// Bestmove is the chars after "bestmove " until a white space
			// is encountered. The bestmove format is something like the following:
			// "bestmove f5f8 ponder d8f8\n"
			int cnt = 0; char *helpPtr = (char *)ptr+strlen("bestmove ");
			while (*helpPtr++ != ' ')
				cnt++;
			
			strncat(movesMade, (char *)ptr+strlen("bestmove "), cnt+1); // move is always 4 bytes, except by promotions
		}
	}
}

// Promt the user to make a move
void makeMove() {
	char move[0x5] = {0}; // move is 4 bytes plus nullbyte
	int n;
	
	fflush(stdout);
	fflush(stdin);
	printf("\nEnter a move now (4 chars like: `f7f5`) ");
	gets(move);
	move[4] = ' ';
		
	// add move to gameState
	strncat(movesMade, move, 5); // move is always 4 bytes (???)
}

int main(int argc, char *argv[]) {
	/*memset(movesMade, 0x0, 0x200);
	initStockfish();
	while (1) {
		printf("\nMoves made: %s\n", movesMade);
		makeMove();
		startEngingCalculation();
		getEngineMove();
	}
	*/
	printf("Quick Move: %s\n", quickMove("e2e4 e7e5    "));

	return 0;
}
