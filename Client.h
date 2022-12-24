#include<mach/port.h>
#include<mach/task.h>

#ifndef CLIENT_H_
#define CLIENT_H_

/* abstract client class include registration logic to a service by a given service name
   NO need for abstract resources cleaning logic
*/

class Client{

protected:

	mach_port_t bs_port ;  // bootstrap port
	mach_port_t service_port  ; // service port
	task_t cur_task  ;  // task's port


public:

	Client(char* service_name) ;
//	~Client() ;

	// getters for handler classes
	mach_port_t get_bs_port() ;
	mach_port_t get_service_port() ;
	mach_port_t get_cur_task() ;

} ;



#endif /* CLIENT_H_ */
