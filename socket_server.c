/*************************************************
Copyright (C), 2007-2017, ShenZhen Hymson Laser Technologies Co., Ltd.
File name: socket_server.c
Author:
Version:
Date: 2017.06.13
Description:
Others:

Function List:
1. ....
History:
Date:
Author:
Modification:

2. ...
*************************************************/

#include <fcntl.h>
#include <sys/ioctl.h>

#include <netinet/in.h>     /* sockaddr_in{} and other Internet defns */
#include <sys/types.h>      /* basic system data types */
#include <sys/socket.h>     /* basic socket definitions */
#include <arpa/inet.h>      /* inet(3) functions */

#include <unistd.h>         /* for read/write/close */
#include <stdio.h>          /* for printf */
#include <stdlib.h>         /* for exit */
#include <errno.h>          /* for errno */
#include <string.h>         /* for bzero */
#include <pthread.h>
#include <signal.h>

#include "gen_helper.h"
#include "parse_stream.h"
#include "thread_pool.h"

#define SYSTEMCFG_INI           "/data/syscfg.ini"

#define PARSE_SERVER_PORT       8888
#define OMC_SERVER_PORT         8889

#define LENGTH_OF_LISTEN_QUEUE  4

#define MAX_THREADPOOL          4

int fpga_fd = -1;

void handle(int socket_id);

void thread_parse_server(void);
void thread_omc_server(void);

pool_t g_threadpool;

void fpga_reset(void)
{
    if (access("/sys/class/gpio/gpio238/direction", 0) == -1)
        system("echo 238 > /sys/class/gpio/export");
    
    system("echo out > /sys/class/gpio/gpio238/direction");
    system("echo 0 > /sys/class/gpio/gpio238/value");
    usleep(1000);
    
    system("echo 1 > /sys/class/gpio/gpio238/value");
}

int fpga_init(char * devname)
{
    fpga_reset();
    
    fpga_fd = open(devname, O_RDWR);
    if (fpga_fd < 0)
    {
        return -1;
    }
    
    printf("%s: succeed\n", __func__);
    
    return 0;
}

int fpga_term(void)
{
    close(fpga_fd);
    fpga_fd = -1;
    
    return 0;
}

static void thread_parse_file_create(int fpga_fd)
{
    pthread_t thread_id = 0;
    pthread_attr_t attr;
    
    if (thread_id)
    {
        pthread_cancel(thread_id);
    }
    
    pthread_attr_init(&attr);
    pthread_attr_setstacksize(&attr, THREADSTACKSIZE*2);
    
    if (pthread_create(&thread_id, &attr, (void*)&parse_file, (void*)fpga_fd))
    {
        PRINTFLOG("ERROR: can't create parse_file thread!\n");
    }
    else
    {
        PRINTFLOG("thread_parse_file started!\n");
        pthread_detach(thread_id);
    }
}

static void thread_parseserver_create(void)
{
    pthread_t thread_id = 0;
    pthread_attr_t attr;
    
    if (thread_id)
    {
        pthread_cancel(thread_id);
    }
    
    pthread_attr_init(&attr);
    pthread_attr_setstacksize(&attr, THREADSTACKSIZE/4);
    
    if (pthread_create(&thread_id, &attr, (void*)&thread_parse_server, (void*)NULL))
    {
        PRINTFLOG("ERROR: can't create parse_server thread!\n");
    }
    else
    {
        PRINTFLOG("thread_parse_server started!\n");
        pthread_detach(thread_id);
    }
}

static void thread_omcserver_create(void)
{
    pthread_t thread_id = 0;
    pthread_attr_t attr;
    
    if (thread_id)
    {
        pthread_cancel(thread_id);
    }
    
    pthread_attr_init(&attr);
    pthread_attr_setstacksize(&attr, THREADSTACKSIZE/4);
    
    if (pthread_create(&thread_id, &attr, (void*)&thread_omc_server, (void*)NULL))
    {
        PRINTFLOG("ERROR: can't create omc_server thread!\n");
    }
    else
    {
        PRINTFLOG("thread_omc_server started!\n");
        pthread_detach(thread_id);
    }
}

int main(int argc, char **argv)
{
    char strValue[32];
    char pBuffer[MAX_LENGTH];
    int  ret;
    
    // Initialize logfile_lock
    sem_init(&logfile_lock, 0, 1);
    
    // Get Skip Tape Flag
    if (GetProfileString(SYSTEMCFG_INI, "system", "isskiptape", strValue) != 0)
        isskiptapeflag = 1;
    else
        isskiptapeflag = (atoi(strValue) ? 1 : 0);
    
    // Get Skip Tape Level Switch
    if (GetProfileString(SYSTEMCFG_INI, "system", "skiptapeswitch", strValue) != 0)
        skiptapeswitch = 0;
    else
        skiptapeswitch = (atoi(strValue) ? 1 : 0);
    
    // Get Unself Trigger FPGA Version Flag
    if (GetProfileString(SYSTEMCFG_INI, "system", "fpgaversion", strValue) != 0)
        utfpgaversion = 0;
    else
        utfpgaversion = atoi(strValue);
    
		 // Get AutoTrigger Flag
    if (GetProfileString(SYSTEMCFG_INI, "system", "AutoTrigger", strValue) != 0)
        isAutoTrigger = 0;
    else
        isAutoTrigger = atoi(strValue);

    // Get Begin Index Flag
    if (GetProfileString(SYSTEMCFG_INI, "trimmer", "beginindex", strValue) != 0)
        beginindex = 0;
    else
        beginindex = atoi(strValue);

	// Get Begin Index Flag
	if (GetProfileString(SYSTEMCFG_INI, "trimmer", "capeindex", strValue) != 0)
		capeindex = 0;
	else
		capeindex = atoi(strValue);
    
    // Write the running information to logfile
    PRINTNEWLINELOG("**********  ShenZhen Hymson Laser Technologies Co., Ltd.  **********\n");
    
    sprintf(pBuffer, "fpga deamon starting, version date: [%s] base on linkage_v1.1\n", __DATE__);
    PRINTFLOG(pBuffer);
    
    sprintf(pBuffer, "[isskiptapeflag:%d] [skiptapeswitch:%d] [utfpgaversion:%d] [beginindex:%d] [capeindex:%d]\n", isskiptapeflag, skiptapeswitch, utfpgaversion, beginindex,capeindex);
    PRINTFLOG(pBuffer);
    
    // Initialize the FPGA
    ret = fpga_init("/dev/fpga");
    if (ret < 0)
    {
        PRINTFLOG("open device fpga error!\n");
		sleep(1);
        
        exit(1);
    }
    
    parse_init(fpga_fd);
    
    // create file handle thread.
    thread_parse_file_create(fpga_fd);
    
    // create threads pool
    pool_init(&g_threadpool, MAX_THREADPOOL);
    
    // create parse server thread.
    thread_parseserver_create();
    
    // create operation maintaince server thread.
    thread_omcserver_create();
    
    // logfile handle.
    logfile_proc();
    
    // destroy the threads pool
    pool_uninit(&g_threadpool);
    
    // 释放申请的内存空间
    free(gLoopMemLog.m_head);
    
    PRINTFLOG("fpga deamon exit.\n\n");
    
    return 0;
}

void thread_parse_server(void)
{
    struct sockaddr_in server_addr;
    char pBuffer[MAX_LENGTH];
    
    // Setting the contents of a memory area to 0
    bzero(&server_addr, sizeof(server_addr));
    
    // Setting a socket address structure server_addr, on behalf of the server internet address, port
    server_addr.sin_family      = AF_INET;
    server_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    server_addr.sin_port        = htons(PARSE_SERVER_PORT);
    
    // Create a stream protocol (TCP) socket for internet, with server_socket on behalf of the server socket
    int server_socket = socket(PF_INET, SOCK_STREAM, 0);
    if (server_socket < 0)
    {
        PRINTFLOG("Create Socket Failed!\n");
        exit(1);
    }
    
    // Link the server_socket and server_addr address structures
    if (bind(server_socket,(struct sockaddr*)&server_addr, sizeof(server_addr)))
    {
        sprintf(pBuffer, "Server Bind Port : %d Failed!\n", PARSE_SERVER_PORT);
        PRINTFLOG(pBuffer);
        exit(1);
    }
    
    // server_socket is used for listening
    if (listen(server_socket, LENGTH_OF_LISTEN_QUEUE))
    {
        PRINTFLOG("Server Listen Failed!\n"); 
        exit(1);
    }
    
    // the server deamon should always running
    while (1)
    {
        // define the client socket address structure client_addr
        struct sockaddr_in client_addr;
        socklen_t length = sizeof(client_addr);
        
        // the accept function returns a new socket, the socket (new_server_socket) used to communicate with the connected client
        // new_server_socket represents a communication channel between the server and the client
        // accept function to connect to the client information to fill in the client socket address structure client_addr
        int new_server_socket = accept(server_socket, (struct sockaddr*)&client_addr, &length);
        if (new_server_socket < 0)
        {
            PRINTFLOG("Server Accept Failed!\n");
            continue;
        }
        
        sprintf(pBuffer, "accept from %s:%d\n", inet_ntoa(client_addr.sin_addr), ntohs(client_addr.sin_port));
        PRINTFLOG(pBuffer);
        
        // process the request
        //handle(new_server_socket);
        pool_add_task(&g_threadpool, handle, new_server_socket);
    }
    
    // close the listenning socket.
    close(server_socket);
}

void thread_omc_server(void)
{
    struct sockaddr_in server_addr;
    char pBuffer[MAX_LENGTH];
    
    // Setting the contents of a memory area to 0
    bzero(&server_addr, sizeof(server_addr));
    
    // Setting a socket address structure server_addr, on behalf of the server internet address, port
    server_addr.sin_family      = AF_INET;
    server_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    server_addr.sin_port        = htons(OMC_SERVER_PORT);
    
    // Create a stream protocol (TCP) socket for internet, with server_socket on behalf of the server socket
    int server_socket = socket(PF_INET, SOCK_STREAM, 0);
    if (server_socket < 0)
    {
        PRINTFLOG("[OMC]Create Socket Failed!\n");
        exit(1);
    }
    
    // Link the server_socket and server_addr address structures
    if (bind(server_socket,(struct sockaddr*)&server_addr, sizeof(server_addr)))
    {
        sprintf(pBuffer, "[OMC]Server Bind Port : %d Failed!\n", OMC_SERVER_PORT);
        PRINTFLOG(pBuffer);
        exit(1);
    }
    
    // server_socket is used for listening
    if (listen(server_socket, LENGTH_OF_LISTEN_QUEUE))
    {
        PRINTFLOG("[OMC]Server Listen Failed!\n");
        exit(1);
    }
    
    // the server deamon should always running
    while (1)
    {
        // define the client socket address structure client_addr
        struct sockaddr_in client_addr;
        socklen_t length = sizeof(client_addr);
        
        // the accept function returns a new socket, the socket (new_server_socket) used to communicate with the connected client
        // new_server_socket represents a communication channel between the server and the client
        // accept function to connect to the client information to fill in the client socket address structure client_addr
        int new_server_socket = accept(server_socket, (struct sockaddr*)&client_addr, &length);
        if (new_server_socket < 0)
        {
            PRINTFLOG("[OMC]Server Accept Failed!\n");
            continue;
        }
        
        sprintf(pBuffer, "[OMC]accept from %s:%d\n", inet_ntoa(client_addr.sin_addr), ntohs(client_addr.sin_port));
        PRINTFLOG(pBuffer);
        
        // process the request
        //omc_handle(new_server_socket);
        pool_add_task(&g_threadpool, omc_handle, new_server_socket);
    }
    
    // close the listenning socket.
    close(server_socket);
}

void handle(int socket_id)
{
    ssize_t recvLength;
    char pBuffer[MAX_LENGTH];
    int  ret, filelen;
    
    while (1)
    {
        recvLength = read(socket_id, pBuffer, MAX_LENGTH);
        
        // Server Recieve Data Failed!
        //if (((recvLength < 0) && (errno != EINTR)) || (recvLength == 0))
        if (recvLength <= 0)
            break;
        
        if (recvLength > 0)
        {
            ret = parse_stream(pBuffer, recvLength, &filelen);
            if(ret == -EAGAIN)
            {
                printf("mark is running!\n");
                
                //notify PC software, marking is in progress
                write(socket_id, "1", 1);
            }
            else if(ret == 2)
            {
                //解析脱机文件
                countdown2_buf_pos = 0;
                beginindex_buf_pos = 0;
                cuttingout_buf_pos = 0;
                
                //等待相关资源数据
                sleep(1);
                
                ret = func_getcd32pos();
                if (ret == -EAGAIN)
                {
                    printf("mark is running!\n");
                    write(socket_id, "1", 1);
                }
                else
                {
                    //upload the end of the return
                    write(socket_id, "0", 1);
                    
                    sprintf(pBuffer, "Upload file success!!! Lase file size: %d bytes.\n", filelen);
                    PRINTFLOG(pBuffer);
                }
            }
        }
        
        usleep(1000);
    }
    
    // close client socket.
    close(socket_id);
    PRINTFLOG("client exit!\n");
}

