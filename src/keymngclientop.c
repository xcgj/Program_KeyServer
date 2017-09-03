
#include <stdio.h>
#include <string.h>
#include "keymngclientop.h"
#include "keymng_msg.h"
#include "poolsocket.h"
#include "keymnglog.h"
#include "keymng_shmop.h"
#include "icdbapi.h"
#include "keymng_dbop.h"

/*
typedef struct _MngClient_Info
{
	char			clientId[12];	//客户端编号
	char			AuthCode[16];	//认证码
	char			serverId[12];	//服务器端编号

	char			serverip[32];
	int 			serverport;

	int				maxnode; 		//最大网点数 客户端默认1个
	int 			shmkey;	 		//共享内存keyid 创建共享内存时使用
	int 			shmhdl; 		//共享内存句柄
}MngClient_Info;
*/
//基本信息
int MngClient_InitInfo(MngClient_Info *pCltInfo)
{
	int ret = 0;
	//MngClient_Info msg = *pCltInfo;
	strcpy(pCltInfo->clientId, "1111");
	strcpy(pCltInfo->AuthCode, "111");
	strcpy(pCltInfo->serverId, "0001");
	strcpy(pCltInfo->serverip, "127.0.0.1");
	pCltInfo->serverport = 8001;
	pCltInfo->maxnode = 10;
	pCltInfo->shmkey = 0x0012;
	pCltInfo->shmhdl = 0;

	//初始化共享内存
	//int KeyMng_ShmInit(int key, int maxnodenum, int *shmhdl);
	ret = KeyMng_ShmInit(pCltInfo->shmkey, pCltInfo->maxnode, &(pCltInfo->shmhdl));
	if (ret != 0)
	{
		KeyMng_Log(__FILE__, __LINE__, KeyMngLevel[4], ret, "KeyMng_ShmInit error:%d\n", ret);
		printf("KeyMng_ShmInit error:%d, %s, %s, %d\n", ret, __FILE__, __func__, __LINE__);
    return ret;
	}

	printf("客户端共享内存初始化成功\n");

	return ret;
}

//协商
int MngClient_Agree(MngClient_Info *pCltInfo)
{
	printf("======================密钥协商=====================\n");
	int ret = 0;
	//组织req报文--随机数
	/*
	typedef struct _MsgKey_Req
	{
		//1 密钥更新  	//2 密钥校验; 	//3 密钥注销
		int				cmdType;		//报文命令码
		char			clientId[12];	//客户端编号
		char			AuthCode[16];	//认证码
		char			serverId[12];	//服务器端I编号
		char			r1[64];		//客户端随机数
	}MsgKey_Req;
	*/
	MsgKey_Req sendStruct;
	sendStruct.cmdType = KeyMng_NEWorUPDATE;
	strcpy(sendStruct.clientId, pCltInfo->clientId);
	strcpy(sendStruct.AuthCode, pCltInfo->AuthCode);
	strcpy(sendStruct.serverId, pCltInfo->serverId);
	//随机数
	int i = 0;
	for (i = 0; i < 10; i++) {
		sendStruct.r1[i] = 'd' + i;
	}
	//编码
	unsigned char *reqData = NULL;
	int reqLen = 0;
	unsigned char *resData = NULL;
	int resLen = 0;
	MsgKey_Res * resStruct = NULL;
	int type = 0;

	ret = MsgEncode((void*)&sendStruct, ID_MsgKey_Req, &reqData, &reqLen);
	if (ret != 0) {
		KeyMng_Log(__FILE__, __LINE__, KeyMngLevel[4], ret, "MsgEncode error:%d\n", ret);
		printf("MsgEncode() error:%d, %s, %s, %d\n", ret, __FILE__, __func__, __LINE__);
		goto END;
	}

	//连接服务器
	ret = sckClient_init();
	if (ret != 0) {
		KeyMng_Log(__FILE__, __LINE__, KeyMngLevel[4], ret, "sckClient_init error:%d\n", ret);
		printf("sckClient_init() error:%d, %s, %s, %d\n", ret, __FILE__, __func__, __LINE__);
		goto END;
	}

	int timeout = 100;
	int connfd = -1;
	ret = sckClient_connect(pCltInfo->serverip, pCltInfo->serverport, timeout, &connfd);
	if (ret == Sck_ErrTimeOut) //连接超时
	{
		KeyMng_Log(__FILE__, __LINE__, KeyMngLevel[2], ret, "sckClient_connect Sck_ErrTimeOut\n");
		printf("Sck_ErrTimeOut");
		goto END;
	}
	else if (ret != 0)		//其他错误
  {
		KeyMng_Log(__FILE__, __LINE__, KeyMngLevel[4], ret, "sckClient_connect error:%d\n", ret);
		printf("sckClient_connect() error:%d, %s, %s, %d\n", ret, __FILE__, __func__, __LINE__);
		goto END;
	}

	//发送报文
	ret = sckClient_send(connfd, timeout, reqData, reqLen);
	if (ret == Sck_ErrTimeOut) //连接超时
	{
		KeyMng_Log(__FILE__, __LINE__, KeyMngLevel[2], ret, "sckClient_send Sck_ErrTimeOut\n");
		printf("Sck_ErrTimeOut");
		sckClient_closeconn(connfd);
		goto END;
	}
	else if (ret == Sck_ErrPeerClosed) //服务器关闭
	{
		KeyMng_Log(__FILE__, __LINE__, KeyMngLevel[2], ret, "sckClient_rev Sck_ErrPeerClosed\n");
		printf("sckClient_rev Sck_ErrPeerClosed %s, %s, %d\n", __FILE__, __func__, __LINE__);
		sckClient_closeconn(connfd);
		goto END;
	}
	else if (ret != 0)		//其他错误
  {
		KeyMng_Log(__FILE__, __LINE__, KeyMngLevel[4], ret, "sckClient_send error:%d\n", ret);
		printf("sckClient_send() error:%d, %s, %s, %d\n", ret, __FILE__, __func__, __LINE__);
		sckClient_closeconn(connfd);
		goto END;
	}

	//---------------------------------服务通信----------------------------------------

	//接收服务器res报文
	ret = sckClient_rev(connfd, timeout, &resData, &resLen);
	if (ret == Sck_ErrTimeOut) //连接超时
	{
		KeyMng_Log(__FILE__, __LINE__, KeyMngLevel[2], ret, "sckClient_rev Sck_ErrTimeOut\n");
		printf("Sck_ErrTimeOut\n");
		sckClient_closeconn(connfd);
		goto END;
	}
	else if (ret == Sck_ErrPeerClosed) //服务器关闭
	{
		KeyMng_Log(__FILE__, __LINE__, KeyMngLevel[2], ret, "sckClient_rev Sck_ErrPeerClosed\n");
		printf("sckClient_rev Sck_ErrPeerClosed %s, %s, %d\n", __FILE__, __func__, __LINE__);
		sckClient_closeconn(connfd);
		goto END;
	}
	else if (ret != 0)		//其他错误
  {
		KeyMng_Log(__FILE__, __LINE__, KeyMngLevel[4], ret, "sckClient_rev error:%d\n", ret);
		printf("sckClient_rev() error:%d, %s, %s, %d\n", ret, __FILE__, __func__, __LINE__);
		sckClient_closeconn(connfd);
		goto END;
	}

	//socket任务完成，客户端主动关闭
	sckClient_closeconn(connfd);

	//解码
	ret = MsgDecode(resData, resLen, (void **)&resStruct, &type);
	if (ret != 0)
	{
		KeyMng_Log(__FILE__, __LINE__, KeyMngLevel[4], ret, "MsgDecode error:%d\n", ret);
		printf("MsgDecode() error:%d, %s, %s, %d\n", ret, __FILE__, __func__, __LINE__);
		goto END;
	}

	//提取信息--判断结果
	if (resStruct->rv == 0)
	{
		printf("客户端服务器密钥协商成功。生成密钥编号为：%d\n", resStruct->seckeyid);
	}
	else
	{
		printf("客户端服务器密钥协商失败\n");
		return -1;
	}


	//9. 利用 r2 、 r1 生成密钥
	/*
  //将网点密钥信息写共享内存， 网点共享内存结构体
  typedef struct _NodeSHMInfo
  {
  	int 			status;			//密钥状态 0-有效 1无效
  	char			clientId[12];	//客户端id
  	char			serverId[12];	//服务器端id
  	int				seckeyid;		//对称密钥id
  	unsigned char	seckey[128];	//对称密钥 //hash1 hash256 md5
  }NodeSHMInfo;
  */
  NodeSHMInfo pNodeInfo;
  pNodeInfo.status = 0;
  strcpy(pNodeInfo.clientId, sendStruct.clientId);
  strcpy(pNodeInfo.serverId, resStruct->serverId);
  pNodeInfo.seckeyid = resStruct->seckeyid;
	//组建密钥
  for (i = 0; i < 64; i++)
  {
    pNodeInfo.seckey[i*2] = sendStruct.r1[i];
    pNodeInfo.seckey[i*2+1] = resStruct->r2[i];
  }

	//10. 写共享内存
	ret = KeyMng_ShmWrite(pCltInfo->shmhdl, pCltInfo->maxnode, &pNodeInfo/*in*/);
	if (ret != 0)
	{
		KeyMng_Log(__FILE__, __LINE__, KeyMngLevel[4], ret, "KeyMng_ShmWrite error:%d\n", ret);
		printf("KeyMng_ShmWrite() error:%d, %s, %s, %d\n", ret, __FILE__, __func__, __LINE__);
    return ret;
	}

	printf("密钥生成完毕，共享内存完成存储\n");
	//printf("%s\n", pNodeInfo.seckey);

END:
	if (reqData != NULL) {
		MsgMemFree((void**)&reqData, 0);	//参数2是不是0比较好
	}
	if (resData != NULL)
	{
		sck_FreeMem((void **)&resData);
	}
	if (resStruct != NULL) {
		MsgMemFree((void **)&resStruct, type);
	}
	return ret;
}

//校验
int MngClient_Check(MngClient_Info *pCltInfo)
{
	printf("======================密钥校验=====================\n");
	// 组建校验报文
	/*
	#define  ID_MsgKey_Req  60
	typedef struct _MsgKey_Req
	{
		//1 密钥更新  	//2 密钥校验; 	//3 密钥注销
		int				cmdType;		//报文命令码
		char			clientId[12];	//客户端编号
		char			AuthCode[16];	//认证码
		char			serverId[12];	//服务器端I编号
		char			r1[64];		//客户端随机数

	}MsgKey_Req;
	*/
	//======================读共享内存取出前10字节-->r1=======================
	//============================组织请求报文===============================
	int ret = 0;
	MsgKey_Req reqStruct;
	NodeSHMInfo pNodeInfo;
	reqStruct.cmdType = KeyMng_Check;
	strcpy(reqStruct.AuthCode, pCltInfo->AuthCode);
	strcpy(reqStruct.serverId, pCltInfo->serverId);
	strcpy(reqStruct.clientId, pCltInfo->clientId);

	//读
	ret = KeyMng_ShmRead(pCltInfo->shmhdl, pCltInfo->clientId, pCltInfo->serverId, pCltInfo->maxnode, &pNodeInfo /*out*/);
	if (ret != 0)
	{
		KeyMng_Log(__FILE__, __LINE__, KeyMngLevel[4], ret, "KeyMng_ShmRead error:%d\n", ret);
		printf("KeyMng_ShmRead error:%d, %s, %s, %d\n", ret, __FILE__, __func__, __LINE__);
		return ret;
	}
	//拷贝
	memcpy(reqStruct.r1, pNodeInfo.seckey, 10*sizeof(unsigned char));

	//===================================编码报文=================================
	unsigned char	*reqData = NULL;
	unsigned char *revData = NULL;
	MsgKey_Res * resStruct = NULL;

	int reqLen = 0;
	ret = MsgEncode((void *)&reqStruct, ID_MsgKey_Req, &reqData, &reqLen);
	if (ret != 0)
	{
		KeyMng_Log(__FILE__, __LINE__, KeyMngLevel[4], ret, "MsgEncode error:%d\n", ret);
		printf("MsgEncode error:%d, %s, %s, %d\n", ret, __FILE__, __func__, __LINE__);
		goto END;
	}

	//==================================连接服务器================================
	ret = sckClient_init();
	if (ret != 0)
	{
		KeyMng_Log(__FILE__, __LINE__, KeyMngLevel[4], ret, "sckClient_init error:%d\n", ret);
		printf("sckClient_init error:%d, %s, %s, %d\n", ret, __FILE__, __func__, __LINE__);
		goto END;
	}

	int connecttime = 5;
	int connfd = -1;
	ret = sckClient_connect(pCltInfo->serverip, pCltInfo->serverport, connecttime, &connfd);
	if (ret == Sck_ErrTimeOut) {
		KeyMng_Log(__FILE__, __LINE__, KeyMngLevel[2], ret, "sckClient_connect Sck_ErrTimeOut\n");
		printf("sckClient_connect Sck_ErrTimeOut\n");
		goto END;
	}
	else if (ret != 0)
	{
		KeyMng_Log(__FILE__, __LINE__, KeyMngLevel[4], ret, "sckClient_connect error:%d\n", ret);
		printf("sckClient_connect error:%d, %s, %s, %d\n", ret, __FILE__, __func__, __LINE__);
		goto END;
	}

	//=====================================发送报文=====================================
	int sendtime = 5;
	ret = sckClient_send(connfd, sendtime, reqData, reqLen);
	if (ret == Sck_ErrTimeOut)
	{
		KeyMng_Log(__FILE__, __LINE__, KeyMngLevel[2], ret, "sckClient_send Sck_ErrTimeOut\n");
		printf("sckClient_send Sck_ErrTimeOut\n");
		sckClient_closeconn(connfd);
		goto END;
	}
	else if (ret == Sck_ErrPeerClosed)
	{
		KeyMng_Log(__FILE__, __LINE__, KeyMngLevel[4], ret, "sckClient_send Sck_ErrPeerClosed:%d\n", ret);
		printf("sckClient_rev Sck_ErrPeerClosed %s, %s, %d\n", __FILE__, __func__, __LINE__);
		sckClient_closeconn(connfd);
		goto END;
	}
	else if (ret != 0)
	{
		KeyMng_Log(__FILE__, __LINE__, KeyMngLevel[4], ret, "sckClient_send error:%d\n", ret);
		printf("sckClient_send error:%d, %s, %s, %d\n", ret, __FILE__, __func__, __LINE__);
		sckClient_closeconn(connfd);
		goto END;
	}

	//=================================================//
	//=========================等待服务器回应，接收res报文==============================
	int revtime = 5;
	int revLen = 0;
	ret = sckClient_rev(connfd, revtime, &revData, &revLen);
	if (ret == Sck_ErrTimeOut)
	{
		KeyMng_Log(__FILE__, __LINE__, KeyMngLevel[2], ret, "sckClient_rev Sck_ErrTimeOut\n");
		printf("sckClient_rev Sck_ErrTimeOut\n");
		sckClient_closeconn(connfd);
		goto END;
	}
	else if (ret == Sck_ErrPeerClosed)
	{
		KeyMng_Log(__FILE__, __LINE__, KeyMngLevel[4], ret, "sckClient_rev Sck_ErrPeerClosed:%d\n", ret);
		printf("sckClient_rev Sck_ErrPeerClosed %s, %s, %d\n", __FILE__, __func__, __LINE__);
		sckClient_closeconn(connfd);
		goto END;
	}
	if (ret != 0)
	{
		KeyMng_Log(__FILE__, __LINE__, KeyMngLevel[4], ret, "sckClient_rev error:%d\n", ret);
		printf("sckClient_rev error:%d, %s, %s, %d\n", ret, __FILE__, __func__, __LINE__);
		sckClient_closeconn(connfd);
		goto END;
	}

	//socket任务完成，客户端主动关闭
	sckClient_closeconn(connfd);

	//==================================解码报文=====================================
	int type = 0;
	ret = MsgDecode(revData, revLen, (void**)&resStruct, &type);
	if (ret != 0)
	{
		KeyMng_Log(__FILE__, __LINE__, KeyMngLevel[4], ret, "MsgDecode error:%d\n", ret);
		printf("MsgDecode error:%d, %s, %s, %d\n", ret, __FILE__, __func__, __LINE__);
		sckClient_closeconn(connfd);
		goto END;
	}

	/*
	#define  ID_MsgKey_Res  61
	typedef struct  _MsgKey_Res
	{
		int					rv;				//返回值
		char				clientId[12];	//客户端编号
		char				serverId[12];	//服务器编号
		unsigned char		r2[64];			//服务器端随机数
		int					seckeyid;		//对称密钥编号 //modfy 2015.07.20
	}MsgKey_Res;
	*/
	//====================================处理回应消息==============================
	if (resStruct->rv == 0)
	{
		printf("密钥校验成功。密钥编号为：%d\n", resStruct->seckeyid);
		printf("客户端%s\n", reqStruct.r1);
		printf("服务器%s\n", resStruct->r2);
	}
	else
	{
		printf("密钥校验失败\n");
		ret = -1;
	}


END:
	if (reqData != NULL)
	{
		MsgMemFree((void **)&reqData, 0);
	}
	if (revData != NULL)
	{
		sck_FreeMem((void **)&revData);
	}
	if (resStruct != NULL)
	{
		MsgMemFree((void **)&resStruct, type);
	}

	return ret;
}

//注销
int MngClient_Revoke(MngClient_Info *pCltInfo)
{
	printf("======================密钥注销=====================\n");
	int ret = 0;
	//读共享内存=================
	NodeSHMInfo pNodeInfo;
	ret = KeyMng_ShmRead(pCltInfo->shmhdl, pCltInfo->clientId, pCltInfo->serverId, pCltInfo->maxnode, &pNodeInfo /*out*/);
	if (ret != 0)
	{
		KeyMng_Log(__FILE__, __LINE__, KeyMngLevel[4], ret, "KeyMng_ShmRead error:%d\n", ret);
		printf("KeyMng_ShmRead error:%d, %s, %s, %d\n", ret, __FILE__, __func__, __LINE__);
    return ret;
	}

	//组织请求报文--seckeyid密钥注销--存放到r1==============
	MsgKey_Req reqStruct;
	memset(&reqStruct, 0, sizeof(MsgKey_Req));
	reqStruct.cmdType = KeyMng_Revoke;
	strcpy(reqStruct.AuthCode, pCltInfo->AuthCode);
	strcpy(reqStruct.serverId, pCltInfo->serverId);
	strcpy(reqStruct.clientId, pCltInfo->clientId);
	//sprintf(reqStruct.r1, "%d", pNodeInfo.seckeyid);可以不需要，用客户端和服务器id找密钥

	//编码报文=======================
	unsigned char *reqData = NULL;
	int reqLen = 0;
	unsigned char *resData = NULL;
	int resLen = 0;
	MsgKey_Res * resStruct = NULL;
	int type = 0;

	ret = MsgEncode(&reqStruct, ID_MsgKey_Req, &reqData, &reqLen);
	if (ret != 0)
	{
		KeyMng_Log(__FILE__, __LINE__, KeyMngLevel[4], ret, "MsgEncode error:%d\n", ret);
		printf("MsgEncode() error:%d, %s, %s, %d\n", ret, __FILE__, __func__, __LINE__);
		goto END;
	}

	//发送报文（建立连接）==================
	//初始化
	ret = sckClient_init();
	if (ret != 0)
	{
		KeyMng_Log(__FILE__, __LINE__, KeyMngLevel[4], ret, "sckClient_init error:%d\n", ret);
		printf("sckClient_init() error:%d, %s, %s, %d\n", ret, __FILE__, __func__, __LINE__);
		goto END;
	}

	//连接
	int timeout = 5;
	int connfd = -1;
	ret = sckClient_connect(pCltInfo->serverip, pCltInfo->serverport, timeout, &connfd);
	if (ret == Sck_ErrTimeOut)
	{
		KeyMng_Log(__FILE__, __LINE__, KeyMngLevel[2], ret, "sckClient_connect Sck_ErrTimeOut\n");
		printf("Sck_ErrTimeOut");
		goto END;
	}
	else if (ret != 0)
	{
		KeyMng_Log(__FILE__, __LINE__, KeyMngLevel[4], ret, "sckClient_connect error:%d\n", ret);
		printf("sckClient_connect() error:%d, %s, %s, %d\n", ret, __FILE__, __func__, __LINE__);
		goto END;
	}

	//发送
	ret = sckClient_send(connfd, timeout, reqData, reqLen);
	if (ret == Sck_ErrTimeOut)
	{
		KeyMng_Log(__FILE__, __LINE__, KeyMngLevel[2], ret, "sckClient_send Sck_ErrTimeOut\n");
		printf("Sck_ErrTimeOut");
		sckClient_closeconn(connfd);
		goto END;
	}
	else if (ret == Sck_ErrPeerClosed)
	{
		KeyMng_Log(__FILE__, __LINE__, KeyMngLevel[2], ret, "sckClient_rev Sck_ErrPeerClosed\n");
		printf("sckClient_rev Sck_ErrPeerClosed %s, %s, %d\n", __FILE__, __func__, __LINE__);
		sckClient_closeconn(connfd);
		goto END;
	}
	else if (ret != 0)
	{
		KeyMng_Log(__FILE__, __LINE__, KeyMngLevel[4], ret, "sckClient_send error:%d\n", ret);
		printf("sckClient_send() error:%d, %s, %s, %d\n", ret, __FILE__, __func__, __LINE__);
		sckClient_closeconn(connfd);
		goto END;
	}

//==================================================================================

	//接收应答报文===================
	ret = sckClient_rev(connfd, timeout, &resData, &resLen); //1
	if (ret == Sck_ErrTimeOut)
	{
		KeyMng_Log(__FILE__, __LINE__, KeyMngLevel[2], ret, "sckClient_rev Sck_ErrTimeOut\n");
		printf("Sck_ErrTimeOut\n");
		sckClient_closeconn(connfd);
		goto END;
	}
	else if (ret == Sck_ErrPeerClosed)
	{
		KeyMng_Log(__FILE__, __LINE__, KeyMngLevel[2], ret, "sckClient_rev Sck_ErrPeerClosed\n");
		printf("sckClient_rev Sck_ErrPeerClosed %s, %s, %d\n", __FILE__, __func__, __LINE__);
		sckClient_closeconn(connfd);
		goto END;
	}
	else if (ret != 0)
	{
		KeyMng_Log(__FILE__, __LINE__, KeyMngLevel[4], ret, "sckClient_rev error:%d\n", ret);
		printf("sckClient_rev() error:%d, %s, %s, %d\n", ret, __FILE__, __func__, __LINE__);
		sckClient_closeconn(connfd);
		goto END;
	}

	//socket任务完成，客户端主动关闭
	sckClient_closeconn(connfd);

	//解码报文=========================
	ret = MsgDecode(resData, resLen, (void **)&resStruct, &type);
	if (ret != 0)
	{
		KeyMng_Log(__FILE__, __LINE__, KeyMngLevel[4], ret, "MsgDecode error:%d\n", ret);
		printf("MsgDecode() error:%d, %s, %s, %d\n", ret, __FILE__, __func__, __LINE__);
		goto END;
	}

	//取出结果=========================
	if (resStruct->rv == 0)
	{
		printf("客户端-->服务器密钥注销成功。注销编号为：%d\n", resStruct->seckeyid);
	}
	else
	{
		printf("客户端-->服务器密钥注销失败\n");
		return -1;
	}

	//写共享内存，客户端status状态改变============
  pNodeInfo.status = 1;
	ret = KeyMng_ShmWrite(pCltInfo->shmhdl, pCltInfo->maxnode, &pNodeInfo/*in*/);
	if (ret != 0)
	{
		KeyMng_Log(__FILE__, __LINE__, KeyMngLevel[4], ret, "KeyMng_ShmWrite error:%d\n", ret);
		printf("KeyMng_ShmWrite() error:%d, %s, %s, %d\n", ret, __FILE__, __func__, __LINE__);
    return ret;
	}

	printf("客户端密钥注销完毕，共享内存信息更改\n");

END:
	if (reqData != NULL) {
		MsgMemFree((void**)&reqData, 0);	//参数2是不是0比较好
	}
	if (resData != NULL)
	{
		sck_FreeMem((void **)&resData);
	}
	if (resStruct != NULL) {
		MsgMemFree((void **)&resStruct, type);
	}
	return ret;
}

//查询
int MngClient_view(MngClient_Info *pCltInfo)
{
	/*
	读共享内存
	*/
	int ret = 0;
	NodeSHMInfo pNodeInfo;
	ret = KeyMng_ShmRead(pCltInfo->shmhdl, pCltInfo->clientId, pCltInfo->serverId, pCltInfo->maxnode, &pNodeInfo /*out*/);
	if (ret != 0)
	{
		KeyMng_Log(__FILE__, __LINE__, KeyMngLevel[4], ret, "KeyMng_ShmRead error:%d\n", ret);
		printf("KeyMng_ShmRead error:%d, %s, %s, %d\n", ret, __FILE__, __func__, __LINE__);
    return ret;
	}

	printf("密钥：%s\n密钥编号：%d\n密钥状态：%d\n", pNodeInfo.seckey, pNodeInfo.seckeyid, pNodeInfo.status);
	return ret;
}

//退出
int MngClient_Quit(MngClient_Info *pCltInfo)
{
	printf("keymngclient logout...客户端退出成功\n");
	exit(1);
}
