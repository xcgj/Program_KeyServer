#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <pthread.h>
#include <signal.h>
#include <unistd.h> //fork setsid

#include "keymngserverop.h"
#include "keymng_msg.h"
#include "poolsocket.h"
#include "keymnglog.h"
#include "icdbapi.h"
#include "keymng_dbop.h"

//守护进程宏函数
#define CREATE_DEAMON if(fork()>0)exit(1);setsid();

MngServer_Info serinfo;
int ExitFlag = 0;

//================================子线程回调函数==================================
void * callback(void *arg)
{
    //int connfd = *(int*)arg;
    int connfd = (int)arg;
    int ret = 0;
    int timeout = 5;
    unsigned char *revData = NULL;
    int revDataLen = 0;

    printf("===================子线程创建成功===================\n");
    MsgKey_Req * reqStruct = NULL;
    int type = 0;

    unsigned char *outData = NULL;
    int datalen = 0;


    //接收报文
    ret = sckServer_rev(connfd, timeout, &revData, &revDataLen);
    if (ret == Sck_ErrTimeOut)
    {
        KeyMng_Log(__FILE__, __LINE__, KeyMngLevel[2], ret, "sckServer_rev Sck_ErrTimeOut");
        printf("func sckServer_rev() r_accept() sckServer_rev Sck_ErrTimeOut\n");
        goto END;
    }
    else if (ret == Sck_ErrPeerClosed)
    {
        KeyMng_Log(__FILE__, __LINE__, KeyMngLevel[2], ret, "sckServer_rev Sck_ErrPeerClosed");
        printf("func sckServer_rev() Sck_ErrPeerClosed\n");
        goto END;
    }
    else if (ret != 0)
    {
        KeyMng_Log(__FILE__, __LINE__, KeyMngLevel[4], ret, "sckServer_rev失败");
        printf("sckServer_rev() error:%d, %s, %s, %d\n", ret, __FILE__, __func__, __LINE__);
        goto END;
    }

    //解码报文
    ret = MsgDecode(revData, revDataLen, (void **)&reqStruct, &type);
    if (ret != 0) {
        KeyMng_Log(__FILE__, __LINE__, KeyMngLevel[4], ret, "MsgDecode失败:%d\n", ret);
        printf("MsgDecode() error:%d, %s, %s, %d\n", ret, __FILE__, __func__, __LINE__);
        goto END;
    }

    //读取报文信息
    switch(reqStruct->cmdType)
    {
    case KeyMng_NEWorUPDATE://协商
        ret = MngServer_Agree(&serinfo, reqStruct, &outData, &datalen);
        /*
        产生随机数
        组建密钥
        组织应答报文
        编码
        写共享内存
        传出
        */
        break;
    case KeyMng_Check://校验
        ret = MngServer_Check(&serinfo, reqStruct, &outData, &datalen);
        /*
        读共享内存
        组织应答报文
        编码
        传出
        */
        break;
    case KeyMng_Revoke://注销
        ret = MngServer_Revoke(&serinfo, reqStruct, &outData, &datalen);
        /*
        读共享内存
        写共享内存--写数据库
        组织应答报文
        编码
        传出
        */
        break;
    }
    if (ret != 0)
    {
        KeyMng_Log(__FILE__, __LINE__, KeyMngLevel[4], ret, "switch处理失败");
        printf("switch处理 error:%d, %s, %s, %d\n", ret, __FILE__, __func__, __LINE__);
        goto END;
    }

    //发送应答报文
    ret = sckServer_send(connfd, timeout, outData, datalen);
    if (ret == Sck_ErrTimeOut)
    {
        KeyMng_Log(__FILE__, __LINE__, KeyMngLevel[2], ret, "Sck_ErrTimeOut");
        printf("func sckServer_send() Sck_ErrTimeOut\n");
        goto END;
    }
    else if (ret == Sck_ErrPeerClosed)
    {
        KeyMng_Log(__FILE__, __LINE__, KeyMngLevel[2], ret, "sckServer_send Sck_ErrPeerClosed");
        printf("func sckServer_send() Sck_ErrPeerClosed\n");
        goto END;
    }
    else if (ret != 0)
    {
        KeyMng_Log(__FILE__, __LINE__, KeyMngLevel[4], ret, "sckServer_send失败:%d\n", ret);
        printf("sckServer_send() error:%d, %s, %s, %d\n", ret, __FILE__, __func__, __LINE__);
        goto END;
    }

END:
    if (revData != NULL) {
        sck_FreeMem((void **)&revData);
    }
    if (reqStruct != NULL) {
         MsgMemFree((void **)&reqStruct, type);
    }
    if (outData != NULL) {
        MsgMemFree((void **)&outData, 0);
    }

    //关闭通信接口
    sckServer_close(connfd);

    printf("======数据处理完毕，socket关闭成功，当前线程退出======\n");

    return NULL;
}


//==============================信号处理函数==============================
void sighandle(int signum, siginfo_t * info, void * p)
{
    printf("=====================收到退出信号====================\n");
    ExitFlag = 1;
    return;
}

//================================主函数=================================//
int main()
{
    //守护进程
    CREATE_DEAMON

    //12号函数注册，杀死服务器进程
    int ret = 0;
    struct sigaction act;
    struct sigaction oldact;
    /*struct sigaction
    {
        void      (*sa_handler)(int);
        void      (*sa_sigaction)(int, siginfo_t *, void *);
        sigset_t     sa_mask;
        int       sa_flags;
        void      (*sa_restorer)(void);
    };*/
    act.sa_handler = NULL;
    act.sa_sigaction = sighandle;
    sigemptyset(&act.sa_mask);
    //act.sa_flags = SA_RESTART;//让阻塞的系统调用被这个信号打断之后，能重启
    act.sa_flags = SA_SIGINFO;//带参数的捕捉函数的flag宏
    act.sa_restorer = NULL;

    ret = sigaction(SIGUSR2/*12号信号*/, &act/*新的信号处理*/,&oldact/*保存初始处理状态*/);
    if (ret != 0)
    {
        KeyMng_Log(__FILE__, __LINE__, KeyMngLevel[4], ret, "sigaction失败:%d\n", ret);
        printf("sigaction() error:%d, %s, %s, %d\n", ret, __FILE__, __func__, __LINE__);
		return ret;
    }

    pthread_t tid;
    //初始化服务器信息
    ret = MngServer_InitInfo(&serinfo);
    if (ret != 0)
    {
        KeyMng_Log(__FILE__, __LINE__, KeyMngLevel[4], ret, "服务器信息初始化失败:%d\n", ret);
        printf("MngServer_InitInfo() error:%d, %s, %s, %d\n", ret, __FILE__, __func__, __LINE__);
		return ret;
    }

    //初始化服务器通信接口
    int listenfd = 0;
    ret = sckServer_init(serinfo.serverport, &listenfd);
    if (ret != 0)
    {
        KeyMng_Log(__FILE__, __LINE__, KeyMngLevel[4], ret, "服务器通信接口初始化失败:%d\n", ret);
        printf("sckServer_init() error:%d, %s, %s, %d\n", ret, __FILE__, __func__, __LINE__);
		return ret;
    }

    int timeout = 5;
    int connfd;
    while(1)
    {
        if (ExitFlag)
        {
            break;
        }
        //监听，等待客户端连接
        ret = sckServer_accept(listenfd, timeout, &connfd);
        if (ret == Sck_ErrTimeOut)
        {
            KeyMng_Log(__FILE__, __LINE__, KeyMngLevel[2], ret, "Sck_ErrTimeOut");
    		printf("func sckServer_accept() Sck_ErrTimeOut\n");
    		continue;
        }
        else if (ret != 0)
        {
            KeyMng_Log(__FILE__, __LINE__, KeyMngLevel[4], ret, "sckServer_accept失败:%d\n", ret);
            printf("sckServer_accept() error:%d, %s, %s, %d\n", ret, __FILE__, __func__, __LINE__);
    		break;
        }

        //产生子线程处理
        ret = pthread_create(&tid, NULL, callback, (void*)connfd);
        if (ret != 0)
        {
            KeyMng_Log(__FILE__, __LINE__, KeyMngLevel[4], ret, "pthread_create失败:%d\n", ret);
            printf("pthread_create() error:%d, %s, %s, %d\n", ret, __FILE__, __func__, __LINE__);
            return ret;
        }
        pthread_detach(tid);
    }

    //共享内存清理
    //TODO
    //连接池清理
    IC_DBApi_PoolFree();
    //服务器清理
    sckServer_destroy();

    printf("=================服务器内存清理完毕=================\n");

    return 0;
}
