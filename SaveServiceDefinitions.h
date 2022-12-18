#include <mach/message.h>
#include <mach/task.h>

#ifndef SAVE_SERVIC_DEFINITIONS_H
#define SAVE_SERVIC_DEFINITIONS_H


#define MAX_CHUNK_SIZE 1024

typedef uint64_t data_t ;  // raw data chunk

// describe op type for a sent message and owner task

typedef struct ss_msg_body {
	char type ; //  's' - save , 'r' - receive, 'd' - dead (notify sever for termination)
	task_t task ;
} ss_msg_body_t ;

/* complex message packet used for 'save' op by the client and reply with data by the server
   its size will be used to allocate receiving (message) buffers at both sides
   if we wanted to send/receive multiple chunks we could use an array of OOL's
   and specify its size in the base.body (desc count) field of the packet
*/
typedef struct data_packet {  //
	mach_msg_base_t base ;   // includes header and desc count, defined in mach/message.h
	mach_msg_ool_descriptor64_t data ;   // actual chunk up to MAX_CHUNK_SIZE
	ss_msg_body_t body ;
} data_packet_t ;

// simple message packet used for 'receive' op e.g. send server a receive request (as well as sending 'dead' message)
typedef struct simple_packet {
	mach_msg_header_t header ;   // includes header only ! defined in mach/message.h
	ss_msg_body_t body ;
} simple_packet_t ;

#endif
