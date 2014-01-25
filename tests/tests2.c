#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdint.h>
#include <netinet/in.h>

/* Make a cstring from a hexdump:
def rn(s):
	import re
	s = re.sub(r"0x[0-9a-f]*?:", "", s)
	s = re.sub(r"\s.{16}$", "", s, flags=re.MULTILINE)
	s = s.replace(' ', '\\x')
	print(s.replace('\\x\n', ''))
*/

#define WS_HEADER_PRINT(ws_h) \
			printf("Information about WebSocketMessage:\n\tFlags: FIN(%d), RSV1(%d), RSV2(%d), RSV3(%d), OPCODE(%d), MASK(%d)\n\tMessageLength: %zd\n\tPayloadOffset: %zd\n\tPayloadLength: %zd\n\tMask: 0x%x\n", \
				isFinal(ws_h), ws_h->bits.RSV1, ws_h->bits.RSV2, ws_h->bits.RSV3, getOpCode(ws_h), isMasked(ws_h), getMessageLength(ws_h), getPayloadOffset(ws_h), getPayloadLength(ws_h), getMask(ws_h));
			
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

// from outerspace
void memPrint(const char *, size_t , size_t);




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

uint32_t getMask(WebSocketMessageHeader *header) {
	uint32_t mask;
	
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
#ifdef DEBUG
	printf("Mkeys=0x%x, 0x%x, 0x%x, 0x%x\n------------------\n", mkeys[0], mkeys[1], mkeys[2], mkeys[3]);
#endif
	for (i = 0; i < length; i++) {
		msg[i] = (unsigned char)(msg[i] ^ mkeys[i%4]);
	}
}

void printDecodedMessage(char *msg, size_t length) {
	WebSocketMessageHeader* header = initWS_Header(msg, length);
	WS_HEADER_PRINT(header);
	
	uint32_t mask = getMask(header);
	size_t offset = getPayloadOffset(header);
	
	messageDecode((char *)(msg+offset), getPayloadLength(header), mask);
	printf("\n%s\n", (char *)(msg+offset));
	// call again to re-encode
	messageDecode((char *)(msg+offset), getPayloadLength(header), mask);
	// now dump the mem to prove that we obtain the orginal message
	memPrint(msg, getMessageLength(header), 16);
	
	freeWS_Header(header);
}

int main(int argc, char **argv) {
	char packet3[] = "\x81\xfe\x00\xe1\x97\xfa\x30\xdc\xcc\x81\x12\xbf\xff\x9b\x5e\xb2\xf2\x96\x12\xe6\xb5\xd5\x43\xb9\xe5\x8c\x59\xbf\xf2\xd5\x45\xaf\xf2\x88\x12\xf0\xb5\x9e\x51\xa8\xf6\xd8\x0a\xa7\xb5\x97\x5f\xaa\xf2\xd8\x0a\xa7\xb5\x9d\x59\xb8\xb5\xc0\x07\xec\xa1\xc8\x00\xe4\xae\xcf\x08\xf0\xb5\x89\x55\xad\xb5\xc0\x01\xf0\xb5\x8f\x59\xb8\xb5\xc0\x12\xae\xf8\x99\x53\xb4\xf2\x94\x12\xf0\xb5\x97\x5f\xaa\xf2\xd8\x0a\xfe\xcd\xb0\x12\xf0\xb5\x99\x5c\xb3\xf4\x91\x12\xe6\xa4\xca\x00\xec\xbb\xd8\x53\xb0\xf8\x99\x5b\xb1\xe4\xd8\x0a\xef\xa7\xca\x00\xec\xa7\xd6\x12\xaf\xe6\x8f\x51\xae\xf2\x9e\x12\xe6\xf1\x9b\x5c\xaf\xf2\x87\x1c\xfe\xe4\x93\x54\xfe\xad\xd8\x57\xaf\xf2\x88\x46\xfe\xbb\xd8\x44\xb5\xf3\xd8\x0a\xfe\xda\x95\x46\xb9\xb5\x87\x1c\xfe\xfe\x9e\x12\xe6\xb5\xce\x03\xfe\xbb\xd8\x53\xb0\xfe\x9f\x5e\xa8\xde\x9e\x12\xe6\xb5\xce\x04\xaa\xe3\xc9\x58\xba\xa4\x8a\x40\xb6\xe0\x90\x55\xea\xf2\x8f\x5c\xe5\xa1\xcf\x43\xb1\xaf\x95\x4a\xe9\xf9\xca\x12\xa1\xca";
	
	printDecodedMessage(packet3, 66);
	
	return 0;
}

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
