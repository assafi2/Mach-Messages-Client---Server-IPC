#include "DataTable.h"
#include <math.h>
#include <signal.h> // instead of using possibly unsupported by default function task_for_pid


	DataTable::DataTable(int size) {

		this->size = size ;
		buckets = (bucket_t*)(calloc(size,sizeof(bucket_t))) ;
		cur_task = mach_task_self() ;
		// init bucket sems / chains
		for (int i = 0 ; i < size ; ++i) {
			semaphore_create(cur_task,&(buckets[i].up_lock), SYNC_POLICY_FIFO, 1) ;
			semaphore_create(cur_task,&(buckets[i].chain_lock), SYNC_POLICY_FIFO, 1) ;
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
		newEntry->count = 0 ;
		newEntry->deletion = false ;
		newEntry->post_count_sem = false ;
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

	if (entry->deletion == true) {
		return false ;  // entry already under deletion - return
	}
	// acquire relevant bucket update lock
	bucket_t* b = &(buckets[entry->pid % size]) ;
	semaphore_wait(b->up_lock) ;
	// turn on (under) deletion flag
	entry->deletion = true ;
	// unique delete logic to be applied in case its a ONE entry chain
	if ((entry->forward_entry) == entry) {
	//	cout << "one entry chain" << endl ;
		// acquire bucket chain lock for disabling threads enter into the chain
		semaphore_wait(b->chain_lock) ;
		b->chain = NULL ;
		semaphore_signal(b->chain_lock) ;
	}

	else { // chain holds at list two entries
		// disable progress towards the "to be deleted" entry by other threads
		semaphore_wait(entry->backward_entry->f_lock) ;
		// same for incoming threads from the bucket in case its the first chain entry
		if (entry == b->chain)
			semaphore_wait(b->chain_lock) ;

		// remove entry from chain without interrupting currently visiting threads from progress
		entry->backward_entry->forward_entry = entry->forward_entry ;
		entry->forward_entry->backward_entry = entry->backward_entry ;
		if (entry == b->chain) // re reference chain from bucket in case first entry has been removed
			b->chain = entry->forward_entry ;
		semaphore_signal(entry->backward_entry->f_lock) ; // enable progress from previous entry
		if (entry == b->chain) // same for incoming threads from bucket in case first entry has been removed
			semaphore_signal(b->chain_lock) ;
	}
	semaphore_wait(entry->count_lock) ; // acquire sync access to count var
	if (entry->count > 1) { // other threads currently visiting this entry - can not deallocate
		// can cause dead lock in case a context switch occur and all currently visiting threads
		// left entry before current deleting thread manage to wait on count_sem - should raise a flag for this purpose
		semaphore_signal(entry->count_lock) ;
		// down on count sem, will be notified when
		// the last other thread progress from the entry
		semaphore_wait(entry->count_sem) ;   // consider using the flag below (post_count_sem) to prevent potential starvation
		entry->post_count_sem = true ; // raise flag to end a possible "busy waking up"
		// aquire count lock to let the last thread ("busy waker") which hold this lock leave
		semaphore_wait(entry->count_lock) ;
				if (entry->count == 1) entry->count-- ;
		semaphore_signal(entry->count_lock) ;
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
	free (entry) ; // free entry chunk
	return true ;
	}

	// garbage collect (deallocate) data of terminated processes
	// traverse the whole structure in order to delete (remove) data (entries) of terminated processes
	// in synchronized manner

	void DataTable::collect() {

		mach_port_t task_port = 0 ;  // task related to a process holding data in an entry
		chain_entry_t *first, *entry, *next ;
		for (int i = 0 ; i < size ; ++i){
			// acquire chain lock
			semaphore_wait(buckets[i].chain_lock) ;
			chain_entry_t *first = buckets[i].chain ;
			semaphore_signal(buckets[i].chain_lock) ;
			if (first == NULL) continue  ;
		//	cout << "bucket  # " << i << " has chain" << endl ; // test purpose
			// traverse
			entry = first ;
			next = NULL ;
			// inc entry visit counter
			do {
				semaphore_wait(entry->count_lock) ;
				entry->count++ ;
				semaphore_signal(entry->count_lock) ;
				// bad return (kern_return_t) value due to changes in modern OSs,
				// should make configuration to allow proper usage of this func
		//		task_for_pid(mach_task_self(), entry->pid , &task_port) ;
		//		int pStatus  = kill(entry->pid,0) ; // testing purpose
		//		cout << "process " << entry->pid << " kill return val : " << pStatus << endl ; // testing purpose
				// instead usuing unix signals kill func to check if process pid isn't terminated
				// delete entry in case of a dead process (unless under deletion)
				if (kill(entry->pid,0) != 0 /* task_port == 0 (?) */) { 	// process dead
					cout << "collector thread found an invalid data entry of DEAD client process num : "
							<< entry->pid << " to be removed" << endl ;
					semaphore_wait(entry->f_lock) ;
					next = entry->forward_entry ;
					semaphore_signal(entry->f_lock) ;
					deleteEntry(entry) ;
				}
				else {
					// exit entry logic (considering the case where the entry turned under deletion by another thread while visiting)
					semaphore_wait(entry->f_lock) ;
					next = entry->forward_entry ;
					semaphore_signal(entry->f_lock) ;
					semaphore_wait(entry->count_lock) ;
					entry->count-- ;
					if ((entry->count == 1) and (entry->deletion == true)) {
						// apply "busy waking up" to prevent starvation of the deleting thread
						while (!(entry->post_count_sem)) // post_count_sem
							semaphore_signal(entry->count_sem) ; // wake up the deleting thread
						}
					semaphore_signal(entry->count_lock) ; // a deleting thread should acquire count_lock before do the actual free()
				}

				entry = next ;
			} while(entry!=first) ;
		}
	}

	// return the chain entry (within a bucket) of a specific process or null if not exist

	// caller should apply relevant count decrementing logic (optional positing)
	// on the returned entry if not null

	DataTable::chain_entry_t* DataTable::find(int pid) {

		if (pid <= 0)
			return NULL ; // validity check
		bucket *b = &(buckets[pid % size]) ;
		// acquire chain lock
		semaphore_wait(b->chain_lock) ;
		chain_entry_t *first = b->chain ;
		semaphore_signal(b->chain_lock) ;
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
			if ((entry->count == 1) and (entry->deletion == true)) {
				// apply "busy waking up" to prevent starvation of the deleting thread
				while (!(entry->post_count_sem)) // post_count_sem
					semaphore_signal(entry->count_sem) ; // wake up the deleting thread
			}
			semaphore_signal(entry->count_lock) ; // a deleting thread should acquire count_lock before do the actual free()
			entry = next ;

		} while(entry!=first) ;
		 return NULL ;
	}
