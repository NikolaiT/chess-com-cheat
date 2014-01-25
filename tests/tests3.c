#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdint.h>

#define WS_HEADER_PRINT(ws_h) \
			printf("Information about WebSocketMessage:\n\tMessageLength: %u\n\tPayloadOffset: %d\n\tPayloadLength: %d\n\tMask: 0x%x\n\tOpCode: 0x%x\n\t\n", \
				ws_h->getMessageLength(ws_h), ws_h->getPayloadOffset(ws_h), ws_h->getPayloadLength(ws_h), ws_h->getMask(ws_h), ws_h->getOpCode(ws_h));
			
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

size_t getMessageLength(WebSocketMessageHeader *self) {
	
}

uint8_t getOpCode(WebSocketMessageHeader *self) {
	
}

int isFinal(WebSocketMessageHeader *self) {}

int isMasked(WebSocketMessageHeader *self) {}

size_t getPayloadOffset(WebSocketMessageHeader *self) {
	size_t offset = 2; // smallest possible header size is 2 bytes
	
	switch (self->bits.PAYLOAD) {
		case 126:
			offset += 2; // 16 additional bits for the payload length
			break;
		case 127:
			offset += 8; // 64 additional bits for the payload length
			break;
		default:
			break;
	}
	
	if (self->isMasked())
		offset += 4;
		
	return offset;
}

uint64_t getPayloadLength(WebSocketMessageHeader *self) {
	uint64_t length;
	
	switch (self->bits.PAYLOAD) {
		case 126:
			length = *((uint16_t *)(&self->short_header+1));
			break;
		case 127:
			length = *((uint64_t *)(&self->short_header+1));
			break;
		default:
			length = self->bits.PAYLOAD;
			break;
	}
	
	return length;
}

uint32_t getMask(WebSocketMessageHeader *self) {
	uint32_t mask;
	
	/* short_header is a uint16_t, so pointer arithmetic always increases by 2 bytes! */
	switch (self->bits.PAYLOAD) {
		case 126:
			mask = *((uint32_t *)(&self->short_header+2)); // mask is at 4 bytes in the message
			break;
		case 127:
			mask = *((uint32_t *)(&self->short_header+5)); // mask is at 10 bytes in the message
			break;
		default:
			mask = *((uint32_t *)(&self->short_header+1)); // mask is at 2 bytes in the message
			break;
	}
	
	return mask;
}

WebSocketMessageHeader * initWS_Header(char *buffer, size_t length) {
	WebSocketMessageHeader* header = malloc(sizeof(WebSocketMessageHeader));
	if (header == NULL) {
		fprintf(stderr, "malloc() couldn't allocate memory for a WebSocketMessageHeader\n");
		exit(EXIT_FAILURE);
	}
	memset((void *)header, 0x0, sizeof(WebSocketMessageHeader));
	
	header = (WebSocketMessageHeader *)(buffer);
	
	// set the member functions of the struct
	header->getMessageLength = getMessageLength;
	header->getPayloadOffset = getPayloadOffset;
	header->getPayloadLength = getPayloadLength;
	header->getMask = getMask;
	header->getOpCode = getOpCode;
	header->isFinal = isFinal;
	header->isMasked = isMasked;
	
	return header;
}

freeWS_Header(WebSocketMessageHeader *header) {
	free(header);
}

int main(int argc, char **argv) {
	//printf("Sizeof header: %d\n", sizeof(WebSocketMessageHeader));
	char packet[] = "\x81\xfe\x00\xe1\xb5\x82\x10\xeb\xee\xf9\x32\x88\xdd\xe3\x7e\x85\nd0\xee\x32\xd1\x97\xad\x63\x8e\xc7\xf4\x79\x88\xd0\xad\x65\x98\nd0\xf0\x32\xc7\x97\xe6\x71\x9f\xd4\xa0\x2a\x90\x97\xef\x7f\x9d\nd0\xa0\x2a\x90\x97\xe5\x79\x8f\x97\xb8\x27\xdb\x83\xb0\x20\xd3\n8c\xb7\x28\xc7\x97\xf1\x75\x9a\x97\xb8\x29\xc7\x97\xf7\x79\x8f\n97\xb8\x32\x99\xda\xe1\x73\x83\xd0\xec\x32\xc7\x97\xef\x7f\x9d\nd0\xa0\x2a\xc9\x85\xc9\x32\xc7\x97\xe1\x7c\x84\xd6\xe9\x32\xd1\n87\xbb\x25\xdb\x99\xa0\x73\x87\xda\xe1\x7b\x86\xc6\xa0\x2a\xd9\n8c\xb7\x20\xdf\x81\xae\x32\x98\xc4\xf7\x71\x99\xd0\xe6\x32\xd1\nd3\xe3\x7c\x98\xd0\xff\x3c\xc9\xc6\xeb\x74\xc9\x8f\xa0\x77\x98\nd0\xf0\x66\xc9\x99\xa0\x64\x82\xd1\xa0\x2a\xc9\xf8\xed\x66\x8e\n97\xff\x3c\xc9\xdc\xe6\x32\xd1\x97\xb7\x28\xc9\x99\xa0\x73\x87\ndc\xe7\x7e\x9f\xfc\xe6\x32\xd1\x97\xb6\x24\x9d\xc1\xb1\x78\x8d\n86\xf2\x60\x81\xc2\xe8\x75\xdd\xd0\xf7\x7c\xd2\x83\xb7\x63\x86\n8d\xed\x6a\xde\xdb\xb2\x32\x96\xe8";
	
	WebSocketMessageHeader* header = initWS_Header(packet, 0);
	WS_HEADER_PRINT(header);
	freeWS_Header(header);
}
