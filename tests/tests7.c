#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

void extractMove(const char *data, char *moves) {
	int cnt = 0; char *helpPtr = (char *)data+strlen("bestmove ");
	while (*helpPtr++ != ' ')
		cnt++;
	
	strncat(moves, (char *)data+strlen("bestmove "), cnt+1); // move is always 4 bytes, except by promotions
}


int main() {
	char *data[] = {"bestmove g7h8q ponder d8e7\n", "bestmove e7g8r ponder d8e7\n", "bestmove g6g7 ponder f6h5\n", "bestmove d1h5 ponder f8g7"};
	char buf[0x1000];
	int i;
	
	for (i = 0; i < 4; i++)
		extractMove(data[i], buf);
		
	printf("Moves made: %s\n", buf);

	return 0;
	
}
