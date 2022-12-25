#include <mach/port.h>
#include <mach/task_special_ports.h>  // task get bp
#include <servers/bootstrap.h>
#include <servers/bootstrap_defs.h>
#include <mach/mach_init.h>
#include <mach/task.h>
#include <pthread.h>
#include "saveServiceDefinitions.h"
#include "DataTable.h"

#ifndef SAVE_SERVER_H
#define SAVE_SERVER_H

#define MAX_TASKS   // upper id for tasks range
#define MAX_THREADS 30   // numbers of service threads
#define DATA_RECEIVE 0
#define DATA_SEND 1
#define DELETE 2

typedef uint64_t data_t ;

// struct to hold save's table entry
typedef struct map_entry {

	semaphore_t lock ;
	int size ;
	data_t data ;
} map_entry_t ;

class SaveServer {


	mach_port_t bs_port ;  // bootstrap port
	mach_port_t ss_port  ; // save service port
	task_t cur_task  ;  // task's port

	DataTable& dHash ; // saved data hash table
	pthread_t threads[MAX_THREADS]; // "thread pool"  - working with pthreads instead of mach threads for comfortability
	int maxNumOfThreads ;  // actual max num of possible service threads, to be settable
 	int nextThread ; // next thread entry in the pool
	bool running ; // server state
	pthread_t collectorThread ;  // task's garbage collector




	// static functions
	static void* receiver(void *sargs_ptr) ;
	static void* sender(void *sargs_ptr) ;
	static void* cleaner(void *sargs_ptr) ;
	static void* collector(void* server) ;

	// return a generally constructed data_packet_t for sending
	data_packet_t* getBuiltDataPacket(mach_port_t client_port) ;

public :

//	SaveServer() ;
	SaveServer(bool manual) ;
	void runServer() ;
	void stopServer() ;
	friend void* receiver(void *sargs_ptr) ;
	friend void* sender(void *sargs_ptr) ;
	friend void* cleaner(void *sargs_ptr) ;
	friend void* collector(void *server) ;


protected :


	// op can be either DATA_RECEIVE, DATA_SEND or TERMINATE
	int processMessage(void* mach_msg, short op) ;

	void send(void* mach_msg) ;

	void receive(void* mach_msg) ;

} ;



#endif // SAVE_SERVER_H
