#include "DataTable.h"
#include <math.h>


	DataTable::DataTable(int size) {
		this->size = size ;
		buckets = (bucket_t*)(calloc(size,sizeof(bucket_t))) ;
		cur_task = mach_task_self() ;
		// init bucket sems / chains
		for (int i = 0 ; i < size ; ++i) {
			semaphore_create(cur_task,&(buckets[i].addlock), SYNC_POLICY_FIFO, 1) ;
			buckets[i].chain = NULL ;
		}
	}

	DataTable::DataTable() {
		DataTable(TABLE_SIZE) ;
	}

	DataTable::~DataTable() {
		free(buckets) ;
	}

	data_t DataTable::getData(task_t task) {

		cout << "in get data " << endl ;
		chain_entry_t *entry = find(task) ;
		if (entry == NULL) {
			return NULL ;
		}
		semaphore_wait(entry->lock) ;
		if (entry->data = NULL) return NULL ; // outdated
		semaphore_signal(entry->lock) ;
		return entry->data ;
	}
	// add / update (in case entry exist) data for given task
	void DataTable::updateData(task_t task, data_t data){

		chain_entry_t *entry = find(task) ;

		if (entry != NULL) {
			semaphore_wait(entry->lock) ;
			entry->data = data ;
    		semaphore_signal(entry->lock) ;
			return ;
		}
		// acquire bucket's addlock
		bucket_t b = buckets[task % size] ;
		semaphore_wait(b.addlock) ;
		// new entry
		chain_entry_t *newEntry = (chain_entry_t*) malloc(sizeof(chain_entry_t)) ;
		newEntry->data = data ;
		newEntry->task = task ;
		semaphore_create(cur_task,&(newEntry->lock), SYNC_POLICY_FIFO, 1) ;

		// add logic
		if (b.chain == NULL)
		{
			newEntry->forward_entry = newEntry ;
			newEntry->backward_entry = newEntry ;
			b.chain = newEntry ;
	//		cout << "task for update " << task << " size of buckets " << size << endl ;

		}
		else {
			newEntry->forward_entry = b.chain ; // first pointing from new entry
			newEntry->backward_entry = b.chain->backward_entry ;
			b.chain->backward_entry = newEntry ;
			b.chain->backward_entry->forward_entry = newEntry ; // after pointing to new entry
		}
		semaphore_signal(b.addlock) ;

		return ;
	}

	// delete (deallocate) saved data corresponding to a task
	bool DataTable::deleteData(task_t task) {

		chain_entry_t *entry = find(task) ;
		if (entry == NULL) return false ;
		semaphore_wait(entry->lock) ;
		if (entry->data != NULL) {
			free((void*)(entry->data)) ;
			entry->data = NULL ;
		}
		semaphore_signal(entry->lock) ;
		return true ;
	}


	// grabage collect (deallocate) data of terminated tasks
	// not good collect function which keep entries of deallocated (collected) data in the chain ...
	// this can lead to bad performance within time after termination and keep servicing of more client processes
	// should be changed .

	void DataTable::collect() {

		// for retrieving info on a specific thread
		// list of threads as an array of mach_pot_t, limited to hold 30 elem
	    thread_act_port_array_t tlist = (thread_act_t*)calloc(30,sizeof(mach_port_t)) ;
		mach_msg_type_number_t tcount ; // threads count

		for (int i = 0 ; i < size ; ++i){

			chain_entry_t *first = buckets[i].chain ;
			chain_entry_t *entry = first ;
			// traverse
			if (first == NULL) continue  ;
			do {
				if (task_threads(entry->task,&tlist,&tcount) == KERN_SUCCESS) {
					cout << "task " << entry->task << " has total of " << tcount << " threads" << endl ;
					if (tcount == 0 ) {  // task dead acquire lock on data entry
		 				semaphore_wait(entry->lock) ;   // can not call semaphore wait not compiling any reason ?!
						if (entry->data != NULL) {
							free((void*)(entry->data)) ;
							entry->data = NULL ;
						}
						semaphore_signal(entry->lock) ;
						}
					}
					entry = entry->forward_entry ;
				 } while(entry!=first) ;
		}
		 free(tlist) ;
	}

	// return the chain entry (within a bucket) of a specific task or null if not exist
	DataTable::chain_entry_t* DataTable::find(task_t task) {

		chain_entry_t *first = buckets[task % size].chain ;
		// traverse
		if (first == NULL) return NULL ;
		chain_entry_t *entry = first ;
		do {
			if (entry->task == task) {
				return entry ;
			}
			entry = entry->forward_entry ;
		} while(entry!=first) ;
		 return NULL ;
	}


