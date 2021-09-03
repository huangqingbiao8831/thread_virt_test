#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <netdb.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/un.h>
#include <signal.h>
#include <sys/prctl.h>



#include <pthread.h>

#define MAX_TREAD_NUM      50
#define MAX_RUN_LOOP_NUM  100000
#define RECV_PORT         40303

int g_recv_fd;
pthread_mutex_t send_lock;

typedef struct 
{
	int used;
	int next;
	pthread_t threadId;
}threadQ;

typedef struct{
	int threadq_tail;
	int threadq_head;
	threadQ *paddr;
	pthread_mutex_t lock;
}ts_thread_addr;

ts_thread_addr thread_pcontext;

void thread_q_init(void)
{
    int i;
    thread_pcontext.paddr = (threadQ*)malloc(sizeof(threadQ)*(MAX_TREAD_NUM+1));
    memset((char*)thread_pcontext.paddr,0,sizeof(threadQ)*(MAX_TREAD_NUM+1));
    thread_pcontext.threadq_head = 1;
    thread_pcontext.threadq_tail = MAX_TREAD_NUM;
    //init queue
    for (i = 1; i < MAX_TREAD_NUM;i++ )
    {
        thread_pcontext.paddr[i].used = 0;
	thread_pcontext.paddr[i].next = i+1;
	thread_pcontext.paddr[i].threadId = 0;
    }
    thread_pcontext.paddr[i].next = 0;
    //init lock
    if(pthread_mutex_init(&thread_pcontext.lock, NULL) != 0) {
	printf("Init lock failed\n");
	exit(0);
    }
    return ;

}

int thread_q_get(void)
{
	int i;

	pthread_mutex_lock(&thread_pcontext.lock);
	if (thread_pcontext.threadq_head)
	{
		i = thread_pcontext.threadq_head;
		thread_pcontext.threadq_head = thread_pcontext.paddr[i].next;
		if (thread_pcontext.threadq_head == 0)
		{
		   thread_pcontext.threadq_tail = 0;
		}
		/* seized flag */
		thread_pcontext.paddr[i].used = 1;
		pthread_mutex_unlock(&thread_pcontext.lock);
                //printf("get index:%d\n",i);
		return i; //get port
	}
	pthread_mutex_unlock(&thread_pcontext.lock);
        //printf("get error\n");
	return 0;
}

void thread_q_free(int idx)
{
	pthread_mutex_lock(&thread_pcontext.lock); //lock data...
	if ((idx > 0) && (idx < (MAX_TREAD_NUM+1)))
	{
          if (pthread_join(thread_pcontext.paddr[idx].threadId, NULL))
          {
                   //printf("thread exit:id%d\n",thread_pcontext.paddr[idx].threadId);
                   
        } 
        usleep(200); 
		//memset((char *)&thread_pcontext.paddr[idx], 0, sizeof(threadQ));
		thread_pcontext.paddr[idx].used = 0;
		thread_pcontext.paddr[idx].threadId = 0;
		if (thread_pcontext.threadq_head == 0)
		{
			thread_pcontext.threadq_head = idx;
                       thread_pcontext.threadq_tail = idx;
		}
		else
		{
			thread_pcontext.paddr[thread_pcontext.threadq_tail].next = idx;
			thread_pcontext.threadq_tail = idx;
		}
               thread_pcontext.paddr[idx].next = 0;
	}
	pthread_mutex_unlock(&thread_pcontext.lock); //unlock data...
}

int sipsock_read(int sockfd, void *buf, size_t bufsize, struct sockaddr_in *from,struct sockaddr_in *dest) 
{
	int count;
	socklen_t fromlen;

	fromlen = sizeof(struct sockaddr_in);

	//tcp socket 是忽略from和len参数的，这里读出不from信息
	count = recvfrom(sockfd, buf, bufsize, 0, (struct sockaddr *)from, &fromlen);

	if (count<0) {
		printf("sipsock: recvfrom() returned error [%s]\n",strerror(errno));
	}

	return count;
}


void init_udp_recv()
{
	/* ??UDP??? */
	struct sockaddr_in udp_client_addr;
	bzero(&udp_client_addr,sizeof(udp_client_addr));
	udp_client_addr.sin_family = AF_INET;
	//udp_client_addr.sin_addr.s_addr = (INADDR_ANY);
	udp_client_addr.sin_addr.s_addr = inet_addr("127.0.0.1");
	udp_client_addr.sin_port = htons(RECV_PORT);
    
	//初始化send_lock
	if(pthread_mutex_init(&send_lock, NULL) != 0) {
	printf("Init lock failed\n");
	exit(0);
    }

	g_recv_fd = socket(AF_INET, SOCK_DGRAM, 0);
	if (g_recv_fd == -1)
	{
		return 0;
	}
	if(-1 == (bind(g_recv_fd,(struct sockaddr*)&udp_client_addr,sizeof(udp_client_addr))))
	{
		return 0;
	}
	return 1;
}

int send_index2father(char *pmsg,int msgLen)
{
	 /* 设置address */  
 struct sockaddr_in addr_serv; 
 int send_num = 0;
 int len;  
 memset(&addr_serv, 0, sizeof(addr_serv));  
 addr_serv.sin_family = AF_INET;  
 addr_serv.sin_addr.s_addr = inet_addr("127.0.0.1");  
 addr_serv.sin_port = htons(RECV_PORT);  
 len = sizeof(addr_serv);  
 //lac_log(LOCATION, xsutil::MINOR, "ERROR: sendUDPmsg to server,port 20303,msg:%s,msgLen:%d\n",pmsg,msgLen);
 pthread_mutex_lock(&send_lock);
 if (g_recv_fd > 0 )
 {
   send_num = sendto(g_recv_fd, pmsg, msgLen, 0, (struct sockaddr *)&addr_serv, len);
   if ( send_num > 0)
   {
       pthread_mutex_unlock(&send_lock);
       printf("send msg,len=%d\n",send_num);
      return 0;
   }
 }
 printf("send msg error \n");
 pthread_mutex_unlock(&send_lock);
 return -1;
}

void *virt_thread_run(void *ptr)
{
   int index = *((int*)ptr);
   char pthread_name[64] = {0};
   int i;
   void *pmalloc;
    
    sprintf(pthread_name,"virt_%d",index);
    prctl(PR_SET_NAME,pthread_name);
    for (i = 0; i<MAX_RUN_LOOP_NUM ;i++ )
    {
	pmalloc = malloc(sizeof(1024));//分配1024个字节
	free(pmalloc); //分配的字节释放掉
    }
   usleep(5000000);
   //printf("free index:%d\n",index);
   //thread_q_free(index);//释放进程id结构到队列中
   printf("send msg to father:%d\n",index);
   send_index2father((char*)&index,sizeof(int));
   pthread_exit(NULL);
}

void virt_thread_recv(void*ptr)
{
   	size_t buflen=0;
	fd_set fdset;
	struct sockaddr_in from_addr;
	struct sockaddr_in to_addr;
	int dw;
	int size;
	struct timeval timeout;
	char buff[32];
	int recv_index;

    prctl(PR_SET_NAME,"recv_thread");

    while(1)
	{
          FD_ZERO(&fdset);
	  FD_SET (g_recv_fd, &fdset);
	  timeout.tv_sec=2;  // 10 seconds timerout
	  timeout.tv_usec=0;
          size = g_recv_fd + 1;
	  dw = select(size,&fdset,NULL,NULL,&timeout);
	  if(dw<0)
	  {
		  printf("recv select return error:[%d]%s", errno,strerror(errno)); 
		  continue; 
	   }
	   else if (dw == 0)//timer out
	   {
             //printf("timer out.......\n");
             //usleep(10);
             continue;
	   }
	   else
	   {
              printf("will recv msg.....\n");
	      if(FD_ISSET(g_recv_fd,&fdset))
	      {
		 memset((void*)&(from_addr),0,sizeof(struct sockaddr_in));
		 buflen = sipsock_read(g_recv_fd, buff, sizeof(buff)-1, &from_addr,&to_addr);
		 if(buflen ==sizeof(int))
		 {
		    buff[buflen]='\0';
                    printf("buf[0]=%02x,buf[1]=%02x,buf[2]=%02x,buf[3]=%02x\n",buff[0],buff[1],buff[2],buff[3]);
		    //recv_index = (buff[3]<<24)&0xff000000 + (buff[2]<<16)&0x00ff0000 + (buff[1]<< 8)&0x0000ff00 + buff[0];
		    recv_index = buff[0];
		    printf("recv index:%d\n",recv_index);
                    if (recv_index) thread_q_free(recv_index);
		}
		else
		{
		    printf("udp sock received wrong msg len:%lu",buflen);
		}
	     }
	}// end of else
  }
}

int main(int argc,char *argv[])
{	
	int index;
	threadQ *pq = NULL;
        pthread_t  thread_recv_id;


	thread_q_init();//初始化
	init_udp_recv();

	//初始化接收线程
	if (-1 == pthread_create(&thread_recv_id,NULL,virt_thread_recv,NULL))
	{
        printf("create virt_thread error\n");
	    exit(0);
	}

	while(1)
	{
      index = thread_q_get();
	  if (index)
	  {
             pq = (threadQ*)&thread_pcontext.paddr[index];
	     if (-1 == pthread_create(&pq->threadId,NULL,virt_thread_run,(void*)&index))
	     {
                printf("create thread error\n");
	     	exit(0);
	     }
            //thread_q_free(index);
            printf("create thread,id=%d\n",index);   

	  }
          usleep(5000);
	}
	return 1;
}
