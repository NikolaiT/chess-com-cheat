#include <unistd.h>
#include <time.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

void sleepMs(uint32_t ms) {
	struct timespec ts;
	ts.tv_sec = 0 + (ms / 1000);
	ts.tv_nsec = 1000 * 1000 * (ms % 1000);
	nanosleep(&ts, NULL);
}

int main(int argc, char *argv[]) {
	long int cnt = 0;
	char buf[0x10] = {0};
	
	while (1) {
		sleepMs(100);
		sprintf(buf, "%ld\n", ++cnt);
		if (write(STDOUT_FILENO, buf, strlen(buf)) == -1)
			perror("write");
		fflush(stdout);
	}
}
