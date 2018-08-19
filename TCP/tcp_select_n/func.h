#include<stdio.h>
#include<sys/epoll.h>
#include<netdb.h>
#include<sys/socket.h>
#include<netinet/in.h>
#include<arpa/inet.h>
#include<wait.h>
#include<signal.h>
#include<sys/msg.h>
#include<sys/sem.h>
#include<sys/shm.h>
#include<sys/ipc.h>
#include<stdlib.h>
#include<string.h>
#include<sys/time.h>
#include<sys/stat.h>
#include<sys/types.h>
#include<unistd.h>
#include<dirent.h>
#include<fcntl.h>
#include<errno.h>
#include<time.h>
#include<sys/select.h>
#define args_check(a,b) {if(a!=b) {printf("error args\n");return -1;}}
#define check_error(ret_val,ret,func_name) {if(ret_val==ret)\
	{perror(func_name);return -1;}}
