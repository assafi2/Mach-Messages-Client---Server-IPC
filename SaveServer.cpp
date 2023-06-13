#include "SaveServer.h"
#include <iostream>
#include <mach/message.h>
#include <mach/mach.h>
#include <mach/port.h>
#include <mach/thread_act.h>
#include <mach/semaphore.h>

// for testings within main
#include <unistd.h>

#include "SaveClientHandler.h"
#include "SaveServiceDefinitions.h"
// #include "ClientServerAPI.cpp"
#include "DataTable.h" // designated hash table class

#include <pthread.h>
#include <errno.h>

// internal class usage
// args to pass a processing thread funct
typedef struct serve_args {
	SaveServer* server ; // this server instance pointer
	void* msg ; // mach message packet
} sargs_t ;

using namespace std ;

extern data_packet_t* build_data_packet(data_packet_t* packet, mach_port_t reply_port, mach_port_t receiver_port) ;

// overloaded for construction with manual registration
/*
SaveServer::SaveServer() : dHash(*(DataTable*)NULL) {


	SaveServer(true) ;
}
*/

// manual == false when SaveServer instance created due to bootstrap_check_in call
// with corresponding service name (therefore will be registered)
// otherwise signifies manual registering logic to be applied

SaveServer::SaveServer(bool manual) : dHash(*(new DataTable(TABLE_SIZE))) {


//	dHash =  *(new DataTable(TABLE_SIZE));

	cur_task = mach_task_self() ;

	//	allocate service port with receive right

	if (mach_port_allocate(cur_task,MACH_PORT_RIGHT_RECEIVE,&ss_port)
			!= KERN_SUCCESS) {
		exit(EXIT_FAILURE) ;    // failed to allocate a port with receive right
	}


	// receive send right on the service port for service registration purpose by bootstrap interface
	if (mach_port_insert_right(cur_task,ss_port,ss_port,MACH_MSG_TYPE_MAKE_SEND)
			!= KERN_SUCCESS) {
		exit(EXIT_FAILURE) ;    // failed to receive send right on service port
	}

	// manual registration logic

	if (manual == true) {
		if (task_get_special_port(cur_task, TASK_BOOTSTRAP_PORT, &bs_port)
				!= KERN_SUCCESS) {
			exit(EXIT_FAILURE);
		}
		if (bootstrap_register(bs_port, "com.apple.save_service", ss_port)
				!= KERN_SUCCESS) {
			exit(EXIT_FAILURE);
				}
		}

	running = false ;
	maxNumOfThreads = MAX_THREADS ;
	nextThread = 0 ;

	// 0 set threads array
	memset(threads, 0, sizeof(pthread_t) * MAX_THREADS) ;

	cout << "save_service mach port #  : " << ss_port << endl ;

	// run collector as thread
	int err = pthread_create(&collectorThread, NULL, &collector, (void*)this);

	if (err!=0) {

	cerr << "could not run collector thread, terminating service " << endl ;

	exit(1);

	}
}

// should consist main dispatch loop
void SaveServer::stopServer() {
	running = false ;
}

void SaveServer::runServer() {

	running = true;
	// start collector

	while (running) {

	int timeout = 5000 ;
	// allocate incoming message buffer - big enough for both complex and simple messages
	void* msg  = malloc(sizeof(data_packet_t) + 10) ;
	// message receiving with timeout for non blocking the whole task ...
	mach_msg_return_t mr =
			mach_msg((mach_msg_header_t*)msg, MACH_RCV_MSG | MACH_RCV_TIMEOUT ,0, //receive with timeout | MACH_RCV_TIMEOUT, 0,
			sizeof(data_packet_t)+10, ss_port, timeout , MACH_PORT_NULL) ;  // MACH_MSG_TIMEOUT_NONE
 	cout << "server message receive (with " << timeout << " ms timeout) result : " << mr << endl ;
	if (mr != MACH_MSG_SUCCESS) {
		cout << "server timeout "  << timeout << " seconds on msg queue " << endl ;
		continue ;
	}

	// check complex bit

	if (((mach_msg_header_t*) msg)->msgh_bits & 0x80000000)  {

		processMessage(msg, DATA_RECEIVE) ; // treat as data_packet_t
	}
	// treat as simple_packet_t
	else if (((simple_packet_t*) msg)->body.type == 'r') {

		processMessage(msg, DATA_SEND) ;
	}
	else if (((simple_packet_t*)msg)->body.type == 'd') {

		processMessage(msg, DELETE) ;
	}
	else { //IF NON OF THE REPRESTING OP LETTRES FOUND MESSAGE DISCARDED

		free(msg) ;
//		cout << "discard message" << endl ;
	}

	// VALID MSG -> MUST FREE RECIEVE BUFFER IN EACH THREAD ROUTINE SEPERATLLY !
	}

}

	data_packet_t* SaveServer::getBuiltDataPacket(mach_port_t client_port) {
		return build_data_packet((data_packet_t*)malloc(sizeof(data_packet_t)), ss_port, client_port) ;
	}

//   STATIC FUNCTIONS

// service threads funcs
// receive sargs_t*

	void* SaveServer::sender(void* sargs_ptr) {

		if (sargs_ptr == NULL) return NULL ; // pass
		simple_packet_t *rec_msg = (simple_packet_t*)(((sargs_t*)sargs_ptr)->msg) ;
		SaveServer *server = ((sargs_t*)sargs_ptr)->server ;
		int pid  = rec_msg->body.pid ;  // ss_msg_body_t
		data_info_t data = server->dHash.getData(pid) ;
		//send relevant data
		if (data.ptr != NULL) {   // saved data exist for the particular client task
			data_packet_t *reply_msg = (data_packet_t*) malloc(sizeof(data_packet_t)) ;
			reply_msg = server->getBuiltDataPacket(rec_msg->header.msgh_remote_port) ; // TODO check if its that ? review header ...
			reply_msg->data.address = data.ptr ;
		    reply_msg->data.size = data.size ;
			// APLLY TIMEOUT ASPECTS ON HEADER ?
			mach_msg_return_t mr = mach_msg(
					(mach_msg_header_t*)reply_msg,MACH_SEND_MSG,
					 sizeof(data_packet_t),/*0 on sending */0, MACH_PORT_NULL,MACH_MSG_TIMEOUT_NONE,MACH_PORT_NULL) ;
			printf("mach_msg_ret value for sending data to client pid %d : %d \n", pid, mr) ;
		}
		else cout << "NO data saved for the requesting client process " << pid 	<< (char*)data.ptr << endl ;
		// cout << didnt send data message + details
		free(rec_msg) ;
//		free(reply_msg) ??? should check if needed by kernel
		return NULL ; // unconsidered val
}

	void* SaveServer::receiver(void *sargs_ptr) {

		if (sargs_ptr == NULL) return NULL ; // pass
		SaveServer *server = (SaveServer*)((sargs_t*)sargs_ptr)->server ;
		data_packet_t *msg = (data_packet_t*)(((sargs_t*)sargs_ptr)->msg) ;
		int pid = msg->body.pid ;
		if (msg->data.size > MAX_CHUNK_SIZE) {   // verify data size limit
			free(msg) ;
			free(sargs_ptr) ; // heap allocated
			cout << "client process " << pid << " message's data too big, can not save" << endl ;
			return NULL ;  // pass
		}

		// OOL desc, in case of a resent data former physical mapping should hold while a new virtual pointer is given ?
		// COPY ON WRITE OF A SHEM REGION by client mach msg header settings
		server->dHash.updateData(pid, msg->data.address,msg->data.size) ;
		cout << "server saved successfully data from client process " <<
				msg->body.pid <<
						", size of saved data : " << msg->data.size << endl ;
		free(msg) ;
		free(sargs_ptr) ; // heap allocated
		return NULL ; // unconsidered val
	}

	void* SaveServer::cleaner(void* sargs_ptr) {

//	cout << "in cleaner code" << endl ;
	if (sargs_ptr == NULL) return NULL ; // pass
	simple_packet_t *simple_msg =
			(simple_packet_t*)&((sargs_t*)sargs_ptr)->msg ;
	SaveServer *server = (SaveServer*)&((sargs_t*)sargs_ptr)->server ;
	int pid = simple_msg->body.pid ;
	free(simple_msg) ; // free buffer after extracting directions
	// acquire lock on map entry
	char* ret ;
	if (server->dHash.deleteData(pid)) {
		ret = "successfully" ;
	}
	else ret = "unsuccessfully" ;
		cout << "client process " << pid << " data " << ret << " deleted from server" << endl ;
	return NULL ; // unconsidered val
}

	// continuously running DataTable collect method to delete client processes garbage

	void* SaveServer::collector(void* server) {

		{

			SaveServer *cast_server = (SaveServer*)server ; // prevent var names collision
			while (cast_server->running) {
				sleep(3) ;
				cast_server->dHash.collect() ;
			}
			return NULL  ;
		}
	}
/*
 process the mach msg with a sender / receiver / cleaner new thread
 return 0 for success otherwise return error num (EAGAIN,EDADLK,EINVAL,ESRCH,EPERM)
 while error result on creating or trying to execute the thread (with pthread_join)
 or EDOM for bad op arg
*/

int SaveServer::processMessage(void* mach_msg, short op) {

//	cout << "in proccess message op is : " << op << endl ;
	void* (*func_op) (void*) ;
	switch (op) {
				case DATA_RECEIVE :
					func_op = &receiver ;
					break ;
				case DATA_SEND :
					func_op = &sender ;
					break ;
				case DELETE :
					func_op = &cleaner ;
					break ;
				default :
					return EDOM ; // num arg out of domain
	}
	int err = 0  ; // also return value

	sargs_t& args = *((sargs_t*)malloc(sizeof(sargs_t))) ;
	args.server = this ;
	args.msg = mach_msg ;

	if (threads[nextThread] != 0 ) {  // entry for an existed thread
		err = pthread_join(threads[nextThread],NULL) ;
	}
	if (err == 0)
		err = pthread_create(&threads[nextThread], NULL, func_op, &args);
	nextThread = (nextThread + 1) % maxNumOfThreads ;
	return err ;}

// MAIN TEST FUNCTION FOR SAVE SERVICE SYS

int main(int argc, char** argv) {

	int count = 6 ;
	int f = fork() ;
	if (f > 0) { // server


		SaveServer ss = SaveServer(true) ;
		cout << "server registered " << endl ;

		ss.runServer() ;

		cout << "server stopped" << endl ;
	}

	if (f ==0)  { // client(s)
		while( (f == 0) and (count > 0)) {
			sleep(2) ;
			if (count == 0) return 0 ;
			cout <<  "client fork " <<  count << endl ;
			--count ;
			int pid ;
			pid_for_task(mach_task_self(),&pid) ;
			cout << "client of process " << pid << " initialization" << endl ;
			SaveClientHandler sc = SaveClientHandler("com.apple.save_service") ;
			cout << "new client from process " << pid << " initialized successfully"<<  endl;
			char* input = new char[MAX_CHUNK_SIZE] ;  // data to be saved
			sprintf(input,"client of process %d test data", pid) ;
			if (sc.send((data_t)input, strlen(input)+1) == true)
				cout << "client process " << pid <<
				" sent request to save data successfully" << endl ;
			sleep(2) ;
			data_info_t data = sc.receive() ;
			if (data.ptr != NULL) {
				char* content = (char*)malloc(data.size+1) ;
				memcpy((void*)content, (void*)data.ptr, data.size) ;
				content[data.size] = '\0' ;
				printf("recieved data chunk ptr %p, received data content : \n%s\n ", data.ptr, content ) ;
			}

			f = fork() ;
		}
	}
	return 0 ;
} ;
