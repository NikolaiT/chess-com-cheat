#include <stdio.h>
#include <string.h>

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

size_t getIndex(char *, char);
void decodeMoves(const char* , char *);
void encodeMoves(const char* , char* );

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

int main() {
	// Test decoding
	const char *moves = "iq!TkA5Qmu0KgvZRlBKBuB6Efm90eg8!bs98pxEvmv3VcDQGdtGQaeRJAIZ@7ZjzWOY{";
	const char *moves2 = "0e";
	char decodedMoves[0x500] = {0};
	decodeMoves(moves, decodedMoves);
	printf("After decoding: %s\n", decodedMoves);
	
	// Test encoding
	char holdEncodedMoves[0x500] = {0};
	encodeMoves(decodedMoves, holdEncodedMoves);
	printf("After encoding again: %s\n", holdEncodedMoves);
	return 0;
}
