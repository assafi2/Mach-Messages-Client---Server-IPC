# Mach-Messages-Client---Server-IPC

semi finished	

----------------------------------------------------------------------------------


Client - Server system relayin on Mach Messages main IPC mechanism 


Client functionalities :


 - save data request, accept data chunk (pointer) to be saved within server process memory   

 - data receive request, recieve data saved to current client process by server (if exist), and verify its identical   

 - able to find server as a system service 


Server functionalities : 

- visible as a system service to other processes (com.apple.save_service) 

- constantly receiving (serving) client requests

- delete a client process saved data in case of its termination both normal and due to crash   

- serve multiple client requests concurrently (not blocked by a single client)     

- support copy on write optimizations for clients data  

- effective concurrency in aspects of shared data manipulation by multiple threads  



