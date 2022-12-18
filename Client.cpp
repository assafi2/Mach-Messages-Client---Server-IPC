#include <iostream>
#include "Client.h"
#include <servers/bootstrap.h>
#include <servers/bootstrap_defs.h>
#include <mach/mach_init.h>
#include <mach/task_special_ports.h>  // task get bp

using namespace std ;

	Client::Client(char* service_name) {

		cur_task = mach_task_self() ;

		if (task_get_special_port(cur_task, TASK_BOOTSTRAP_PORT, &bs_port)
				!= KERN_SUCCESS) {
			exit(EXIT_FAILURE);
		}

		// asking for service port

		if (bootstrap_look_up(bs_port, service_name, &service_port)
				!= KERN_SUCCESS) {
			exit(EXIT_FAILURE);
		}


	}




