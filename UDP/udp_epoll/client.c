#include"func.h"
int main(int argc,char **argv)
{
	args_check(argc,3);
	int sfd=socket(AF_INET,SOCK_DGRAM,0);
	check_error(-1,sfd,"socket");
	struct sockaddr_in ser;
	bzero(&ser,sizeof(ser));
	ser.sin_family=AF_INET;
	ser.sin_port=htons(atoi(argv[2]));
	ser.sin_addr.s_addr=inet_addr(argv[1]);
	struct epoll_event event,evs[2];
	int epfd;
	epfd=epoll_create(1);
	event.events=EPOLLIN;
	event.data.fd=STDIN_FILENO;
	int ret=epoll_ctl(epfd,EPOLL_CTL_ADD,STDIN_FILENO,&event);
	check_error(-1,ret,"epoll_ctl");
	event.data.fd=sfd;
	ret=epoll_ctl(epfd,EPOLL_CTL_ADD,sfd,&event);
	check_error(-1,ret,"epoll_ctl");
	char buf[128]={0};
	int i;
	while(1)
	{
		ret=epoll_wait(epfd,evs,2,-1);
		for(i=0;i<ret;i++)
		{
			if(evs[i].data.fd==sfd)
			{
			bzero(buf,sizeof(buf));
			int res=recvfrom(sfd,buf,sizeof(buf),0,NULL,NULL);
			check_error(-1,res,"recvfrom");
			printf("%s\n",buf);
			}
			if(evs[i].data.fd==STDIN_FILENO)
			{
				bzero(buf,sizeof(buf));
				read(STDIN_FILENO,buf,sizeof(buf));
				sendto(sfd,buf,sizeof(buf),0,(struct sockaddr*)&ser,sizeof(struct sockaddr));
			}
		}
	}
	close(sfd);
}
