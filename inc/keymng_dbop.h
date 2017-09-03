
// keymng_dbop.h
#ifndef _KEYMNG_DBOP_H_
#define _KEYMNG_DBOP_H_

#include "keymngserverop.h"
#include "keymng_shmop.h"

#ifdef __cplusplus
extern "C" {
#endif

int KeyMngsvr_DBOp_GenKeyID(void *dbhdl, int *keyid);

//直接插入，不更新
int KeyMngsvr_DBOp_WriteSecKey(void *dbhdl, NodeSHMInfo *pNodeInfo);

//先尝试更新，再尝试插入----协商
int KeyMngsvr_UpInsr_DBOp_WriteSecKey(void *dbhdl, NodeSHMInfo *pNodeInfo);

//直接更新----注销
int KeyMngsvr_Update_DBOp_WriteSecKey(void *dbhdl, NodeSHMInfo *pNodeInfo);

#ifdef __cplusplus
}
#endif

#endif
