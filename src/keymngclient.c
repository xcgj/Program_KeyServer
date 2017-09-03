#include <unistd.h>
#include <sys/types.h>
#include <signal.h>
#include <sys/wait.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <pthread.h>
#include "keymngclientop.h"
#include "keymng_msg.h"
#include "poolsocket.h"
#include "keymnglog.h"
#include "icdbapi.h"
#include "keymng_dbop.h"


//登陆界面
int Usage()
{
    int nSel = -1;

    system("clear");
    printf("\n  /*************************************************************/");
    printf("\n  /*************************************************************/");
    printf("\n  /*     1.密钥协商                                            */");
    printf("\n  /*     2.密钥校验                                            */");
    printf("\n  /*     3.密钥注销                                            */");
    printf("\n  /*     4.密钥查看                                            */");
    printf("\n  /*     0.退出系统                                            */");
    printf("\n  /*************************************************************/");
    printf("\n  /*************************************************************/");
    printf("\n\n  选择:");
    scanf("%d", &nSel);
    while(getchar() != '\n'); //把应用程序io缓冲器的所有的数据 都读走,避免影响下一次 输入

    return nSel;
}

int main()
{
	MngClient_Info cinfo;
	memset(&cinfo, 0, sizeof(MngClient_Info));

	int ret = 0;

	//2 初始化基本信息
	ret = MngClient_InitInfo(&cinfo);
	if(ret != 0)
  {
		KeyMng_Log(__FILE__, __LINE__, KeyMngLevel[4], ret, "客户端初始化失败:%d\n", ret);
    printf("MngClient_InitInfo() error:%d, %s, %s, %d\n", ret, __FILE__, __func__, __LINE__);
		return ret;
	}
	printf("%s\n", "客户端初始化完成");

	int sel = -1;
	while(1)
	{
		//1 登陆界面
		sel = Usage();

		//3 switch选择功能
		switch(sel)
		{
		case 0:
			//退出系统
			ret = MngClient_Quit(&cinfo);
			break;
		case KeyMng_NEWorUPDATE:
			//密钥协商
			ret = MngClient_Agree(&cinfo);
      /*
      产生随机数
      组织请求报文
      编码报文
      发送报文（建立连接）

      接收应答报文
      解码报文
      取出结果
      写入共享内存
      */
			break;
		case KeyMng_Check:
			//密钥校验
			ret = MngClient_Check(&cinfo);
      /*
      读共享内存取出前10字节-->r1
      组织请求报文
      编码报文
      发送报文（建立连接）

      接收应答报文
      解码报文
      取出结果
      */
			break;
		case KeyMng_Revoke:
			//密钥注销
			ret = MngClient_Revoke(&cinfo);
      /*
      读共享内存
      组织请求报文--seckeyid密钥注销--存放到r1
      编码报文
      发送报文（建立连接）

      接收应答报文
      解码报文
      取出结果
      写共享内存，客户端status状态改变
      */
			break;
		case 4:
			//密钥查看
			ret = MngClient_view(&cinfo);
      /*
      读共享内存
      */
			break;
		default:
			break;
		}

		//4 显示结果
		if (ret)
		{
			printf("\n!!!!!!!!!!!!!!!!!!!!ERROR!!!!!!!!!!!!!!!!!!!!");
			printf("\n错误码是：%x\n", ret);
		}
		else
		{
			printf("\n!!!!!!!!!!!!!!!!!!!!SUCCESS!!!!!!!!!!!!!!!!!!!!\n");
		}
		getchar();
	}

	printf("keymngclient logout...\n");
	return 0;
}
