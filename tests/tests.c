/* Compile with: 
	gcc -ggdb3 -L$PWD/jsmn/ -ljsmn -o tests tests.c jsmn/libjsmn.a
*/
#include <stdio.h>
#include "jsmn/jsmn.h"
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdint.h>

#define TOKEN_STRING(js, t, s) \
	(strncmp(js+(t).start, s, (t).end - (t).start) == 0 \
	 && strlen(s) == (t).end - (t).start)

#define GAME_STATE_PRINT(gs) \
        printf("moveNumber: %d\nplayerName: %s\nopponentPlayerName: %s\nremainingTimeMsSelf %d\n" \
	       "remainingTimeMsOpponent: %d\nmovesMade: %s\ncurrentMoveSelf: %s\ncurrentMoveOpponent: %s\n", \
                        (gs).moveNumber, (gs).playerName, (gs).opponentPlayerName, (gs).remainingTimeMsSelf, \
			(gs).remainingTimeMsOpponent, (gs).movesMade, (gs).currentMoveOpponent, (gs).currentMoveSelf)
			
typedef struct {
	char status[0x50];
	uint64_t gameID;
	uint32_t moveNumber;
	char playerName[0x50];
	char opponentPlayerName[0x50];
	uint64_t remainingTimeMsSelf;
	uint64_t remainingTimeMsOpponent;
	char movesMade[0x100];
	char currentMoveOpponent[0x3];
	char currentMoveSelf[0x3];
} _CHESS_COM_GAME_SESSION_STATE;

static _CHESS_COM_GAME_SESSION_STATE gameState;

const char * memContains(const char *start, size_t length, const char *needle) {
	const char *end = start + length;

	while (start < end) {
		if (memcmp((const void *)start, (const void *)needle, strlen(needle)) == 0)
			return start;
		++start;
	}
	return NULL;
}
/* 
 * Analyising WebSocket packets is cumbersome, here's a little 
 * function that dumps memory and also tries to output printable
 * ascii chars
 */
void memPrint(const char *start, size_t length, size_t line_length) {
	const char *end = start + length;
	unsigned char buf[17];
	int cnt = 0x10;

	while (start < end) {
		if (cnt % line_length == 0) {
			printf("\n%p: ", start);
		}
		
		printf("%02x ", (unsigned char)*start);
		if ((unsigned char)*start > 0x20 && (unsigned char)*start < 0x7e)
			buf[cnt%line_length] = (char)*start;
		else
			buf[cnt%line_length] = '.';
		buf[(cnt%line_length)+1] = '\0';

		if ((cnt+1)%line_length == 0) printf(" %s", buf);

		++start;
		++cnt;
	}
	// print the remaining ascii representation and padding bytes
	while (cnt++%line_length != 0) {
		printf("   ");
	}
	// and now the final ascii buffer
	printf(" %s\n", buf);
}

void extractData(char *buf) {
	int r;
	jsmn_parser p;
	jsmntok_t tokens[300];

	jsmn_init(&p);
	if ((r = jsmn_parse(&p, buf, tokens, 300)) != JSMN_SUCCESS) {
		fprintf(stderr, "jsmn_parse couldn't parse the token. ErrorValue=%d\n", r);
		return;
	}
	
	int i, selfAlreadyParsed = 0;
	
	for (i = 0; i < 300; i++) {
		if (tokens[i].type == JSMN_STRING) {
			if (TOKEN_STRING(buf, tokens[i], "moves"))
				strncpy(gameState.movesMade, (const char *)buf+tokens[i+1].start, tokens[i+1].end - tokens[i+1].start);
			else if (TOKEN_STRING(buf, tokens[i], "uid")) {
				if (selfAlreadyParsed)
					strncpy(gameState.opponentPlayerName, (const char *)buf+tokens[i+1].start, tokens[i+1].end - tokens[i+1].start);
				else
					strncpy(gameState.playerName, (const char *)buf+tokens[i+1].start, tokens[i+1].end - tokens[i+1].start);
				selfAlreadyParsed = 0x666;
			} else if (TOKEN_STRING(buf, tokens[i], "status"))
				strncpy(gameState.status, (const char *)buf+tokens[i+1].start, tokens[i+1].end - tokens[i+1].start);
			else if (TOKEN_STRING(buf, tokens[i], "id") && TOKEN_STRING(buf, tokens[i-1], "game"))
				gameState.gameID = 0x1;
		}

	}	
}

void testMemContains() {
	char string[] = {'A', 'b', 'r', 'a', 'k', 'a', 'd', 'a', 'b', 'a'};

	if (memContains(string, sizeof(string), "kada") != NULL) printf("contains\n");
	else printf("doesn't contain\n");
}

void testMemPrint() {
	char buffer[] = "\x81\xfe\x00\xdf\x8e\xb3\x72\xfb\xd5\xc8\x50\x98\xe6\xd2\x1c\x95\xeb\xdf\x50\xc1\xac\x9c\x01\x9e\xfc\xc5\x1b\x98\xeb\x9c\x07\x88\xeb\xc1\x50\xd7\xac\xd7\x13\x8f\xef\x91\x48\x80\xac\xde\x1d\x8d\xeb\x91\x48\x80\xac\xd4\x1b\x9f\xac\x89\x45\xcb\xbb\x80\x41\xcd\xb8\x83\x43\xd7\xac\xc0\x17\x8a\xac\x89\x47\xd7\xac\xc6\x1b\x9f\xac\x89\x50\x89\xe1\xd0\x11\x93\xeb\xdd\x50\xd7\xac\xde\x1d\x8d\xeb\x91\x48\xd9\xbf\xe7\x50\xd7\xac\xd0\x1e\x94\xed\xd8\x50\xc1\xba\x81\x45\xd7\xac\xd0\x1e\x94\xed\xd8\x1f\x88\xac\x89\x46\xc9\xb9\x86\x42\xd7\xac\xc0\x03\x8e\xef\xc1\x17\x9f\xac\x89\x06\x89\xfb\xd6\x0f\xd7\xac\xc0\x1b\x9f\xac\x89\x50\x9c\xfd\xd6\x00\x8d\xac\x9f\x50\x8f\xe7\xd7\x50\xc1\xac\xfe\x1d\x8d\xeb\x91\x0f\xd7\xac\xda\x16\xd9\xb4\x91\x44\xc9\xac\x9f\x50\x98\xe2\xda\x17\x95\xfa\xfa\x16\xd9\xb4\x91\x41\x9c\xfe\xc0\x16\xca\xe3\xdd\x41\x95\xeb\xd4\x05\x93\xfd\xd1\x1a\xcf\xe0\x85\x05\x88\xff\x82\x08\x91\xed\xdb\x0b\xce\xac\xce\x2f";

	memPrint(buffer, 230, 16);
}

void testGameDataExtraction() {
	char data[] = "[{\"data\":{\"sid\":\"gserv\",\"game\":{\"id\":704815001,\"status\":\"in_progress\",\"seq\":39,\"players\":[{\"uid\":\"rocchen\",\"status\":\"playing\",\"lag\":5,\"lagms\":527,\"gid\":704815001},{\"uid\":\"Hibou7iles\",\"status\":\"playing\",\"lag\":0,\"lagms\":85,\"gid\":704815001}],\"moves\":\"mu0SltZJclYIbs5Qgm!Tmw90fmWOmvXHtBIAuCJCvC8!CQ45eg6XQX5XlM7YMT0TdN97sCT0CM0MNM\",\"clocks\":[156,444],\"draws\":[],\"squares\":[0,0]},\"tid\":\"GameState\"},\"channel\":\"/game/704815001\"}][{\"id\":\"142\",\"successful\":true,\"channel\":\"/meta/connect\",\"ext\":{\"ack\":66}}]";

	extractData(data);

	GAME_STATE_PRINT(gameState);
}

void testParseWebSocketMessage() {
	
}

int main() {
	//testGameDataExtraction();
	testMemPrint();
	return 0;
}
