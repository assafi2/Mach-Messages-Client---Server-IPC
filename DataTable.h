#ifndef DATATABLE_H_
#define DATATABLE_H_


#include <mach/port.h>
#include <servers/bootstrap.h>
#include <servers/bootstrap_defs.h>
#include <mach/mach_init.h>
#include <mach/task.h>
#include <pthread.h>
#include <mach/semaphore.h>
#include "SaveServiceDefinitions.h"


#include <iostream>

using namespace std ;

/*
* Hash table to hold data entries
* table will contain fixed number of buckets and resolve collisions with chaining
* does not support dynamic number of buckets
* a client process can have at most 1 data entry in the table, processes pids are the table keys
*/

// as mentioned
// not support dynamic hashing i.e. resizing of hash table entries number (number of buckets) according to current capacity
// works ok because of assumable limited number of client processes relative to max number of processes in the machine (OS)

#define TABLE_SIZE 30

class DataTable {

	// doubly linked list for bucket_t entries

	typedef struct chain_entry {
		struct chain_entry* backward_entry ;
		struct chain_entry* forward_entry ;
		int pid ;
		int count ; // count the number of currently visiting threads
		semaphore_t d_lock ; // content (data ptr) RW lock
		semaphore_t f_lock ; // forward_entry ptr lock
		semaphore_t count_lock ; // lock for counter field
		// 0 credit initialized semaphore like to get posted (up) by the before last
		// visiting thread while the a deleting thread is waiting on it
		semaphore_t count_sem ;
		data_t data ;
		natural_t d_size ; // data size
		bool deletion ; // indicate entry is under deletion
//		mach_port_t* portlists ; // a buffer for dynamic arrays retrieving as part getting info on specific task - OMMITED
/*
		// inline constructor
		chain_entry (struct chain_entry_t* back_ent, struct chain_entry_t* for_ent, int pidd,
					 data_t dataa, natural_t d_sizee) : backward_entry(back_ent), forward_entry(for_ent),
					 pid(pidd), count(0), data(dataa), d_size(d_sizee), deletion(false) {
			semaphore_create(cur_task,&(d_lock), SYNC_POLICY_FIFO, 1) ;
			semaphore_create(cur_task,&(f_lock), SYNC_POLICY_FIFO, 1) ;
			semaphore_create(cur_task,&(count_lock), SYNC_POLICY_FIFO, 1) ;
			// designated for a deleting thread to wait on until
			// get posted by the last non deleting thread
			semaphore_create(cur_task,&(count_sem), SYNC_POLICY_FIFO, 0) ;
		}
*/
	} chain_entry_t ;

	// bucket data structure

	typedef struct bucket {
		chain_entry_t* chain ;
		semaphore_t up_lock ; // lock for updating (adding/deleting) new/existing entry on specific bucket chain
		// lock to exclude threads from visiting the first chain entry while it being critically manipulated (under deletion time)
		semaphore_t chain_lock ;
	} bucket_t ;

	bucket_t* buckets ; // inline array of buckets
 	task_t cur_task ;

public :

 	int size ; // number of buckets

 	DataTable() ;

	DataTable(int size) ;

	~DataTable()  ;

	// hashing pid value over the table to retrieve data
	// if an entry corresponding to the process not exist return null
	data_info_t getData(int pid) ;

	// add / update (in case entry exist) data for given pid
	void updateData(int pid, data_t data, natural_t d_size) ;

	// garbage collect (deallocate) data of terminated processes
	// not good collect function which keep entries of deallocated (collected) data in the chain ...
	// this can lead to bad performance within time after termination and keep servicing of more client processes
	// should be changed .

	bool deleteData(int pid) ;


	void collect() ;


private :


	// return the chain entry (within a bucket) of a specific process or null if not exist

	// caller should apply relevant count decrementing logic (optional positing)
	// on the returned entry if not null

	chain_entry_t* find(int pid) ;

	bool deleteEntry(chain_entry_t* entry) ;


} ;


#endif /* DATATABLE_H_ */
