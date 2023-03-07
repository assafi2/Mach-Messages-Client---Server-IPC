
/*
 * client handler class for save service (com.apple.save_service)
 * expose public interface for SaveClient (direct client of the service)
 * results in separate compilation
 *
 * we assume client dont make a subsequent send(data) op in case it did a receive op
 * which hasn't been completed (i.e. verified incoming data) yet
 * otherwise receive op can result in mismatched data
 * TODO apply with locks
 */


#ifndef SAVE_CLIENT_HANDLER_H
#define SAVE_CLIENT_HANDLER_H

// avoid including defs header

#ifndef DATA_DEF
#define DATA_DEF

typedef uint64_t data_t ;  // raw data chunk

// struct to hold data chunk ptr and size

typedef struct data_info {
	data_t ptr ;
	natural_t size ;
} data_info_t;

#endif


class SaveClientHandler {


	class SaveClient ;  // declaration

	SaveClient* client ;

public :

	SaveClientHandler(char* sevice_name) ;

	~SaveClientHandler() ;

	// 'save' method
	// accepts (raw) data to save pointer and the data size
	// asynchronous
	bool send(data_t data, int size);

	/* asking for saved data to be received
	 * return the received chunk in case its equal to the last sent (saved) before the actual receive op,
	 * otherwise null . There is no guarantee (for data returning) in case of ambiguities such as if 's', 'r' ops being sent fast
	 * there also possibility for not receiving the last save, therefore returning null
	 *
	 * synchronous (with timeout)
	 */
	data_info_t receive() ;

} ;

#endif
