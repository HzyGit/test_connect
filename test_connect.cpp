#include <sys/socket.h>
#include <stdlib.h>
#include <stdio.h>
#include <error.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <netdb.h>
#include <string.h>
#include <getopt.h>


const char *g_host="127.0.0.1";
const char *g_serv="22";
struct timeval g_timeout={3,0};

/// @brief 以tv为超时值建立tcp链接
/// @param[in] socketfd tcp套接字
/// @param[in] addr,salen 链接地址
/// @param[in] tv超时值
/// @retval 成功返回0 错误返回错误代码负值
static int connect_nonb(int sockfd,const struct sockaddr *addr,socklen_t salen,
		struct timeval &tv) {
	/// 保存套接字状态
	int flags;
	flags=fcntl(sockfd,F_GETFL,0);
	if(flags<0)
		return -errno;
	/// 设置套接字非阻塞
	if(fcntl(sockfd,F_SETFL,flags|O_NONBLOCK)<0)
		return -errno;
	/// 建立连接
	int err=0;
	do{
		if(connect(sockfd,addr,salen)<0){
			if(EINPROGRESS!=errno){
				err=-errno;
				break;
			}
			/// select超时判断
			fd_set rset;
			FD_ZERO(&rset);
			FD_SET(sockfd,&rset);
			fd_set wset=rset;	
			int n=0;
			if((n=select(sockfd+1,&rset,&wset,NULL,&tv))==0){
				/// 超时
				err=-ETIMEDOUT;
				break;
			}
			if(FD_ISSET(sockfd,&rset)||FD_ISSET(sockfd,&wset)){
				int error;
				unsigned int len=sizeof(error);
				if(getsockopt(sockfd,SOL_SOCKET,SO_ERROR,&error,&len)<0){
					err=-errno;
					break;
				}
				/// 链接出错
				if(error){
					err=-error;
					break;
				}
			}
		}
	}while(false);
	fcntl(sockfd,F_SETFL,flags);
	if(err<0)
		close(sockfd);
	return err;
}

/// @brief 依据Host，Serv，解析sockaddr地址
/// @retval 成功返回相应套接字 错误返回错误代码负值
static int create_socket(struct sockaddr *addr,socklen_t *addrlen,
		const char *host,const char *serv){
	if(NULL==host||NULL==serv||NULL==addr||NULL==addrlen)
		return -EINVAL;
	struct addrinfo hints,*info;
	memset(&hints,0,sizeof(hints));
	hints.ai_flags=AI_NUMERICHOST|AI_NUMERICSERV;
	hints.ai_family=AF_UNSPEC;
	hints.ai_socktype=SOCK_STREAM;
	int err=0;
	err=getaddrinfo(host,serv,&hints,&info);
	if(err){
		error_at_line(0,0,__FILE__,__LINE__,"getaddrinfo error :%s",gai_strerror(err));
		return -EINVAL;
	}
	/// 复制地址
	if(info->ai_addrlen>*addrlen)
		return -EFAULT;
	*addrlen=info->ai_addrlen;
	memcpy(addr,info->ai_addr,info->ai_addrlen);
	/// 建立套接字
	int sockfd=socket(info->ai_family,info->ai_socktype,info->ai_protocol);
	freeaddrinfo(info);
	if(sockfd<0){
		error_at_line(0,errno,__FILE__,__LINE__,"sockeet error");
		return -errno;
	}
	return sockfd;
}


static void usage(int err){
	const char *name=program_invocation_short_name;
	if(err==EXIT_SUCCESS){
		printf("Usage: %s [options] host prot\n",name);
		printf("测试指定主机端口是否打开,主机默认为127.0.0.1,端口默认为22\n");
		printf("\n");
		printf("选项:\n");
		printf("  -s, --sec  测试时的超时值，单位为秒,默认值为3\n");
		printf("  -h, --help 显示改帮助信息\n");

	}else{
		printf("Please type \"%s --hep\" for more infomation\n",name);
	}
	exit(err);
}

/// @brief 解析命令行参数
static void parse_argument(int argc,char **argv){
	const char *name=program_invocation_short_name;
	struct option opts[]={
		{"help",no_argument,NULL,'h'},
		{"sec",required_argument,NULL,'s'},
		{NULL,0,NULL,0},
	};
	int ch;
	while((ch=getopt_long(argc,argv,":hs:",opts,NULL))!=-1){
		long sec;
		switch(ch){
			case 'h':
				usage(EXIT_SUCCESS);
			case 's':
				sec=strtol(optarg,NULL,10);
				if(sec<=0){
					fprintf(stderr,"%s: 超时值必须大于0\n",name);
					usage(EXIT_FAILURE);
				}
				g_timeout.tv_sec=sec;
				break;
			case '?':
				fprintf(stderr,"%s:未识别选项: \'%c\'\n",name,optopt);
				usage(EXIT_FAILURE);
			case ':':
				fprintf(stderr,"%s:选项\'%c\'缺少参数\n",name,optopt);
				usage(EXIT_FAILURE);
			default:
				fprintf(stderr,"%s:解析参数遇到未知错误\n",name);
				usage(EXIT_FAILURE);
		}
	}
	if(argc-optind==2){
		g_host=argv[optind];
		g_serv=argv[optind+1];
	}
}

int main(int argc,char **argv){
	parse_argument(argc,argv);
	/// 建立套接字
	struct sockaddr_storage addr;
	socklen_t addrlen=sizeof(addr);
	int sockfd=create_socket((struct sockaddr*)&addr,&addrlen,g_host,g_serv);
	if(sockfd<0)
		error_at_line(EXIT_FAILURE,-sockfd,__FILE__,__LINE__,"create socket error");
	/// 测试连接
	int err=connect_nonb(sockfd,(struct sockaddr*)&addr,addrlen,g_timeout);
	if(err<0){
		close(sockfd);
		error(EXIT_FAILURE,-err,"connect %s:%s error",g_host,g_serv);
	}
	printf("connect to %s:%s Ok\n",g_host,g_serv);
	close(sockfd);
	return 0;
}

