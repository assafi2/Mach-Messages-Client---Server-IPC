#include "DataTable.h"
#include <math.h>


	DataTable::DataTable(int size) {

		this->size = size ;
		buckets = (bucket_t*)(calloc(size,sizeof(bucket_t))) ;
		cur_task = mach_task_self() ;
		// init bucket sems / chains
		for (int i = 0 ; i < size ; ++i) {
			semaphore_create(cur_task,&(buckets[i].up_lock), SYNC_POLICY_FIFO, 1) ;
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
		semaphore_wait(entry->d_lock) ;
		retData.ptr = entry->data ; // NULL in case of outdated / invalid data
		retData.size = entry->d_size ;
		semaphore_signal(entry->d_lock) ;
		// get out of the entry - decrement count
		semaphore_wait(entry->count_lock) ;
		entry->count-- ;
		if ((entry->count == 1) and (entry->deletion)) {
			semaphore_signal(entry->count_sem) ; // wake up the deleting thread
		}
		semaphore_signal(entry->count_lock) ;
		// a deleting thread should acquire count_lock before do the actual free()
		return retData ;
	}
	// add / update (in case entry exist) data for given process
	void DataTable::updateData(int pid, data_t data, natural_t d_size){

		chain_entry_t *entry = find(pid) ;
		if (entry != NULL) {
			semaphore_wait(entry->d_lock) ;
			entry->data = data ;
			entry->d_size = d_size ;
    		semaphore_signal(entry->d_lock) ;
    		semaphore_wait(entry->count_lock) ;
    		entry->count-- ;
    		if ((entry->count == 1) and (entry->deletion)) {
    			semaphore_signal(entry->count_sem) ; // wake up the deleting thread
    		}
    		semaphore_signal(entry->count_lock) ;
    		// a deleting thread should acquire count_lock before do the actual free()
    		return ;
		}
		// acquire bucket's update lock (i.e. add / delete a chain entry)
		bucket_t* b = &(buckets[pid % size]) ;
		semaphore_wait(b->up_lock) ;
		// new entry

		// initialization without constructor

		chain_entry_t *newEntry = (chain_entry_t*) malloc(sizeof(chain_entry_t)) ;
		newEntry->data = data ;
		newEntry->pid = pid ;
		newEntry->d_size = d_size ;
		newEntry->deletion = false ;
		semaphore_create(cur_task,&(newEntry->d_lock), SYNC_POLICY_FIFO, 1) ;
		semaphore_create(cur_task,&(newEntry->f_lock), SYNC_POLICY_FIFO, 1) ;
		semaphore_create(cur_task,&(newEntry->count_lock), SYNC_POLICY_FIFO, 1) ;
		// designated for a deleting thread to wait on until
		// get posted by the last non deleting thread
		semaphore_create(cur_task,&(newEntry->count_sem), SYNC_POLICY_FIFO, 0) ;

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
		semaphore_signal(b->up_lock) ;
		return ;
	}

	// delete (deallocate) saved data corresponding to a process
	bool DataTable::deleteData(int pid) {

		chain_entry_t *entry = find(pid) ;
		if (entry == NULL) return false ;
		deleteEntry(entry) ;
		return true ;
	}

	bool DataTable::deleteEntry(chain_entry_t* entry) {

	if (entry->deletion == true) return false ;  // entry already under deletion - return
	// acquire relevant bucket update lock
	bucket_t* b = &(buckets[entry->pid % size]) ;
	semaphore_wait(b->up_lock) ;
	// turn on (under) deletion flag
	entry->deletion == true ;
	// disable progress towards the "to be deleted" entry by other threads
	semaphore_wait(entry->backward_entry->f_lock) ;
	semaphore_wait(entry->count_lock) ; // acquire sync access to count var
	// remove entry from chain without interrupting currently visiting threads from progress
	entry->backward_entry->forward_entry = entry->forward_entry ;
	entry->forward_entry->backward_entry = entry->backward_entry ;
	semaphore_signal(entry->backward_entry->f_lock) ; // enable progress from previous entry
	if (entry->count > 1) { // other threads currently visiting this entry - can not deallocate
		// down on count sem, will be notified when
		// the last other thread progress from the entry
		semaphore_wait(entry->count_sem) ;   // might be done with some other technique to prevent potential starvation
	}
	// destruction logic - could be done as well with a proper destructor method
	semaphore_destroy(cur_task,entry->count_lock) ;
	semaphore_destroy(cur_task,entry->d_lock) ;
	semaphore_destroy(cur_task,entry->f_lock) ;
	semaphore_destroy(cur_task,entry->count_sem) ;
	// free client process data chunk
	// NOT WORKING AGAINST (INCOMING) MACH MESSAGE "ALOCCATED" OUT OF LINE DATA CHUNK - temporary omitted
//	if (entry->data != NULL)
//		free((void*)(entry->data)) ;
	semaphore_signal(b->up_lock) ; // finished update (release bucket update lock)
	return true ;
	}


	// garbage collect (deallocate) data of terminated processes
	// traverse the whole structure in order to delete (remove) data (entries) of terminated processes
	// in synchronized manner 

	void DataTable::collect() {

		mach_port_t task_port = 0 ;  // task related to a process holding data in an entry
		chain_entry_t *first, *entry, *next ;
		for (int i = 0 ; i < size ; ++i){

			first = buckets[i].chain ;
			if (first == NULL) continue  ;
			// traverse
			entry = first ;
			next = NULL ;
			// inc entry visit counter
			semaphore_wait(entry->count_lock) ;
			entry->count++ ;
			semaphore_signal(entry->count_lock) ;
			do {
				task_for_pid(mach_task_self(), entry->pid , &task_port) ;
				// delete entry in case of a dead process (unless under deletion)
				if (task_port == 0) { 	// process dead
					deleteEntry(entry) ;
				}
				semaphore_wait(entry->f_lock) ;
				next = entry->forward_entry ;
				semaphore_signal(entry->f_lock) ;
				entry = next ;
			} while(entry!=first) ;
		}
	}

	// return the chain entry (within a bucket) of a specific process or null if not exist

	// caller should apply relevant count decrementing logic (optional positing)
	// on the returned entry if not null

	DataTable::chain_entry_t* DataTable::find(int pid) {

		chain_entry_t *first = buckets[pid % size].chain ;

		// traverse
		if (first == NULL) return NULL ;
		chain_entry_t *entry = first ;
		chain_entry_t *next = NULL ;
		do {
			// inc entry visit counter
			semaphore_wait(entry->count_lock) ;
			entry->count++ ;
			semaphore_signal(entry->count_lock) ;
			if (entry->pid == pid) {
				return entry ;
			}

			semaphore_wait(entry->f_lock) ; // provide mutual exclusion on accessing the entry farward_entry ptr ;
			next = entry->forward_entry ;
			semaphore_signal(entry->f_lock) ;
			semaphore_wait(entry->count_lock) ;
			entry->count-- ;
			if ((entry->count == 1) and (entry->deletion)) {
				semaphore_signal(entry->count_sem) ; // wake up the deleting thread
			}
			semaphore_signal(entry->count_lock) ; // a deleting thread should acquire count_lock before do the actual free()
			entry = next ;

		} while(entry!=first) ;
		 return NULL ;
	}
