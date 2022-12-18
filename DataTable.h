
#ifndef DATATABLE_H_
#define DATATABLE_H_


#include <mach/port.h>
// #include <mach/task_special_ports.h>  // task get bp
#include <servers/bootstrap.h>
#include <servers/bootstrap_defs.h>
#include <mach/mach_init.h>
#include <mach/task.h>
#include <pthread.h>
#include <mach/semaphore.h>
#include "saveServiceDefinitions.h"


#include <iostream>

using namespace std ;


 /*
 * Hash table to hold data entries
 * table will contain fixed number of buckets and resolve collisions with chaining
 * do not support dynamic number of buckets
 * a task can have at most 1 data entry in the table, tasks ports (task_t) are the table keys
 */

#define TABLE_SIZE 30

class DataTable {

	// doubly linked list for bucket_t entries

	typedef struct chain_entry {
		struct chain_entry* backward_entry ;
		struct chain_entry* forward_entry ;
		task_t task ;
		semaphore_t lock ;
		data_t data ;
		mach_port_t* portlists ; // a buffer for dynamic arrays retrieving as part getting info on specific task
	} chain_entry_t ;

	// bucket data structure

	typedef struct bucket {
		chain_entry_t* chain ;
		semaphore_t addlock ;  // lock for adding new entry on specific bucket chain
	} bucket_t ;

	bucket_t* buckets ; // inline array of buckets
 	task_t cur_task ;

public :

 	int size ; // number of buckets


 	DataTable() ;

	DataTable(int size) ;

	~DataTable()  ;

	// hashing task (port num) value over the table to retreive data
	// if an entry corresponding to the task not exit return null
	data_t getData(task_t task) ;

	// add / update (in case entry exist) data for given task
	void updateData(task_t task, data_t data) ;

	// grabage collect (deallocate) data of terminated tasks
	// not good collect function which keep entries of deallocated (collected) data in the chain ...
	// this can lead to bad performance within time after termination and keep servicing of more client processes
	// should be changed .

	bool deleteData(task_t) ;

	void collect() ;


private :

	// return the chain entry (within a bucket) of a specific task or null if not exist
	chain_entry_t* find(task_t task) ;

} ;





#endif /* DATATABLE_H_ */
