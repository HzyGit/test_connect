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

#include <list>
#include <algorithm>

const char *g_host="127.0.0.1";
const char *g_serv="22";
struct timeval g_timeout={3,0};

/// @brief 探测结构
struct connect_unit{
	int sockfd;   ///< 套接字
	struct sockaddr_storage addr;
	socklen_t addrlen;
	std::string host;
	std::string serv;
};

std::list<connect_unit* > g_connect_list;

/// @brief list<connect_unit*>探测结构的指针
static void clean_connect_list(std::list<connect_unit*> &ls){
	for(auto it=ls.begin();it!=ls.end();++it){
		close((*it)->sockfd);
		delete *it;
	}
	ls.clear();
}

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

/// @brief 依据AI_NUMERIC解析host,serv为connect_unit结构，并添加至g_connect_list
/// @retval 成功返回0，错误返回错误代码负值
static int append_connect_list_numeric(const char *host,const char *serv){
	if(NULL==host||serv==NULL)
		return -EINVAL;
	/// 解析host,serv
	struct addrinfo hints,*info;
	memset(&hints,0,sizeof(hints));
	hints.ai_flags=AI_NUMERICHOST|AI_NUMERICSERV;
	hints.ai_socktype=SOCK_STREAM;
	hints.ai_family=AF_UNSPEC;
	int err=0;
	if((err=getaddrinfo(host,serv,&hints,&info))!=0){
		error_at_line(0,0,__FILE__,__LINE__,"getaddrinfo error:%s",gai_strerror(err));
		return -EINVAL;
	}
	// 转换为connect_unit
	struct addrinfo *old=info;
	for(;info!=NULL;info=info->ai_next){
		int sockfd=socket(info->ai_family,info->ai_socktype,info->ai_protocol);
		if(sockfd<0){
			error_at_line(0,errno,__FILE__,__LINE__,"create socket error");
			continue;
		}
		struct connect_unit *unit=new connect_unit;
		if(NULL==unit)
			continue;
		unit->sockfd=sockfd;
		memcpy(&(unit->addr),info->ai_addr,info->ai_addrlen);
		unit->addrlen=info->ai_addrlen;
		unit->host=host;
		unit->serv=serv;
		g_connect_list.push_back(unit);
	}
	freeaddrinfo(old);
	return 0;
}

/// @brief 转换sockaddr为字符串表示
/// @retval 成功返回0 失败错误代码负值
static int sockaddr_to_string(const struct sockaddr *addr,socklen_t len,
		std::string &str_host,std::string &str_serv){
	if(NULL==addr||0==len)
		return -EINVAL;
	char host[NI_MAXHOST],serv[NI_MAXSERV];
	int err;
	if((err=getnameinfo(addr,len,host,NI_MAXHOST,serv,NI_MAXSERV,NI_NUMERICHOST|NI_NUMERICSERV))!=0){
		error_at_line(0,0,__FILE__,__LINE__,"getnameinfo error:",gai_strerror(err));
		return -EINVAL;
	}
	str_host=host;
	str_serv=serv;
	return 0;
}


/// @brief 判断字符串是否为数字
static bool is_numeric_str(const char *str){
	if(NULL==str)
		return false;
	for(int i=0;str[i]!='\0';++i){
		if(str[i]<'0'||str[i]>'9')
			return false;
	}
	return true;
}

/// @brief 依据域名解析host,serv，并添加至g_connect_list
/// @retval 0成功 <0 错误代码负值
static int append_connect_list_domain(const char *host,const char *serv){
	if(NULL==serv||NULL==host)
		return -EINVAL;
	/// 解析host,serv
	struct addrinfo hints,*ainfo;
	memset(&hints,0,sizeof(hints));
	if(is_numeric_str(serv))
		hints.ai_flags=AI_NUMERICSERV;
	hints.ai_socktype=SOCK_DGRAM;
	hints.ai_family=AF_UNSPEC;
	int err=0;
	if((err=getaddrinfo(host,serv,&hints,&ainfo))!=0){
		error_at_line(0,0,__FILE__,__LINE__,"getaddrinfo error :%s",gai_strerror(err));
		return -EINVAL;
	}
	/// 解析connect_unit
	struct addrinfo *old=ainfo;
	for(;ainfo!=NULL;ainfo=ainfo->ai_next){
		int sockfd=socket(ainfo->ai_family,ainfo->ai_socktype,ainfo->ai_protocol);
		if(sockfd<0){
			error_at_line(0,errno,__FILE__,__LINE__,"create socket error");
			continue;
		}
		struct connect_unit *unit=new connect_unit;
		unit->sockfd=sockfd;
		memcpy(&(unit->addr),ainfo->ai_addr,ainfo->ai_addrlen);
		unit->addrlen=ainfo->ai_addrlen;
		if(sockaddr_to_string(ainfo->ai_addr,ainfo->ai_addrlen,unit->host,unit->serv)<0){
			unit->host="unknown";
			unit->serv="uknown";
		}
		g_connect_list.push_back(unit);
	}
	freeaddrinfo(old);
	return 0;
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

/// @brief 设置g_connect_list
static void init_connect_list(){
	g_connect_list.clear();
	if(append_connect_list_numeric(g_host,g_serv)<0)
		append_connect_list_domain(g_host,g_serv);
}

/// @brief 测试单个连接
static void test_connect_unit(const connect_unit *unit){
	printf("connect %s:%s",unit->host.c_str(),unit->serv.c_str());
	int err=connect_nonb(unit->sockfd,(struct sockaddr*)&(unit->addr),unit->addrlen,g_timeout);
	if(err)
		printf("  %s\n",strerror(-err));
	else
		printf("  ok\n");
}

int main(int argc,char **argv){
	parse_argument(argc,argv);
	/// 建立connect_list
	init_connect_list();
	/// 测试连接
	std::for_each(g_connect_list.begin(),g_connect_list.end(),test_connect_unit);
	/// 释放connect_list
	clean_connect_list(g_connect_list);
	return 0;
}

