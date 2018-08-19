#include"func.h"
int main(int argc,char **argv)
{
	args_check(argc,3);
	int sfd;
	sfd=socket(AF_INET,SOCK_STREAM,0);//创建套接字描述符
	check_error(-1,sfd,"socket");
	struct sockaddr_in ser;
	bzero(&ser,sizeof(ser));
	ser.sin_family=AF_INET;
	ser.sin_addr.s_addr=inet_addr(argv[1]);//网络地址转化
	ser.sin_port=htons(atoi(argv[2]));//主机和网络字节序转化

	int ret;
	ret=bind(sfd,(struct sockaddr*)&ser,sizeof(ser));//请求建立连接
	check_error(-1,ret,"connect");

	listen(sfd,10);
	int new_fd;
	char buf[128];
	struct sockaddr_in p;//用于存储客户端的ip和端口信息
	bzero(&p,sizeof(p));
	int len=sizeof(p);

	struct epoll_event event,evs[3];//运用epoll函数
	int epfd;
	epfd=epoll_create(1);
	event.events=EPOLLIN;
	event.data.fd=STDIN_FILENO;
	ret=epoll_ctl(epfd,EPOLL_CTL_ADD,STDIN_FILENO,&event);//将标准输入添加到epoll实例中epfd中的兴趣列表中
	check_error(-1,ret,"epoll_ctl");
	event.data.fd=sfd;
	ret=epoll_ctl(epfd,EPOLL_CTL_ADD,sfd,&event);//把文件描述符sfd添加到epoll实例中epfd中的兴趣列表中
	check_error(-1,ret,"epoll_ctl");
	int i;
	while(1)
	{
		ret=epoll_wait(epfd,evs,3,-1);
		for(i=0;i<ret;i++)
		{
			if(evs[i].data.fd==sfd)//判断描述符是否在epoll的兴趣列表中
			{
				new_fd=accept(sfd,(struct sockaddr*)&p,&len);
				event.data.fd=new_fd;
				epoll_ctl(epfd,EPOLL_CTL_ADD,new_fd,&event);
				printf("ip=%s,port=%d\n",inet_ntoa(p.sin_addr),ntohs(p.sin_port));
			}
			//每一个请求都有一个new_fd
			if(evs[i].data.fd==new_fd)//如果在就输出内容
			{
				bzero(buf,sizeof(buf));
				int res=recv(new_fd,buf,sizeof(buf),0);
				if(res>0)
				{
					printf("%s\n",buf);
				}else
				{
					event.data.fd=new_fd;
					epoll_ctl(epfd,EPOLL_CTL_DEL,new_fd,&event);
					close(new_fd);
					printf("goodbye\n");
				}
			}
			if(EPOLLIN==evs[i].events&evs[i].data.fd==STDIN_FILENO)//如果标准输入有内容则发送
			{
				bzero(buf,sizeof(buf));
				read(STDIN_FILENO,buf,sizeof(buf));
				send(new_fd,buf,strlen(buf)-1,0);//发送数据
			}
		}
	}
	close(sfd);
}
