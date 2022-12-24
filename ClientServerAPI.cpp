#include <iostream>

#include <mach/message.h>
#include "SaveServiceDefinitions.h"
#include <pthread.h>
//#include <unistd.h>
using namespace std ;
/*
 * STATIC API
 */

// receive allocated packet chunk
// return a constructed packet designated to hold data for sending
// not include actual data
	data_packet_t* build_data_packet(data_packet_t* packet, mach_port_t reply_port, mach_port_t receiver_port) {


	 memset((void*)packet, 0, sizeof(data_packet_t)) ;
	 // header processing
	 // 1 for send right of the sending process on the remote port, and 2 to give (multiple) send right to the receiver on the reply port (headers local port)
	 packet->base.header.msgh_bits = MACH_MSGH_BITS_SET(/*1*/MACH_MSG_TYPE_COPY_SEND, /*2*/MACH_MSG_TYPE_MAKE_SEND, 0, 0) ;
	 packet->base.header.msgh_bits |= MACH_MSGH_BITS_COMPLEX ; // SET COMPLEX
	 packet->base.header.msgh_size = sizeof(data_packet_t) ;
	 packet->base.header.msgh_remote_port = receiver_port ; // other end
	 packet->base.header.msgh_local_port = reply_port ;
	 packet->base.header.msgh_id = 0x00000001 ; // shouldn't matter
	 // desc aspects
	 packet->base.body.msgh_descriptor_count = 1 ; // 1 data attachment
	 // save_packet.data.address = NULL ;
	 packet->data.deallocate = false ; // keep for later comparison (client) / keep data (server)
	 packet->data.type = MACH_MSG_OOL_DESCRIPTOR ;
	 packet->data.copy = MACH_MSG_VIRTUAL_COPY ;  // SHEM WITH COPY ON WRITE
//	}
	 return packet ;
}



//apply  MACH_MSG_VIRTUAL_COPY set by default
