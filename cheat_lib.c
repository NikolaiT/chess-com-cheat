/* 
 * This little program hooks into firefox's PR_write() and PR_read() function and modifies chess.com moves
 * as obtained by live.chess.com by piping them into a local Stockfish chess engine. It then substitues
 * the move to be sent with the one from the engine. It's specifically designed for bullet games.
 *
 * This works (of course) only for firefox browsers on Linux systems. I plan to also make a Windows version, such 
 * that sneaky little skids can have their fun! :)
 * 
 * It's (in my humble opinion) a straighforward approach to make browser game bots, because we just need to reverse the the 
 * game protocol and inject here and there slight modifications in order to gain an advantage :P
 * Edit after actually implementing the idea: WTF! This is not a straightforward approach :/
 *
 * The same principle could be applied to poker bots, go bots, ...
 * 
 * Inspect LiveChessCore.js, at line 8577
 *
 * Basic algorithm:
 * 	1. The shared library (let's call it libpwh.so) is dynamically loaded in a firefox process with the 
 * 	   LD_PRELOAD trick. For instance: 'export LD_PRELOAD=$PWD/libpwh.so; /usr/bin/firefox'
 * 	2. The functions PR_Read() and PR_Write() are hooked.
 * 	3. As soon as a WebSocket request that indicates the beginning of a new live.chess.com game is detected, 
 * 	   libpwh.so initializes the local Stockfish engine with the function int initStockfish() {There should be enough time before the game actually begins}.
 *  	3.1 When a JSON message like the following
 * 				�~�[{"data":{"sid":"gserv","game":{"id":706548893,"status":"starting","seq":0,"players":[{"uid":"rocchen","status":"playing","lag":4,"lagms":415,"gid":706548893},{"uid":"workcentre7328","status":"playing","lag":2,"lagms":210,"gid":706548893}],"abortable":[true,true],"moves":"","clocks":[600,600],"draws":[],"repeated":true,"squares":[0,0]},"tid":"GameState"},"channel":"/game/706548893"}]�~
 * 			is intercepted, a game has begun. The needles that indicate such a beginning are thus my UID and the string | "status":"starting" |
 *  4. From now on, every outgoing packet (from PR_Write()), that causes a move, must be updated with a
 *     chess engine move. This is done with with function void modifyMove(), which in turns relies on a correct gameState. Such a outbound packet looks like:
 * 				[{"channel":"/service/user","data":{"move":{"gid":706662190,"seq":11,"uid":"rocchen","move":"9I","clock":76,"clockms":7661,"squared":false},"sid":"gserv","tid":"Move"},"id":"105","clientId":"4gzikii6gu01a1k7kvdcb7a1h2gh6"}]
 *  5. Concurrently to step 4, the move made by the opponent is synchronized with a local gameState struct variable. The opponent's move is 
 *     obtained in PR_Read() and the function void collectGameState() keeps the move history current.
 *
 * Compile with:
 *	gcc -Wall -shared -ggdb3 -fPIC -ldl -ljsmn -L$PWD/jsmn/ -o libpwh.so cheat_lib.c jsmn/jsmn.c
 * Sniff packets with:
 * 	tcp.port == 80 or tcp.port == 443 and (ip.dst == 67.201.34.165 or ip.dst == 192.168.178.48)
 *	
 * Date: January 2014
 * Author: Nikolai Tschacher
 * Contact: incolumitas.com
 */
#define _GNU_SOURCE

#include <stdio.h>
#include <dlfcn.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdint.h>
#include <netinet/in.h>
#include "jsmn/jsmn.h"
#include <time.h>

// set the unique user id here! This token must identify your player! This must be set right, otherwise the game packets
// cant be properly identified.
#define PLAYER_UID "EatingSpiders"
#define LOG_FILE "/tmp/hook.log"

/* Bash colors */
#define SH_RED 31
#define SH_GREEN 32
#define SH_YELLOW 33
#define SH_BLUE 34

// Defines the debug level. 0 = log nothing, 1 = log the bare minimum, 2 = log message headers, important key points and the like.
#define DEBUG_LEVEL 2

typedef int PRInt32;
typedef struct PRFileDesc PRFileDesc;

#define INFO_PRINT(s, c) \
	printf("[!] \e[%dm %s \e[0m\n", c, s);

#define TOKEN_STRING(js, t, s) \
	(strncmp(js+(t).start, s, (t).end - (t).start) == 0 \
	  && strlen(s) == (t).end - (t).start)

#define GAME_STATE_PRINT(gs, func) \
        printf("-------------------- \e[31mModified by %s\e[0m --------------------\n\e[%dmCurrent Game state:\e[0m\n\tStatus: %s\n\tmoveNumber: %d\n\tplayerName: %s\n\topponentPlayerName: %s\n\tremainingTimeMsSelf %lld\n\t" \
	       "remainingTimeMsOpponent: %lld\n\tmovesMade: %s\n\tmovesMadeDecoded: %s\n\tcurrentMoveSelf: %s\n\tcurrentMoveSelfDecoded: %s\n\tcurrentMoveOpponent: %s\n\tcurrentMoveOpponentDecoded: %s\n\tengineMoves: %s\n\tengineSuggestion: \e[31m %s \e[0m\n\tLagMsSelf: %u\n\tLagMsOpponent: %u\n\t\n------------------------\n", \
                        func, SH_BLUE, (gs).status, (gs).moveNumber, (gs).playerName, (gs).opponentPlayerName, (unsigned long long)(gs).remainingTimeMsSelf, \
			(unsigned long long)(gs).remainingTimeMsOpponent, (gs).movesMade, (gs).movesMadeDecoded, (gs).currentMoveSelf, (gs).currentMoveSelfDecoded, (gs).currentMoveOpponent, (gs).currentMoveOpponentDecoded, (gs).engineMoves, (gs).engineSuggestion, (gs).lagMsSelf, (gs).lagMsOpponent)
			
#define WS_HEADER_PRINT(ws_h, s) \
			printf("[%s]: Information about WebSocketMessage:\n\tFlags: FIN(%d), RSV1(%d), RSV2(%d), RSV3(%d), OPCODE(%d), MASK(%d)\n\tMessageLength: %zd\n\tPayloadOffset: %zd\n\tPayloadLength: %zd\n\tMask: 0x%x\n", \
				s, isFinal(ws_h), ws_h->bits.RSV1, ws_h->bits.RSV2, ws_h->bits.RSV3, getOpCode(ws_h), isMasked(ws_h), getMessageLength(ws_h), getPayloadOffset(ws_h), getPayloadLength(ws_h), getMask(ws_h));

// This struct holds the current game state and all kids of game information as parsed and extracted of the live session
typedef struct {
	char status[0x50]; // The status as defined by the protocol. Will be most likely 'playing' during a game.
	uint64_t gameID; // The global game id. Can be looked up after games to review the game.
	uint32_t moveNumber; // The number of moves I did
	char playerName[0x50]; // Me
	char opponentPlayerName[0x50]; // Poor opponent
	uint64_t remainingTimeMsSelf; // yeah does that matter?
	uint64_t remainingTimeMsOpponent; // probably more relevant
	uint16_t lagMsSelf; // i have a stupid connection here in my house
	uint16_t lagMsOpponent;
	char movesMade[0x100]; // in live.chess.com encoded version
	char movesMadeDecoded[0x400]; // the decoded version
	char currentMoveOpponent[0x3]; // last move of opponent
	char currentMoveSelf[0x3]; // my last move
	char currentMoveOpponentDecoded[0x6];
	char currentMoveSelfDecoded[0x6];
	char engineMoves[0x100];
	char engineSuggestion[0x6]; // The best move as suggested by the engine
} _CHESS_COM_GAME_SESSION_STATE;

// declare a global variable that holds the game state
static _CHESS_COM_GAME_SESSION_STATE gameState;

// This structure holds the connection state of the WebSocket status
typedef struct {
	unsigned short state; // bitfield, see flag definitions below
	
} _WEB_SOCKET_STATE;

#define WEB_SOCKET_CONNECTION_REQUESTED 1
#define WEB_SOCKET_CONNECTION_ACCEPTED  2
#define WEB_SOCKET_CONNECTION_REFUSED   4

// declare a global struct variable that holds the WebSocket state
static _WEB_SOCKET_STATE webSocketState;

// flag to indicate that we could read some bites
static int couldRead = 0;

// This struct holds the header of a WebSocket message
typedef struct {
  union {
    struct {
      unsigned int OP_CODE : 4;
      unsigned int RSV1 : 1;
      unsigned int RSV2 : 1;
      unsigned int RSV3 : 1;
      unsigned int FIN : 1;
      unsigned int PAYLOAD : 7;
      unsigned int MASK : 1;
    } bits;
    uint16_t short_header;
  };
} WebSocketMessageHeader;

// To control the engine
typedef struct {
	int engineOutput;
	int engineInput;
} ENGINE_STREAM_FDS;

static ENGINE_STREAM_FDS psHandles;
static char *uciCommands[] = {"uci\n", "isready\n", "position startpos moves\n", "go infinite\n", "stop\n", "go movetime 800\n"}; // UCI commands

// lookup tables for move encoding/decoding
// first come all possible regular squares (a1-a8...h1-h8). then there are 12 keys to indicate the different promotion possibilites:
// Promotion to queen while
// 		- capturing the left square: '{'
// 		- moving pawn one square forward: '~'
// 		- capturing the right square: '}'
// Promotion to knight while
// 		- capturing the left square: '('
// 		- moving pawn one square forward: '^'
// 		- capturing the right square: ')'
// Promotion to rook while
// 		- capturing the left square: '['
// 		- moving pawn one square forward: '_'
// 		- capturing the right square: ']'
// Promotion to bishop while
// 		- capturing the left square: '@'
// 		- moving pawn one square forward: '#'
// 		- capturing the right square: '$'
static const char *decoded[] = {"a4", "a3", "g8", "c7", "f8", "a8", "g5", "c8", "g7", "g6", "g1", "g3", "g2", "c1", "a6", "c3", "c2", "c5", "c4", "a1", "c6", "e8", "e3", "e2", "e1", "e7", "e6", "e5", "e4", "h8", "d1", "g4", "h1", "h2", "h3", "h4", "f4", "h6", "h7", "a2", "d6", "f6", "f7", "b8", "f5", "f2", "f3", "f1", "b2", "b3", "b1", "b6", "b7", "b4", "b5", "a7", "d8", "d4", "d5", "h5", "d7", "a5", "d2", "d3"};
static const char encoded[] = {'y', 'q', '!', 'Y', '9', '4', 'M', '6', '2', 'U', 'g', 'w', 'o', 'c', 'O', 's', 'k', 'I', 'A', 'a', 'Q', '8', 'u', 'm', 'e', '0', 'S', 'K', 'C', '?', 'd', 'E', 'h', 'p', 'x', 'F', 'D', 'V', '3', 'i', 'R', 'T', '1', '5', 'L', 'n', 'v', 'f', 'j', 'r', 'b', 'P', 'X', 'z', 'H', 'W', '7', 'B', 'J', 'N', 'Z', 'G', 'l', 't'};

//The ones to hook
PRInt32 PR_Write(PRFileDesc *fd, const void *buf, PRInt32 amount); // Hook to modify live.chess.com moves
PRInt32 PR_Read(PRFileDesc *fd, void *buf, PRInt32 amount); // Hook to obtain the moves made by the sneaks opponent and collect game state information :)

int initStockfish();//Start the Stockfish engine and maintain a handle to it's stdout
void startEngingCalculation();// Start calculating based on the gameState
int getEngineMove();// Get the engine move. Called by PR_Write().
char * quickMove(const char *previousMoves); //dirty approach, because the spawned child process seems to terminate for some reason
void sleepMs(uint32_t);

void collectGameState(char *buf, size_t length); // Obtain and extract the game state of the current chess session
void modifyMove(char *buf, size_t length); // modify the move as given by the chess engine
void logStdout(const char *msg, size_t bufLength, const char *symbol);
void logFile(const char *msg, size_t bufLength, const char *symbol);
void memPrint(const char *start, size_t length, size_t line_length);
const char * memContains(const char *start, size_t length, const char *needle); // A little (bad because slow) function that scans the buffer for a needle. String functions fail, because we don't deal with strings (0 terminated memory)

// function definitions for WebSocket protocols
size_t getMessageLength(WebSocketMessageHeader *);
uint8_t getOpCode(WebSocketMessageHeader *);
int isFinal(WebSocketMessageHeader *);
int isMasked(WebSocketMessageHeader *);
size_t getPayloadOffset(WebSocketMessageHeader *);
size_t getPayloadLength(WebSocketMessageHeader *);
uint32_t getMask(WebSocketMessageHeader *);
WebSocketMessageHeader * initWS_Header(char *, size_t);
void freeWS_Header(WebSocketMessageHeader *);
void messageDecode(char *, uint32_t, uint32_t );
void printDecodedMessage(char *, size_t);

// function to hande the move encryption of live.chess.com
size_t getIndex(char *, char);
void decodeMoves(const char* , char *);
void encodeMoves(const char* , char* );

PRInt32 PR_Write(PRFileDesc *fd, const void *buf, PRInt32 amount) {
	static PRInt32 (*my_PR_write)(PRFileDesc *, const void *, PRInt32) = NULL;
	
	// Detect the client WebSocket connection attempt
	if (!(webSocketState.state & WEB_SOCKET_CONNECTION_REQUESTED) && amount > 600 && amount < 1200 && memContains((const char *)buf, amount, "Upgrade: websocket") 
		&& memContains((const char *)buf, amount, "Origin: http://live.chess.com") && memContains((const char *)buf, amount, "Sec-WebSocket-Key:") &&
			 memContains((const char *)buf, amount, "Sec-WebSocket-Version: 13")) {
		webSocketState.state |= WEB_SOCKET_CONNECTION_REQUESTED; 
#if DEBUG_LEVEL >= 1
		INFO_PRINT("Chess.com WebSocket client request detected", SH_RED);
#endif
		// Also start up the Stockfish engine
		//INFO_PRINT("Starting the engine...", SH_BLUE);
		//initStockfish();
	}
	/*
	 * When the WebSocket is open sniff for move packets that are between 230 and 240 bytes in size.
	 * I observed that these synchronize with moves made and hence I assume these packetses transmit moves.
	 * Now I just need do decrypt them and good is.
	 */
	if ((webSocketState.state & WEB_SOCKET_CONNECTION_REQUESTED) && amount > 210 && amount < 260) {
#if DEBUG_LEVEL >= 2
		INFO_PRINT("PR_Write() called and buffer size suggests that this packet represents a move!", SH_RED);
#endif
		// Inject a engine made move and update the game state
		modifyMove((char *)buf, (size_t)amount);
	}

	my_PR_write = dlsym(RTLD_NEXT, "PR_Write");

	if (my_PR_write == NULL) {
		fprintf(stderr, "dlsym() failed for PR_Write\n");
		exit(EXIT_FAILURE);
	}

	PRInt32 retVal = (*my_PR_write)(fd, buf, amount);

	return retVal;
}

PRInt32 PR_Read(PRFileDesc *fd, void *buf, PRInt32 amount) {
	static PRInt32 (*my_PR_read)(PRFileDesc *, void *, PRInt32);

	/* 
	 * There is a hell of a lot data that passes through this function. Hence we only want
	 * to consume as few as possible processing power. We are only interested in data that
	 * is in a specfic length range, since it seems that the WebSocket packets on live.chess.com
	 * use pretty regular packet size.
     */

	/* 
	 * Detect the WebSocket handshake response from the server
	 * Doesn't work for some reason. It seems like there's just no such packet in the buffer :/
	 */
	/*
	if ((webSocketState.state & WEB_SOCKET_CONNECTION_REQUESTED) && amount > 10 memContains((const char *)buf, amount, "HTTP/1.1 101 Switching Protocols")
		&& memContains((const char *)buf, amount, "Sec-WebSocket-Accept:")) {
		webSocketState.state |= WEB_SOCKET_CONNECTION_ACCEPTED;
		INFO_PRINT("Chess.com WebSocket Server Handshake response detected", SH_RED);
		memPrint((const char *)buf, amount, 16);
	}*/

	/* 
     * Sniff for incoming live.chess.com WebSocket move data.
	 */
	if ((webSocketState.state & WEB_SOCKET_CONNECTION_REQUESTED) && amount > 50 && memContains((const char *)buf, amount, "moves") != NULL) {
		couldRead = 1;
		collectGameState((char *)buf, amount);
#if DEBUG_LEVEL >= 2
		INFO_PRINT("PR_Read() called with 'moves' keyword inside", SH_BLUE);
#endif
	}
	
	my_PR_read = dlsym(RTLD_NEXT, "PR_Read");
	
	if (my_PR_read == NULL) {
		fprintf(stderr, "dlsym() failed for PR_Write\n");
		exit(EXIT_FAILURE);
	}

	PRInt32 retVal = (*my_PR_read)(fd, buf, amount);

	return retVal;
}

void modifyMove(char *buf, size_t length) {
	int r, i = 0;
	jsmn_parser p;
	size_t numTokens = 0x100; //There are normally not more tokens in move message
	jsmntok_t tokens[numTokens];
	WebSocketMessageHeader* header;
	
	// Try to parse the WebSocket message
	header = initWS_Header(buf, length);
#if DEBUG_LEVEL >= 2
	WS_HEADER_PRINT(header, "Printing in modifyMove()");
#endif

	// if the message looks odd, get the hell out
	if (!isMasked(header) && !isFinal(header) && !(getOpCode(header) == 1)) {
		INFO_PRINT("Leaving modifyMove() because it is a bad WebSocket Message", SH_BLUE);
		goto end;
	}
	
	uint32_t mask = getMask(header);
	size_t offset = getPayloadOffset(header);
	
	// Decode the outbound message
	messageDecode((char *)(buf+offset), getPayloadLength(header), mask);
#if DEBUG_LEVEL >= 2
	// Print the clear text message
	INFO_PRINT("Decoded PR_Write() message is: ", SH_YELLOW);
	printf("\n%s\n", (char *)(buf+offset));
#endif
	// Obtain a pointer to the message payload
	char *payloadPtr = (buf+offset);
	
	// try to update the state of the game
	jsmn_init(&p);
	if ((r = jsmn_parse(&p, payloadPtr, tokens, numTokens)) != JSMN_SUCCESS) {
#if DEBUG_LEVEL >= 2
		fprintf(stderr, "jsmn_parse couldn't properly parse the token in modifyMove. ErrorValue=%d\n", r); // still try to
#endif
	}
	// Synchronise the gameState
	for (i = 0; i < numTokens; i++) {
		if (tokens[i].type == JSMN_STRING) {
			// Get the move I am trying to do
			if (TOKEN_STRING(payloadPtr, tokens[i], "uid") &&
			      TOKEN_STRING(payloadPtr, tokens[i+1], PLAYER_UID) &&
			      TOKEN_STRING(payloadPtr, tokens[i+2], "move")) {
				//increment the move count
				gameState.moveNumber++;
				// Set the current move the player actually made
				memset(&gameState.currentMoveSelf, 0, 0x3);
				strncpy(gameState.currentMoveSelf, (const char *)payloadPtr+tokens[i+3].start, tokens[i+3].end - tokens[i+3].start);
				// Also set the decoded move
				char _moveDecoded[0x7] = {0};
				decodeMoves(gameState.currentMoveSelf, _moveDecoded);
				strncpy(gameState.currentMoveSelfDecoded, _moveDecoded, strlen(_moveDecoded));
				// Sync remainingTimeMsSelf
				gameState.remainingTimeMsSelf = atol((const char *)payloadPtr+tokens[i+7].start);
				// Get the current engine suggestion
				//getEngineMove();
				if (strlen(gameState.movesMadeDecoded) != 0) {
					char *move = quickMove(gameState.movesMadeDecoded);
#if DEBUG_LEVEL >= 1
					printf("!!!Engine found move: \e[35m %s \e[0m while I made move \e[35m %s \e[0m\n", move, gameState.currentMoveSelfDecoded);
#endif
					char bufEncodedMove[0x5] = {0};
					encodeMoves(move, bufEncodedMove);
					// Let's update the message with the engine suggestion
					memcpy((void *)(payloadPtr+tokens[i+3].start), (const void *)bufEncodedMove, tokens[i+3].end - tokens[i+3].start);
					// That was all! Magic!
					free(move);
				}
			}
		}
	}
#if DEBUG_LEVEL >= 1
	// Print the current game state when updated
	GAME_STATE_PRINT(gameState, "modifyMove from PR_Write()");
#endif
	
	// Reencode the message before leaving
	messageDecode((char *)(buf+offset), getPayloadLength(header), mask);
	
end:
	// cleanup
	freeWS_Header(header);

	return;
}

void collectGameState(char *buf, size_t length) {
	int r;
	jsmn_parser p;
	size_t numTokens = 0x200; //Should be enough
	jsmntok_t tokens[numTokens];
	WebSocketMessageHeader* header;
	
	// First of all, parse the WebSocket message
	header = initWS_Header(buf, length);	
	size_t offset = getPayloadOffset(header);
	size_t messageLength = getMessageLength(header);
	
	// Allocate a buffer on the stack for the text message
	char messageBuf[messageLength];
	
	// if the buffer isn't good looking, drop it
	if (isMasked(header) && !isFinal(header) && !(getOpCode(header) == 1))
		goto end;
	
	// Obtain a pointer to the message payload
	char *payloadPtr = (buf+offset);
	// Copy the message
	memcpy(messageBuf, payloadPtr, messageLength);
	// make it a NULL terminated string for the jsmn parser!
	messageBuf[messageLength-1] = '\0';
#if DEBUG_LEVEL >= 2
	// Print the clear text message
	printf("Parsed message from PR_Read() in collectGameState\n%s\n", messageBuf);
#endif
	
	// only continue and try to parse if the message has "status":"in_progress" set or "status":"starting"
	// such a needle in the message indicates that the packet is part of a game (Either observing or playing on my own).
	if (!(strstr(messageBuf, "\"status\":\"in_progress\"") || strstr(messageBuf, "\"status\":\"starting\""))) {
#if DEBUG_LEVEL >= 2
		INFO_PRINT("Leaving collectGameState() because message does not contain status `in_progress` or `starting`", SH_BLUE);
#endif
		goto end;
	}
		
	// Additonally, if the "status":"starting" and my UID is in the message, reset all gameState fields!
	if (strstr(messageBuf, "\"status\":\"starting\"") && strstr(messageBuf, PLAYER_UID)) {
#if DEBUG_LEVEL >= 1
	INFO_PRINT("Resetting gameState because game start was detected!", SH_GREEN);
#endif
		memset(&gameState, 0, sizeof(_CHESS_COM_GAME_SESSION_STATE));
	}
	
	jsmn_init(&p);
	if ((r = jsmn_parse(&p, messageBuf, tokens, numTokens)) != JSMN_SUCCESS) {
#if DEBUG_LEVEL >= 2
		fprintf(stderr, "jsmn_parse couldn't properly parse the token in collectGameState. ErrorValue=%d\n", r); // still try to
#endif
	}
	
	int i, firstPlayerDone = 0;
	
	for (i = 0; i < numTokens; i++) {
		if (tokens[i].type == JSMN_STRING) {
			if (TOKEN_STRING(messageBuf, tokens[i], "moves")) {
				memset(&gameState.movesMade, 0, 0x100);
				memset(&gameState.movesMadeDecoded, 0, 0x400);
				strncpy(gameState.movesMade, (const char *)messageBuf+tokens[i+1].start, tokens[i+1].end - tokens[i+1].start);
				// Also set decode the moves
				char _movesDecoded[0x400] = {0};
				decodeMoves(gameState.movesMade, _movesDecoded);
				strncpy(gameState.movesMadeDecoded, _movesDecoded, strlen(_movesDecoded));
				// if the last move in the moves string is not equal to my last move, then this must be the opponent's move!
				// This is the earliest possible moment to know what move the opponent did. Base on his move, we ask the engine to start calculating!
				char lastMove[0x3] = {0};
				strncpy(lastMove, (char *)(gameState.movesMade + (strlen(gameState.movesMade)-2)), 0x3);
				if (strncmp((char *)(lastMove), gameState.currentMoveSelf, 2) != 0) {
					memset(&gameState.currentMoveOpponent, 0, 0x3);
					memset(&gameState.currentMoveOpponentDecoded, 0, 0x6);
					
					strncpy(gameState.currentMoveOpponent, lastMove, 0x3);
					// Also set the decoded move
					char _moveDecoded[0x7] = {0};
					decodeMoves(lastMove, _moveDecoded);
					_moveDecoded[0x7] = 0;
					strncpy(gameState.currentMoveOpponentDecoded, _moveDecoded, strlen(_moveDecoded));
					// Ask the engine to start calculating to response the opponent's move on next PR_Write()
					if (strlen(gameState.movesMadeDecoded) != 0) {
						//startEngingCalculation();
					}
				}
					
			} else if (TOKEN_STRING(messageBuf, tokens[i], "uid")) {
				if (!TOKEN_STRING(messageBuf, tokens[i+1], PLAYER_UID)) { // if this uid is not ours
					if (firstPlayerDone) { // set the next player, when observering
						memset(&gameState.playerName, 0, 0x50);
						strncpy(gameState.playerName, (const char *)messageBuf+tokens[i+1].start, tokens[i+1].end - tokens[i+1].start);
						gameState.lagMsSelf = atoi((const char *)messageBuf+tokens[i+7].start);
					} else {
						// Set the opponent player
						memset(&gameState.opponentPlayerName, 0, 0x50);
						strncpy(gameState.opponentPlayerName, (const char *)messageBuf+tokens[i+1].start, tokens[i+1].end - tokens[i+1].start);
						gameState.lagMsOpponent = atoi((const char *)messageBuf+tokens[i+7].start); // 7 tokens to the right
						firstPlayerDone = 0x666;
					}
				} else { // its certainly us (when we play)
					memset(&gameState.playerName, 0, 0x50);
					strncpy(gameState.playerName, (const char *)messageBuf+tokens[i+1].start, tokens[i+1].end - tokens[i+1].start);
					gameState.lagMsSelf = atoi((const char *)messageBuf+tokens[i+7].start);
				}
			} else if (TOKEN_STRING(messageBuf, tokens[i], "status")) {
				memset(&gameState.status, 0, 0x50);
				strncpy(gameState.status, (const char *)messageBuf+tokens[i+1].start, tokens[i+1].end - tokens[i+1].start);
			} else if (TOKEN_STRING(messageBuf, tokens[i], "id") && TOKEN_STRING(buf, tokens[i-1], "game"))
				gameState.gameID = atol((const char *)messageBuf+tokens[i+1].start);
		}
	}
#if DEBUG_LEVEL >= 1
	// Print the current game state when updated
	GAME_STATE_PRINT(gameState, "collectGameState() from PR_Read()");
#endif
	
end:
	// cleanup
	freeWS_Header(header);

	return;
}

void logStdout(const char *msg, size_t bufLength, const char *symbol) {
	fprintf(stdout, "Function %s with PID %d contains %u bytes: \n\n%s\n", symbol, getpid(), (unsigned int)bufLength, msg);
	fprintf(stdout, "----------------------------------------\n");
}

/* Little usage function that logs to logfile */
void logFile(const char *msg, size_t bufLength, const char *symbol) {
	FILE *fd;
	
	fd = fopen(LOG_FILE, "a+");

	if (fd == 0) {
		perror("fopen() failed");
		exit(EXIT_FAILURE);
	}

	fprintf(fd, "Function \e[0;31m %s\e[m with PID %d contains %u bytes: \n\n%s\n", symbol, getpid(), (unsigned int)bufLength, msg);
	fprintf(fd, "----------------------------------------\n");
	fclose(fd);
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

const char * memContains(const char *start, size_t length, const char *needle) {
	const char *end = start + length;

	while (start < end) {
		if (memcmp((const void *)start, (const void *)needle, strlen(needle)) == 0)
			return start;
		++start;
	}
	return NULL;
}

// I guess each message has the size of its payload plus the offset to its payload
size_t getMessageLength(WebSocketMessageHeader *header) {
	return (getPayloadLength(header) + getPayloadOffset(header));
}

uint8_t getOpCode(WebSocketMessageHeader *header) {
	return header->bits.OP_CODE;
}

int isFinal(WebSocketMessageHeader *header) {
	return header->bits.FIN;
}

int isMasked(WebSocketMessageHeader *header) {
	return header->bits.MASK;
}

size_t getPayloadOffset(WebSocketMessageHeader *header) {
	size_t offset = 2; // smallest possible header size is 2 bytes
	
	switch (header->bits.PAYLOAD) {
		case 126:
			offset += 2; // 16 additional bits for the payload length
			break;
		case 127:
			offset += 8; // 64 additional bits for the payload length
			break;
		default:
			break;
	}
	
	if (isMasked(header))
		offset += 4;
		
	return offset;
}

size_t getPayloadLength(WebSocketMessageHeader *header) {
	
	switch (header->bits.PAYLOAD) {
		case 126:
			return ntohs(*((uint16_t *)(&header->short_header+1)));
			break;
		case 127:
			return ntohl(*((uint64_t *)(&header->short_header+1))); // I hope this includes 64 bits 'long' types
			break;
		default:
			return header->bits.PAYLOAD;
			break;
	}
}

// Return zero if there is no mask (outbound messages for instance)
uint32_t getMask(WebSocketMessageHeader *header) {
	uint32_t mask;
	
	if (!isMasked(header))
		return 0x0;
	
	/* short_header is a uint16_t, so pointer arithmetic always increases by 2 bytes! */
	switch (header->bits.PAYLOAD) {
		case 126:
			mask = *((uint32_t *)(&header->short_header+2)); // mask is at 4 bytes in the message
			break;
		case 127:
			mask = *((uint32_t *)(&header->short_header+5)); // mask is at 10 bytes in the message
			break;
		default:
			mask = *((uint32_t *)(&header->short_header+1)); // mask is at 2 bytes in the message
			break;
	}
	
	return mask;
}

WebSocketMessageHeader * initWS_Header(char *buffer, size_t length) {
	/* 
	 * Maximal possible header space is apparantly 2 + 8 + 4 bytes.
	 * I still copy the whole message inside the struct. It is convenient because we can use struct
	 * members to craft offsets and reach data.
	 */
	
	WebSocketMessageHeader* header = malloc(length);
	if (header == NULL) {
		fprintf(stderr, "malloc() couldn't allocate memory for a WebSocketMessageHeader\n");
		exit(EXIT_FAILURE);
	}
	memset((void *)header, 0x0, length);
	
	memcpy(header, buffer, length);
	
	header = (WebSocketMessageHeader *)(header);
	
	return header;
}

void freeWS_Header(WebSocketMessageHeader *header) {
	free(header);
}

/*
 * Calling messageDecode twice on the buffer should reencode the message.
 */
void messageDecode(char *msg, uint32_t length, uint32_t mask) {
	int i = 0;
	mask = ntohl(mask);
	uint8_t mkeys[4] = {(uint8_t)(mask>>24), (uint8_t)(mask>>16), (uint8_t)(mask>>8), (uint8_t)mask};
#if DEBUG_LEVEL >= 2
	printf("Mkeys=0x%x, 0x%x, 0x%x, 0x%x\n------------------\n", mkeys[0], mkeys[1], mkeys[2], mkeys[3]);
#endif
	for (i = 0; i < length; i++) {
		msg[i] = (unsigned char)(msg[i] ^ mkeys[i%4]);
	}
}

void printDecodedMessage(char *msg, size_t length) {
	WebSocketMessageHeader* header = initWS_Header(msg, length);
#if DEBUG_LEVEL >= 2
	WS_HEADER_PRINT(header, "Printing in printDecodedMessage()");
#endif
	
	uint32_t mask = getMask(header);
	size_t offset = getPayloadOffset(header);
	
	messageDecode((char *)(msg+offset), getPayloadLength(header), mask);
#if DEBUG_LEVEL >= 3
	// Print the clear text message
	INFO_PRINT("Decoded PR_Write() message is: ", SH_YELLOW);
	printf("\n%s\n", (char *)(msg+offset));
#endif
	// call again to re-encode
	messageDecode((char *)(msg+offset), getPayloadLength(header), mask);
	// now dump the mem to prove that we obtain the orginal message
	// memPrint(msg, getMessageLength(header), 16);
	
	freeWS_Header(header);
}

size_t getIndex(char *decodedToken, char encodedToken) {
	// decode for only one type
	if (decodedToken != NULL && encodedToken != 0)
		return -1;
		
	int i;
	if (decodedToken) {
		for (i = 0; i < 64; i++)
			if (strcmp(decoded[i], decodedToken) == 0) return i;
	} else {
		for (i = 0; i < 64; i++)
			if (encoded[i] == encodedToken) return i;
	}
	
	//should never reach here
	return -1;
}
// Decodes the chess.com move notation
// Assumes that encodedMovesIn is a null terminated string and is around 4 times the size of encodedMovesIn
void decodeMoves(const char* encodedMovesIn, char *decodedMovesOut) {
	if (strlen(encodedMovesIn) % 2 != 0) {
		fprintf(stderr, "Encoded Moves not a multiple of 2\n");
		return;
	}
	int i;
	char buf[0x7] = {0}; // if it is a promotion move it looks like 'a7b8q' and thus has 7 chars (including whitespace and null byte!)
	for (i = 0; i < (strlen(encodedMovesIn)); i+=2) {
		size_t j = getIndex(NULL, encodedMovesIn[i]);
		
		// handle promotional moves
		if (strchr("{~}(^)[_]@#$", encodedMovesIn[i+1])) {
			char promMovePart[0x4] = {0};
			switch (encodedMovesIn[i+1]) {
				case '{':
					promMovePart[0] = decoded[j][0]-1;
					promMovePart[1] = (decoded[j][1] == '7') ? '8' : '1';
					promMovePart[2] = 'q';
					break;
				case '~':
					promMovePart[0] = decoded[j][0];
					promMovePart[1] = (decoded[j][1] == '7') ? '8' : '1';
					promMovePart[2] = 'q';
					break;
				case '}':
					promMovePart[0] = decoded[j][0]+1;
					promMovePart[1] = (decoded[j][1] == '7') ? '8' : '1';
					promMovePart[2] = 'n';
					break;
				case '(':
					promMovePart[0] = decoded[j][0]-1;
					promMovePart[1] = (decoded[j][1] == '7') ? '8' : '1';
					promMovePart[2] = 'n';
					break;
				case '^':
					promMovePart[0] = decoded[j][0];
					promMovePart[1] = (decoded[j][1] == '7') ? '8' : '1';
					promMovePart[2] = 'n';
					break;
				case ')':
					promMovePart[0] = decoded[j][0]+1;
					promMovePart[1] = (decoded[j][1] == '7') ? '8' : '1';
					promMovePart[2] = 'n';
					break;
				case '[':
					promMovePart[0] = decoded[j][0]-1;
					promMovePart[1] = (decoded[j][1] == '7') ? '8' : '1';
					promMovePart[2] = 'r';
					break;
				case '_':
					promMovePart[0] = decoded[j][0];
					promMovePart[1] = (decoded[j][1] == '7') ? '8' : '1';
					promMovePart[2] = 'r';
					break;
				case ']':
					promMovePart[0] = decoded[j][0]+1;
					promMovePart[1] = (decoded[j][1] == '7') ? '8' : '1';
					promMovePart[2] = 'r';
					break;
				case '@':
					promMovePart[0] = decoded[j][0]-1;
					promMovePart[1] = (decoded[j][1] == '7') ? '8' : '1';
					promMovePart[2] = 'b';
					break;
				case '#':
					promMovePart[0] = decoded[j][0];
					promMovePart[1] = (decoded[j][1] == '7') ? '8' : '1';
					promMovePart[2] = 'b';
					break;
				case '$':
					promMovePart[0] = decoded[j][0]+1;
					promMovePart[1] = (decoded[j][1] == '7') ? '8' : '1';
					promMovePart[2] = 'b';
					break;
				default: // that can't happen actually
					fprintf(stderr, "Invalid promotion codec character move!\n");
					break;
			}
			sprintf(buf, "%s%s ", decoded[j], promMovePart);
		} else {
			size_t jj = getIndex(NULL, encodedMovesIn[i+1]);
			sprintf(buf, "%s%s ", decoded[j], decoded[jj]);
		}
		// finally append move to output buffer
		strncat(decodedMovesOut, buf, strlen(buf));
	}
}

void encodeMoves(const char* decodedMovesIn, char* encodedMovesOut) {
	char *token;
	const char *delimiter = " "; // all moves are separated by whitespaces by convention
	int i, ii;
	char p1[3] = {0}, p2[3] = {0}, res[3] = {0};
	char pp3[4] = {0}; // for promotion moves because they have one extra char like q,b,n,r
	
	/* get the first token */
	token = strtok((char *)decodedMovesIn, delimiter);
   
	/* walk through further tokens */
	while(token != NULL) {
		// handle promotion moves
		if (strlen(token) == 5 && (strchr(token, 'q') || strchr(token, 'r') || strchr(token, 'n') || strchr(token, 'b'))) {
			p1[0] = (char)*(token); p1[1] = (char)*(token+1); pp3[0] = (char)*(token+2); pp3[1] = (char)*(token+3);  pp3[2] = (char)*(token+4);
			// pp3[1] is either 1 or 8, is is not important
			// pp3[0] is of importance
			switch (pp3[2]) {
				case 'q':
					if ((pp3[0]-1) == p1[0])
						res[1] = '{';
					else if (pp3[0] == p1[0])
						res[1] = '~';
					else if ((pp3[0]+1) == p1[0])
						res[1] = '}';
					break;
				case 'r':
					if ((pp3[0]-1) == p1[0])
						res[1] = '[';
					else if (pp3[0] == p1[0])
						res[1] = '_';
					else if ((pp3[0]+1) == p1[0])
						res[1] = ']';
					break;
				case 'n':
					if ((pp3[0]-1) == p1[0])
						res[1] = '(';
					else if (pp3[0] == p1[0])
						res[1] = '^';
					else if ((pp3[0]+1) == p1[0])
						res[1] = ')';
					break;
				case 'b':
					if ((pp3[0]-1) == p1[0])
						res[1] = '@';
					else if (pp3[0] == p1[0])
						res[1] = '#';
					else if ((pp3[0]+1) == p1[0])
						res[1] = '$';
					break;
				default:
					fprintf(stderr, "Invalid fifth char=%c in promotion move!\n", pp3[2]);
					break;
			}
			i = getIndex(p1, 0);
			res[0] = encoded[i];
		} else { // normal non promotionial moves with 4 chars
			p1[0] = (char)*(token); p1[1] = (char)*(token+1); p2[0] = (char)*(token+2); p2[1] = (char)*(token+3);
			i = getIndex(p1, 0); ii = getIndex(p2, 0);
			res[0] = encoded[i]; res[1] = encoded[ii]; res[2] = 0;
		}
		strncat(encodedMovesOut, res, strlen(res));
		token = strtok(NULL, delimiter);
	}
}

void sleepMs(uint32_t ms) {
	struct timespec ts;
	ts.tv_sec = 0 + (ms / 1000);
	ts.tv_nsec = 1000 * 1000 * (ms % 1000);
	nanosleep(&ts, NULL);
}

int initStockfish() {
	int pipe_read[2], pipe_write[2], n;
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
	char *execArgs[] = {"stockfish", NULL };

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
				} else {
#if DEBUG_LEVEL >= 2
					fprintf(stdout, "Got ``Stockfish`` header\n");
#endif
				}
			}
			
			if ((n = write(pipe_read[1], uciCommands[0], strlen(uciCommands[0])) == -1))
				perror("write()");
				
			sleepMs(100);
			
			if ((n = read(pipe_write[0], buf, 4049)) >= 0) {
				buf[n] = 0;
				if (!strstr(buf, "uciok")) {
					fprintf(stderr, "No uciok\n");
					return -1;
				} else {
#if DEBUG_LEVEL >= 2
					fprintf(stdout, "Got uciok\n");
#endif
				}
			}
			
			if ((n = write(pipe_read[1], uciCommands[1], strlen(uciCommands[1])) == -1))
				perror("write()");
				
			sleepMs(100);
				
			if ((n = read(pipe_write[0], buf, 100)) >= 0) {
				buf[n] = 0;
				if (!strstr(buf, "readyok")) {
					fprintf(stderr, "No readyok\n");
					return -1;
				} else {
#if DEBUG_LEVEL >= 2
					fprintf(stdout, "Got readyok\n");
#endif
				}
			}
			
			if ((n = write(pipe_read[1], uciCommands[2], strlen(uciCommands[2])) == -1))
				perror("write()");
			
			sleepMs(100);
			
			if ((n = write(pipe_read[1], uciCommands[3], strlen(uciCommands[3])) == -1))
				perror("write()");
				
			printf("Calculating initial engine move\n");
			sleepMs(1000);
			
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
#if DEBUG_LEVEL >= 2
					fprintf(stdout, "Best move is ``%s``\n", ptr);
#endif
					// add move to gameState
					strncat(gameState.engineMoves, ptr+strlen("bestmove "), 5); // move is always 4 bytes (???)
				}
			}
			return 1; //success
			
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
	INFO_PRINT("Starting engine computation on current moves...", SH_GREEN);
	
	char buf[0x200] = {0};
	char *cmdFmt = "position startpos moves %s\n";
	sprintf(buf, cmdFmt, gameState.movesMadeDecoded); // always calculate on the moves Made
	
	if ((n = write(psHandles.engineInput, buf, strlen(buf)) == -1))
		perror("write()");
	
	// sleep for a little time
	sleepMs(30);

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
	
	//sleepMs(500); Never sleap, this function is called asynchroneously
	
	if ((n = write(psHandles.engineInput, uciCommands[4], strlen(uciCommands[4])) == -1))
		perror("write()");
		
	sleepMs(30);
		
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
			
			strncat(gameState.engineSuggestion, (char *)ptr+strlen("bestmove "), cnt+1); // move is always 4 bytes, except when a pawn promotoes, then the move is 5 bytes long
		}
	}
	return 1;
}

char * quickMove(const char *previousMoves) {
	int pipe_read[2], pipe_write[2], n;
	char buf[0x10000] = {0};
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
				} else {
#if DEBUG_LEVEL >= 2
					fprintf(stdout, "Got ``Stockfish`` header\n");
#endif
				}
			}
			
			if ((n = write(pipe_read[1], uciCommands[0], strlen(uciCommands[0])) == -1))
				perror("write()");
				
			sleepMs(30);
			
			if ((n = read(pipe_write[0], buf, 4049)) >= 0) {
				buf[n] = 0;
				if (!strstr(buf, "uciok")) {
					fprintf(stderr, "No uciok\n");
					return NULL;
				} else {
#if DEBUG_LEVEL >= 2
					fprintf(stdout, "Got uciok\n");
#endif
				}
			}
			
			if ((n = write(pipe_read[1], uciCommands[1], strlen(uciCommands[1])) == -1))
				perror("write()");
				
			sleepMs(30);
				
			if ((n = read(pipe_write[0], buf, 100)) >= 0) {
				buf[n] = 0;
				if (!strstr(buf, "readyok")) {
					fprintf(stderr, "No readyok\n");
					return NULL;
				} else {
#if DEBUG_LEVEL >= 2
					fprintf(stdout, "Got readyok\n");
#endif
				}
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
			
			char msgBuf[0x300]; sprintf(msgBuf, "Calculating move based on %s\n", previousMoves);
			INFO_PRINT(msgBuf, SH_YELLOW);
			sleepMs(800);
			
			if ((n = write(pipe_read[1], uciCommands[4], strlen(uciCommands[4])) == -1))
				perror("write()");
				
			sleepMs(30);
				
			if ((n = read(pipe_write[0], buf, 0x10000)) >= 0) {
				buf[n] = 0; /* terminate the string */
				char *ptr;
				if (!(ptr = strstr(buf, "bestmove "))) {
					fprintf(stderr, "No bestmove in %s\n", buf);
					return NULL;
				} else {
					int cnt = 0; char *helpPtr = (char *)ptr+strlen("bestmove ");
					while (*helpPtr++ != ' ')
						cnt++;
					
					char *retVal = malloc(cnt+1);
					memset(retVal, 0, cnt+1);
					strncpy(retVal, (char *)ptr+strlen("bestmove "), cnt); // move is always 4 bytes, except by promotions
					return retVal;
				}
			}
			return NULL; //success
			
		case -1:
			perror("fork");
			exit(1);
	}
}
