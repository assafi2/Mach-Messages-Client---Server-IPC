#include <stdio.h>
#include <iostream>
#include <mach/task_special_ports.h>  // task get bp
#include <servers/bootstrap.h>
#include <servers/bootstrap_defs.h>
#include <mach/task.h>
#include <mach/message.h>
#include <mach/port.h>
#include <mach/mach.h>
#include <mach/mach_host.h>
#include <mach/mach_types.h>
#include <mach/mach_vm.h>

#include "Client.h"
#include "SaveClientHandler.h"
#include "SaveServiceDefinitions.h" // structs definitions etc ...
//#include "ClientServerAPI.cpp"
#include <string.h>



using namespace std ;

extern data_packet_t* build_data_packet(data_packet_t* packet, mach_port_t reply_port, mach_port_t receiver_port) ;

  // packet segments debug
  void dropHex(char* pointer, int bytes) {
	  for(int i = 0 ; i < bytes ; i++){
		  printf("%x ~ ",pointer[i]) ;
		}
	      cout << endl ;
		}

  class SaveClientHandler::SaveClient : Client {

		// *** instance variables *** (other instance vars within super class def

	  	int cur_pid ;   // process associated to client instance pid
		mach_port_t reply_port ;
		// packet buffers must not be allocated inline to prevent the instance become
		// vulnerable to mach msg overflow that can corrupt VPTR

		data_packet_t* save_packet ;
		simple_packet_t* receive_packet ;
		data_packet_t* reply_packet ;

		friend SaveClientHandler ;


		// packet buffers building logic methods, can be used as "getters" also

		// return general struct layout of a data packet for sending 'save' to the service_port
		data_packet_t* getBuiltDataPack() {

			 return build_data_packet(save_packet, reply_port, service_port) ;
		 }

		// return general struct layout of a simple_packet (use to send 'receive' and termination messages)
		 simple_packet_t* getBuiltSimplePack() {

			memset((void*)receive_packet, 0, sizeof(simple_packet_t)) ;
			// general processing / building
			// header processing
			receive_packet->header.msgh_bits = MACH_MSGH_BITS_SET(
					MACH_MSG_TYPE_COPY_SEND, MACH_MSG_TYPE_MAKE_SEND , 0, 0) ;  // complex bit unset
			receive_packet->header.msgh_size = sizeof(simple_packet_t) ;
			receive_packet->header.msgh_remote_port = service_port ; // other end - service
			receive_packet->header.msgh_local_port = reply_port ;
			receive_packet->header.msgh_id = 0x00000001 ;
			// no desc aspects
			// body upon sending
			return receive_packet ;
		}

		// layout of a reply packet buffer that is "empty"
		data_packet_t* buildReplyPack() {

			// no need to build
			memset((void*)reply_packet, 0, sizeof(data_packet_t)) ;
			return reply_packet ;
		}

		// accept data chunk to send for saving
		// return true on MCH_MSG_SUCCESS with mach_send op otherwise false
		bool send(data_t data, int size){

			if (size > MAX_CHUNK_SIZE) return false ;
			getBuiltDataPack() ;
/*
			// debug purpose hex dumping of the header
			int headerSize = sizeof(mach_msg_header_t) ;
			char* str1 = (char*)malloc (headerSize + 1) ;
			str1[headerSize] =  '\0' ;
			str1 = (char*)memcpy((void*)str1, (void*) &(save_packet->base.header), headerSize) ;
			cout << "drop hex for client save_packet message header BEFORE SENDING :  " << endl ;
			dropHex(str1 , headerSize) ;
*/
			// 'save' send op specific processing
			pid_for_task(cur_task, &(save_packet->body.pid))  ;
			save_packet->body.type = 's' ; // 'save' sending op
			save_packet->data.address = data ;
			save_packet->data.size = (mach_msg_size_t) size ;
	//		cout << "in inner client send, header size : " << endl ;
	//		cout << ((char*)&(save_packet->base.body) - (char*)save_packet) << endl ;
	//		cout << "normal header size : " << sizeof(mach_msg_header_t) << endl ;
			mach_msg_return_t mr = mach_msg(
						(mach_msg_header_t*)save_packet,MACH_SEND_MSG,
						 sizeof(data_packet_t),/*0 on sending */0, MACH_PORT_NULL,MACH_MSG_TIMEOUT_NONE,MACH_PORT_NULL) ;
			cout << "client process " << cur_pid << " sending mach return value  :  " << mr << endl ;
			return mr == MACH_MSG_SUCCESS ;
		}

		data_info_t receive() {

			// asking server for saved data
			getBuiltSimplePack() ;
			pid_for_task (cur_task, &(this->receive_packet->body.pid)) ;
			receive_packet->body.type = 'r' ; // 'receive' sending op
				mach_msg_return_t mr = mach_msg(
					(mach_msg_header_t*)receive_packet,MACH_SEND_MSG,
					 sizeof(simple_packet_t),/*0 on sending */0, MACH_PORT_NULL,MACH_MSG_TIMEOUT_NONE,MACH_PORT_NULL) ;
			cout << "client process " << cur_pid  << " sent receive request return value   " << mr << endl ;
			// receiving ...
			data_info_t ret_data ;
			memset(&ret_data,0,sizeof(data_info_t)) ;
			buildReplyPack() ;
			mr = mach_msg(
					(mach_msg_header_t*)reply_packet, MACH_RCV_MSG | MACH_RCV_TIMEOUT ,0, //receive with timeout | MACH_RCV_TIMEOUT, 0,
					sizeof(data_packet_t)+10, reply_port, 10000 , MACH_PORT_NULL) ;  // MACH_MSG_TIMEOUT_NONE
			cout << "Client message receive (with 10 sec timeout) reply result : " << mr << endl ;
			if (mr != MACH_MSG_SUCCESS) return ret_data ; // empty data
			// verify data - in case size are equal ->
			// support possible big chunks
			cout << "reply packet dsize : " << reply_packet->data.size << endl ;
			unsigned long int dsize = reply_packet->data.size ;  // virtual range size
			if ((reply_packet->data.size != save_packet->data.size) || (reply_packet->data.address == NULL))
					return ret_data ; // empty data struct
     		vm_address_t data_old = save_packet->data.address ; // start addresses
			vm_address_t data_new = reply_packet->data.address ;
/*			// fragment memory region for effective comparison (physi addresses at the start of corresponding segments)
		    // get virtual page size (support big)
			host_name_port_t host = mach_host_self() ;
			unsigned long int page_size ;
			// could not retrieve host page size, in that case we can provide small size >= 512 (worst case)
			if (host_page_size(host, &page_size) != KERN_SUCCESS) return page_size = 521 ;
*/			// BYTE COMPARE INSTEAD
			if (memcmp((void*)data_old,(void*)data_new,dsize) == 0){ // fill returned data content
				ret_data.ptr = data_new ;
				ret_data.size = dsize ;
			}

			return ret_data ;
		}

  	  public:

		SaveClient(char* service_name):Client(service_name) {  // call superclass constructor

			//	allocate reply port with receive right

			if (mach_port_allocate(cur_task, MACH_PORT_RIGHT_RECEIVE, &reply_port)
					!= KERN_SUCCESS) {
				exit(EXIT_FAILURE) ;    // failed to allocate a port with receive right
			}

			if (mach_port_insert_right(cur_task,reply_port,reply_port,MACH_MSG_TYPE_MAKE_SEND)
					!= KERN_SUCCESS) {
				exit(EXIT_FAILURE) ;
			}

			pid_for_task(mach_task_self(), &cur_pid) ;

			cout << "client process " << cur_pid << " reply port # : " << reply_port << endl ;

			// allocate packets buffers

			save_packet = (data_packet_t*)malloc(sizeof(data_packet_t)) ;
			receive_packet = (simple_packet_t*)malloc(sizeof(simple_packet_t)) ;
			reply_packet = (data_packet_t*)malloc(sizeof(data_packet_t)) ;
		}

		//  include termination logic in case of proper finalization
		~SaveClient() {

			int updated_cur_pid ;   // pid of the currently running process to be updated
			pid_for_task (cur_task,&updated_cur_pid) ;
			if (updated_cur_pid != cur_pid)
				// current process differs from the original process which created this client instance
				// do not apply destruction logic
				return ;
			// termination request from server
			getBuiltSimplePack() ;
			receive_packet->body.type = 'd' ; // 'save' sending op
			receive_packet->body.pid = cur_pid ; // corresponding to the process relevant to the object
			mach_msg_return_t mr = mach_msg(
					(mach_msg_header_t*)receive_packet,MACH_SEND_MSG,
					2*sizeof(simple_packet_t),/*0 on sending */0, MACH_PORT_NULL,MACH_MSG_TIMEOUT_NONE,MACH_PORT_NULL) ;
			cout << "client of process " << cur_pid << " termination msg sent  " << mr << endl ;

			mach_port_destroy(cur_task,reply_port) ;  // deallocate reply port which only current task (client) has receive right on it
		}
	};

	// SaveClientHandler interface implementation

	SaveClientHandler::SaveClientHandler(char* service_name) {

		client = new SaveClient(service_name) ;
	}

	SaveClientHandler::~SaveClientHandler() {
		client->~SaveClient();
	}

	bool SaveClientHandler::send(data_t data, int size) {

		return client->send(data,size) ;
	}

	data_info_t SaveClientHandler::receive() {
		return client->receive() ;
	}









