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
	fd_set fdr;
	new_fd=accept(sfd,(struct sockaddr*)&p,&len);
	while(1)//客户端不退出
	{
		FD_ZERO(&fdr);
		FD_SET(STDIN_FILENO,&fdr);
		FD_SET(new_fd,&fdr);
		int res=select(new_fd+1,&fdr,NULL,NULL,NULL);
		if(res>0)
		{
			if(FD_ISSET(new_fd,&fdr))
			{
				bzero(buf,sizeof(buf));
				int res=recv(new_fd,buf,sizeof(buf),0);
				if(res>0)
				{
					printf("%s\n",buf);
				}else
				{
					printf("bybai\n");
					break;
				}
			}
			if(FD_ISSET(STDIN_FILENO,&fdr))//如果标准输入有内容则发送
			{
				bzero(buf,sizeof(buf));
				read(STDIN_FILENO,buf,sizeof(buf));
				send(new_fd,buf,strlen(buf)-1,0);//发送数据
			}
		}
	}
	close(sfd);
}
