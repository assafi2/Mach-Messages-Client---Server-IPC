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

	data_info_t DataTable::getData(int pid) {

		data_info_t retData ;
		retData.ptr = NULL ;
		chain_entry_t *entry = find(pid) ;
		if (entry == NULL) {
			return retData ; // d_size field value garbage
		}
		semaphore_wait(entry->lock) ;
		retData.ptr = entry->data ; // NULL in case of outdated / invalid data
		retData.size = entry->d_size ;
		semaphore_signal(entry->lock) ;

		return retData ;
	}
	// add / update (in case entry exist) data for given process
	void DataTable::updateData(int pid, data_t data, natural_t d_size){

		chain_entry_t *entry = find(pid) ;
		if (entry != NULL) {
			semaphore_wait(entry->lock) ;
			entry->data = data ;
			entry->d_size = d_size ;
    		semaphore_signal(entry->lock) ;
			return ;
		}
		// acquire bucket's addlock
		bucket_t* b = &(buckets[pid % size]) ;
		semaphore_wait(b->addlock) ;
		// new entry
		chain_entry_t *newEntry = (chain_entry_t*) malloc(sizeof(chain_entry_t)) ;
		newEntry->data = data ;
		newEntry->pid = pid ;
		newEntry->d_size = d_size ;
		semaphore_create(cur_task,&(newEntry->lock), SYNC_POLICY_FIFO, 1) ;

		// add logic
		if (b->chain == NULL)
		{
			newEntry->forward_entry = newEntry ;
			newEntry->backward_entry = newEntry ;
			b->chain = newEntry ;

		}
		else {
			newEntry->forward_entry = b->chain ; // first pointing from new entry
			newEntry->backward_entry = b->chain->backward_entry ;
			b->chain->backward_entry = newEntry ;
			b->chain->backward_entry->forward_entry = newEntry ; // after pointing to new entry
		}
		semaphore_signal(b->addlock) ;

		return ;
	}

	// delete (deallocate) saved data corresponding to a process
	bool DataTable::deleteData(int pid) {

		chain_entry_t *entry = find(pid) ;
		if (entry == NULL) return false ;
		semaphore_wait(entry->lock) ;
		if (entry->data != NULL) {
			free((void*)(entry->data)) ;
			entry->data = NULL ;
		}
		semaphore_signal(entry->lock) ;
		return true ;
	}


	// garbage collect (deallocate) data of terminated processes
	// not good collect function which keep entries of deallocated (collected) data in the chain ...
	// this can lead to bad performance within time after termination and keep servicing of more client processes
	// should be changed .

	void DataTable::collect() {


		// IRELEVENT
		// for retrieving info on a specific thread
		// list of threads as an array of mach_pot_t, limited to hold 30 elem
//	    thread_act_port_array_t tlist = (thread_act_t*)calloc(30,sizeof(mach_port_t)) ;
//		mach_msg_type_number_t tcount ; // threads count

		mach_port_t task_port = 0 ;  // task related to a process holding data in an entry

		for (int i = 0 ; i < size ; ++i){

			chain_entry_t *first = buckets[i].chain ;
			chain_entry_t *entry = first ;
			// traverse
			if (first == NULL) continue  ;
			do {


				task_for_pid(mach_task_self(), entry->pid , &task_port) ;
				// delete data in case of a dead process
				if (task_port == 0) { 	// process dead acquire lock on data entry
	//				cout << "collecting entry of dead process  : " << entry->pid << endl ;
					semaphore_wait(entry->lock) ;
	//				cout << "data ptr value of a collected entry : " << entry->data << endl ;
					if (entry->data != NULL) { // delete
		// entry cleaning logic not working properly SKIPPED for now
		// (otherwise can crush server) NO ENTRY CLEANING LOGIC APPLIED

                //			        free((void*)(entry->data)) ;

		//				cout << "in collect delete data, data ptr :" << entry->data << endl ;  // debug purpose

		//				entry->data = 0 ;
					}
					semaphore_signal(entry->lock) ;

				}
					entry = entry->forward_entry ;

			} while(entry!=first) ;
		}
	}

	// return the chain entry (within a bucket) of a specific process or null if not exist
	DataTable::chain_entry_t* DataTable::find(int pid) {

		chain_entry_t *first = buckets[pid % size].chain ;

		// traverse
		if (first == NULL) return NULL ;
		chain_entry_t *entry = first ;
		do {
			if (entry->pid == pid) {
				return entry ;
			}
			entry = entry->forward_entry ;
		} while(entry!=first) ;
		 return NULL ;
	}


