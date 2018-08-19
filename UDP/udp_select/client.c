#include"func.h"
int main(int argc,char **argv)
{
	args_check(argc,3);
	int sfd;
	sfd=socket(AF_INET,SOCK_DGRAM,0);//创建套接字描述符
	check_error(-1,sfd,"socket");
	struct sockaddr_in ser;
	bzero(&ser,sizeof(ser));
	ser.sin_family=AF_INET;
	ser.sin_addr.s_addr=inet_addr(argv[1]);//网络地址转化
	ser.sin_port=htons(atoi(argv[2]));//主机和网络字节序转化
	int ret;
	char buf[128];
	struct sockaddr_in server;
	int len=sizeof(struct sockaddr);
	fd_set rset;
	while(1)//客户端不退出
	{
		FD_ZERO(&rset);
		FD_SET(STDIN_FILENO,&rset);
		FD_SET(sfd,&rset);
		int rret=select(sfd+1,&rset,NULL,NULL,NULL);
		if(rret>0)
		{
			if(FD_ISSET(sfd,&rset))
			{
				bzero(buf,sizeof(buf));
				int res=recvfrom(sfd,buf,sizeof(buf),0,(struct sockaddr*)&server,&len);
				if(res>0)
				{
					printf("%s\n",buf);
				}else
				{
					printf("bybai\n");
					break;
				}
			}
			if(FD_ISSET(STDIN_FILENO,&rset))
			{
				bzero(buf,sizeof(buf));
				read(STDIN_FILENO,buf,sizeof(buf));
				sendto(sfd,buf,strlen(buf)-1,0,(struct sockaddr*)&ser,sizeof(struct sockaddr));//发送数据
			}
		}
	}
	close(sfd);
}
