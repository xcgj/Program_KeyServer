#include <stdio.h>
#include <string.h>
#include "keymngserverop.h"
#include "keymng_msg.h"
#include "poolsocket.h"
#include "keymnglog.h"
#include "keymng_shmop.h"
#include "icdbapi.h"
#include "keymng_dbop.h"

//int seckeyid = 100;

int MngServer_InitInfo(MngServer_Info *svrInfo)
{
    /*typedef struct _MngServer_Info
    {
    	char			serverId[12];	//服务器端编号

    	//数据库连接池句柄
    	char			dbuse[24]; 		//数据库用户名
    	char			dbpasswd[24]; 	//数据库密码
    	char			dbsid[24]; 		//数据库sid
    	int				dbpoolnum; 		//数据库池 连接数

    	char			serverip[24];
    	int 			serverport;

    	//共享内存配置信息
    	int				maxnode; //最大网点树 客户端默认1个
    	int 			shmkey;	 //共享内存keyid 创建共享内存时使用
    	int 			shmhdl; //共享内存句柄
    }MngServer_Info;*/
    //MngServer_Info serinfo = *svrInfo;
    int ret = 0;
    strcpy(svrInfo->serverId, "0001");
    strcpy(svrInfo->dbuse, "SECMNG");
    strcpy(svrInfo->dbpasswd, "SECMNG");
    strcpy(svrInfo->dbsid, "orcl");
    strcpy(svrInfo->serverip, "192.168.19.171");
    svrInfo->dbpoolnum = 10;
    svrInfo->serverport = 8001;
    svrInfo->maxnode = 10;
    svrInfo->shmkey = 0x0011;
    svrInfo->shmhdl = 0;

    //初始化共享内存
    ret = KeyMng_ShmInit(svrInfo->shmkey, svrInfo->maxnode, &(svrInfo->shmhdl));
    if (ret != 0)
    {
        KeyMng_Log(__FILE__, __LINE__, KeyMngLevel[4], ret, "KeyMng_ShmInit error:%d", ret);
	printf("KeyMng_ShmInit error:%d, %s, %s, %d\n", ret, __FILE__, __func__, __LINE__);
        return ret;
    }
    printf("服务器共享内存初始化成功\n");

    //初始化数据库
    ret = IC_DBApi_PoolInit(svrInfo->dbpoolnum, svrInfo->dbsid, svrInfo->dbuse, svrInfo->dbpasswd);
    if (ret != 0) {
        KeyMng_Log(__FILE__, __LINE__, KeyMngLevel[4], ret, "IC_DBApi_PoolInit error:%d", ret);
    	printf("IC_DBApi_PoolInit error:%d, %s, %s, %d\n", ret, __FILE__, __func__, __LINE__);
        return ret;
    }
    printf("数据库池初始化成功\n");

    return ret;
}

int MngServer_Agree(MngServer_Info *svrInfo, MsgKey_Req *msgkeyReq, unsigned char **outData, int *datalen)
{
    printf("======================密钥协商=====================\n");
    int ret = 0;
    MsgKey_Res resStruct;
    int seckeyid = 0;

    //从数据库连接池获取一个连接句柄
    ICDBHandle handle = NULL;
    int sTimeout = 0;
    int nsTimeout = 0;
    int validFlag = 1;
    printf("------------6-----------------\n");

    ret = IC_DBApi_ConnGet(&handle, sTimeout, nsTimeout);
    if (ret != 0)
    {
        KeyMng_Log(__FILE__, __LINE__, KeyMngLevel[4], ret, "IC_DBApi_ConnGet error:%d", ret);
        printf("IC_DBApi_ConnGet error:%d, %s, %s, %d\n", ret, __FILE__, __func__, __LINE__);
        if (ret == IC_DB_CONNECT_ERR)
            validFlag = 0;
        goto END;
    }
    printf("------------7-----------------\n");

    //开启事务
    ret = IC_DBApi_BeginTran(handle);
    if (ret != 0)
    {
        KeyMng_Log(__FILE__, __LINE__, KeyMngLevel[4], ret, "IC_DBApi_BeginTran error:%d", ret);
        printf("IC_DBApi_BeginTran error:%d, %s, %s, %d\n", ret, __FILE__, __func__, __LINE__);
        if (ret == IC_DB_CONNECT_ERR)
            validFlag = 0;
        goto END;
    }
    printf("------------8-----------------\n");

    //查询数据库提取seckeyid
    ret = KeyMngsvr_DBOp_GenKeyID(handle, &seckeyid);
    if (ret != 0)
    {
        KeyMng_Log(__FILE__, __LINE__, KeyMngLevel[4], ret, "KeyMngsvr_DBOp_GenKeyID error:%d", ret);
        printf("IC_DBApi_BKeyMngsvr_DBOp_GenKeyIDeginTran error:%d, %s, %s, %d\n", ret, __FILE__, __func__, __LINE__);
        if (ret == IC_DB_CONNECT_ERR)
            validFlag = 0;
        goto END;
    }
    printf("------------1-----------------\n");
    /*
    #define    ID_MsgKey_Res    61
    typedef struct    _MsgKey_Res
    {
    	int					rv;				//返回值
    	char				clientId[12];	//客户端编号
    	char				serverId[12];	//服务器编号
    	unsigned char		r2[64];			//服务器端随机数
    	int					seckeyid;		//对称密钥编号 //modfy 2015.07.20
    }MsgKey_Res;
    */
    //产生随机数
    int i = 0;
    for(i = 0; i < 10; ++i)
    {
        resStruct.r2[i] = 'z'-i;
    }

    //组建应答报文
    resStruct.rv = 0;
    resStruct.seckeyid = seckeyid;
    strcpy(resStruct.clientId, msgkeyReq->clientId);
    strcpy(resStruct.serverId, svrInfo->serverId);

    //编码报文
    ret = MsgEncode((void*)&resStruct, ID_MsgKey_Res, outData, datalen);
    if (ret != 0)
    {
        if (outData != NULL) {
            MsgMemFree((void**)outData, 0);
        }
        KeyMng_Log(__FILE__, __LINE__, KeyMngLevel[4], ret, "MsgEncode失败:%d", ret);
        printf("MsgEncode error:%d, %s, %s, %d\n", ret, __FILE__, __func__, __LINE__);
        goto END;
    }
    printf("------------2----------------\n");

    //密钥存入共享内存
    //组建密钥保存的结构体
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
    strcpy(pNodeInfo.clientId, msgkeyReq->clientId);
    strcpy(pNodeInfo.serverId, svrInfo->serverId);
    pNodeInfo.seckeyid = resStruct.seckeyid;
    //组建密钥
    for (i = 0; i < 64; i++)
    {
        pNodeInfo.seckey[i*2] = msgkeyReq->r1[i];
        pNodeInfo.seckey[i*2+1] = resStruct.r2[i];
    }

    //写入共享内存
    ret = KeyMng_ShmWrite(svrInfo->shmhdl, svrInfo->maxnode, &pNodeInfo/*in*/);
    if (ret != 0)
    {
        KeyMng_Log(__FILE__, __LINE__, KeyMngLevel[4], ret, "KeyMng_ShmWrite error:%d", ret);
        printf("KeyMng_ShmWrite error:%d, %s, %s, %d\n", ret, __FILE__, __func__, __LINE__);
        goto END;
    }

    printf("服务器密钥生成完毕， 共享内存存储完成\n");
    printf("%s\n", pNodeInfo.seckey);

    //密钥存入数据库
    //优化：先尝试update(密钥字符串匹配)再选择insert，保证客户端重复发送密钥协商只更新keyid
    //ret = KeyMngsvr_DBOp_WriteSecKey(handle, &pNodeInfo);
    ret = KeyMngsvr_UpInsr_DBOp_WriteSecKey(handle, &pNodeInfo);
    if (ret != 0)
    {
        KeyMng_Log(__FILE__, __LINE__, KeyMngLevel[4], ret, "KeyMngsvr_DBOp_WriteSecKey error:%d", ret);
        printf("KeyMngsvr_DBOp_WriteSecKey error:%d, %s, %s, %d\n", ret, __FILE__, __func__, __LINE__);
        IC_DBApi_Rollback(handle);
        if (ret == IC_DB_CONNECT_ERR)
            validFlag = 0;
        goto END;
    }

    //结束事务
    ret = IC_DBApi_Commit(handle);
    if (ret != 0)
    {
        KeyMng_Log(__FILE__, __LINE__, KeyMngLevel[4], ret, "IC_DBApi_Commit error:%d", ret);
        printf("IC_DBApi_Commit error:%d, %s, %s, %d\n", ret, __FILE__, __func__, __LINE__);
        IC_DBApi_Rollback(handle);
        if (ret == IC_DB_CONNECT_ERR)
            validFlag = 0;
    }

END:
    if (handle != NULL)
    {
        ret = IC_DBApi_ConnFree(handle, validFlag);
    }
    return ret;
}

int MngServer_Check(MngServer_Info *svrInfo, MsgKey_Req *msgkeyReq, unsigned char **outData, int *datalen)
{
    printf("======================密钥校验=====================\n");
    int ret = 0;
    NodeSHMInfo pNodeInfo;
    MsgKey_Res resStruct;

    //=========================读共享内存=============================
    ret = KeyMng_ShmRead(svrInfo->shmhdl, msgkeyReq->clientId, msgkeyReq->serverId, svrInfo->maxnode, &pNodeInfo /*out*/);
    if (ret != 0)
    {
        KeyMng_Log(__FILE__, __LINE__, KeyMngLevel[4], ret, "KeyMng_ShmRead error:%d", ret);
	printf("KeyMng_ShmRead error:%d, %s, %s, %d\n", ret, __FILE__, __func__, __LINE__);
        return ret;
    }
    // 提取密钥字段
    memcpy(resStruct.r2, pNodeInfo.seckey, 10*sizeof(unsigned char));
    printf("resStruct.r2 key:%s\n", resStruct.r2);
    printf("msgkeyReq->r1 key:%s\n", msgkeyReq->r1);
    // 组成密钥，与数据库中个密钥对比，返回对比结果存到rv
    ret = strcmp(resStruct.r2, msgkeyReq->r1);
    if (ret == 0)
    {
        resStruct.rv = 0; //成功
    }
    else
    {
        resStruct.rv = 1; //失败
    }

    resStruct.seckeyid = pNodeInfo.seckeyid;//获取id

    //=========================组建应答报文=============================
    /*
    #define    ID_MsgKey_Res    61
    typedef struct    _MsgKey_Res
    {
    	int					rv;				//返回值
    	char				clientId[12];	//客户端编号
    	char				serverId[12];	//服务器编号
    	unsigned char		r2[64];			//服务器端随机数
    	int					seckeyid;		//对称密钥编号 //modfy 2015.07.20
    }MsgKey_Res;
    */
    strcpy(resStruct.clientId, msgkeyReq->clientId);
    strcpy(resStruct.serverId, svrInfo->serverId);
    //strcpy(resStruct.r2, "1000");

    //============================编码应答报文==========================
    //===============================传出==============================
    ret = MsgEncode((void *)&resStruct, ID_MsgKey_Res, outData, datalen);
    if (ret != 0)
    {
        if (outData != NULL) {
            MsgMemFree((void **)outData, 0);
        }
        KeyMng_Log(__FILE__, __LINE__, KeyMngLevel[4], ret, "MsgEncode失败%d\n", ret);
        printf("MsgEncode error:%d, %s, %s, %d\n", ret, __FILE__, __func__, __LINE__);
        return ret;
    }

    printf("服务器编码校验处理完毕，校验结果：%d\n", resStruct.rv);

    return ret;
}

int MngServer_Revoke(MngServer_Info *svrInfo, MsgKey_Req *msgkeyReq, unsigned char **outData, int *datalen)
{
    printf("======================密钥注销=====================\n");
    int ret = 0;

    //读共享内存===============
    NodeSHMInfo pNodeInfo;
    ret = KeyMng_ShmRead(svrInfo->shmhdl, msgkeyReq->clientId, msgkeyReq->serverId, svrInfo->maxnode, &pNodeInfo /*out*/);
    if (ret != 0)
    {
        KeyMng_Log(__FILE__, __LINE__, KeyMngLevel[4], ret, "KeyMng_ShmRead error:%d", ret);
        printf("KeyMng_ShmRead error:%d, %s, %s, %d\n", ret, __FILE__, __func__, __LINE__);
        return ret;
    }
    pNodeInfo.status = 1;

    //写共享内存--修改写数据库==========
    //从数据库连接池获取一个连接句柄
    ICDBHandle handle = NULL;
    int sTimeout = 0;
    int nsTimeout = 0;
    int validFlag = 1;
    ret = IC_DBApi_ConnGet(&handle, sTimeout, nsTimeout);
    if (ret != 0)
    {
        KeyMng_Log(__FILE__, __LINE__, KeyMngLevel[4], ret, "IC_DBApi_ConnGet error:%d", ret);
        printf("IC_DBApi_ConnGet error:%d, %s, %s, %d\n", ret, __FILE__, __func__, __LINE__);
        if (ret == IC_DB_CONNECT_ERR)
            validFlag = 0;
        goto END;
    }

    //开启事务
    ret = IC_DBApi_BeginTran(handle);
    if (ret != 0)
    {
        KeyMng_Log(__FILE__, __LINE__, KeyMngLevel[4], ret, "IC_DBApi_BeginTran error:%d", ret);
        printf("IC_DBApi_BeginTran error:%d, %s, %s, %d\n", ret, __FILE__, __func__, __LINE__);
        if (ret == IC_DB_CONNECT_ERR)
            validFlag = 0;
        goto END;
    }

    //密钥存入数据库
    //优化：update
    //ret = KeyMngsvr_DBOp_WriteSecKey(handle, &pNodeInfo);
    ret = KeyMngsvr_Update_DBOp_WriteSecKey(handle, &pNodeInfo);
    if (ret != 0)
    {
        KeyMng_Log(__FILE__, __LINE__, KeyMngLevel[4], ret, "KeyMngsvr_DBOp_WriteSecKey error:%d", ret);
        printf("KeyMngsvr_DBOp_WriteSecKey error:%d, %s, %s, %d\n", ret, __FILE__, __func__, __LINE__);
        IC_DBApi_Rollback(handle);
        if (ret == IC_DB_CONNECT_ERR)
            validFlag = 0;
        goto END;
    }

    //结束事务
    ret = IC_DBApi_Commit(handle);
    if (ret != 0)
    {
        KeyMng_Log(__FILE__, __LINE__, KeyMngLevel[4], ret, "IC_DBApi_Commit error:%d", ret);
        printf("IC_DBApi_Commit error:%d, %s, %s, %d\n", ret, __FILE__, __func__, __LINE__);
        IC_DBApi_Rollback(handle);
        if (ret == IC_DB_CONNECT_ERR)
            validFlag = 0;
        goto END;
    }

    //内存可以不用写，反正要覆盖，密钥放入数据库就行
    ret = KeyMng_ShmWrite(svrInfo->shmhdl, svrInfo->maxnode, &pNodeInfo/*in*/);
    if (ret != 0)
    {
        KeyMng_Log(__FILE__, __LINE__, KeyMngLevel[4], ret, "KeyMng_ShmWrite error:%d", ret);
        printf("KeyMng_ShmWrite error:%d, %s, %s, %d\n", ret, __FILE__, __func__, __LINE__);
        goto END;
    }

    //组织应答报文======================
    MsgKey_Res resStruct;
    resStruct.rv = 0;
    resStruct.seckeyid = pNodeInfo.seckeyid;
    strcpy(resStruct.clientId, msgkeyReq->clientId);
    strcpy(resStruct.serverId, svrInfo->serverId);

    //编码=================================传出
    ret = MsgEncode((void*)&resStruct, ID_MsgKey_Res, outData, datalen);
    if (ret != 0)
    {
        if (outData != NULL) {
            MsgMemFree((void**)outData, 0);
        }
        KeyMng_Log(__FILE__, __LINE__, KeyMngLevel[4], ret, "MsgEncode失败:%d", ret);
        printf("MsgEncode error:%d, %s, %s, %d\n", ret, __FILE__, __func__, __LINE__);
        goto END;
    }

END:
    if (handle != NULL)
    {
        ret = IC_DBApi_ConnFree(handle, validFlag);
    }
    return ret;
}
