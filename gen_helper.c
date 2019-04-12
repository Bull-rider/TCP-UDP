/*************************************************
Copyright (C), 2007-2017, ShenZhen Hymson Laser Technologies Co., Ltd.
File name: gen_helper.c
Author:
Version:
Date: 2017.06.13
Description:
Others:

Function List:
1. ....
History:
Date:
Author:
Modification:

2. ...
*************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <assert.h>
#include <errno.h>

#include <unistd.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <sys/time.h>
#include <time.h>
#include <sys/select.h>

#include "gen_helper.h"

#define LOOPREADPOS     (pLoopMem->m_head + pLoopMem->m_read)
#define LOOPWRITEPOS    (pLoopMem->m_head + pLoopMem->m_write)
#define KEYVALLEN       100

// Delete the left space
char *l_trim(char *szOutput, const char *szInput)
{
    for (; *szInput != '\0' && isspace(*szInput); ++szInput)
    {
        ;
    }
    
    return strcpy(szOutput, szInput);
}

// Delete the right space
char *r_trim(char *szOutput, const char *szInput)
{
    char *p = NULL;
    
    strcpy(szOutput, szInput);
    for (p = szOutput + strlen(szOutput) - 1; p >= szOutput && isspace(*p); --p)
    {
        ;
    }
    
    *(++p) = '\0';
    return szOutput;
}

// Delete spaces on both sides
char *a_trim(char *szOutput, const char *szInput)
{
    char *p = NULL;
    
    l_trim(szOutput, szInput);
    for (p = szOutput + strlen(szOutput) - 1;p >= szOutput && isspace(*p); --p)
    {
        ;
    }
    
    *(++p) = '\0';
    return szOutput;
}

/************************************************
从配置文件读取配置项
参数:
    *profile        配置文件名, 绝对路径
    *AppName        AppName
    *KeyName        KeyName
    *KeyVal         返回KeyVal
**************************************************/
int GetProfileString(char *profile, char *AppName, char *KeyName, char *KeyVal)
{
    char appname[32], keyname[32];
    char *buf, *c;
    char buf_i[KEYVALLEN], buf_o[KEYVALLEN];
    
    FILE *fp;
    int found=0; /* 1 AppName 2 KeyName */
    
    if ((fp=fopen(profile, "r")) == NULL)
    {
        printf("openfile [%s] error [%s]\n", profile, strerror(errno));
        return(-1);
    }
    
    fseek(fp, 0, SEEK_SET);
    memset(appname, 0, sizeof(appname));
    sprintf(appname, "[%s]", AppName);
    
    while (!feof(fp) && (fgets(buf_i, KEYVALLEN, fp) != NULL))
    {
        l_trim(buf_o, buf_i);
        
        if (strlen(buf_o) <= 0)
            continue;
        
        buf = NULL;
        buf = buf_o;
        
        if (found == 0)
        {
            if (buf[0] != '[')
            {
                continue;
            }
            else if (strncmp(buf, appname, strlen(appname)) == 0)
            {
                found = 1;
                continue;
            }
        }
        else if (found == 1)
        {
            if (buf[0] == '#')
            {
                continue;
            }
            else if (buf[0] == '[')
            {
                break;
            }
            else
            {
                if ((c = (char*)strchr(buf, '=')) == NULL)
                    continue;
                
                memset(keyname, 0, sizeof(keyname));
                sscanf(buf, "%[^=|^ |^\t]", keyname);
                
                if (strcmp(keyname, KeyName) == 0)
                {
                    sscanf(++c, "%[^\n]", KeyVal);
                    char *KeyVal_o = (char *)malloc(strlen(KeyVal) + 1);
                    
                    if (KeyVal_o != NULL)
                    {
                        memset(KeyVal_o, 0, sizeof(KeyVal_o));
                        a_trim(KeyVal_o, KeyVal);
                        
                        if(KeyVal_o && strlen(KeyVal_o) > 0)
                            strcpy(KeyVal, KeyVal_o);
                        
                        free(KeyVal_o);
                        KeyVal_o = NULL;
                    }
                    
                    found = 2;
                    break;
                }
                else
                {
                    continue;
                }
            }
        }
    }
    
    fclose(fp);
    
    if (found == 2)
        return(0);
    
    return(-1);
}

/************************************************
设置操作系统时间
参数:*dt数据格式为"2006-4-20 20:30:30"
调用方法:
    char *pt="2006-4-20 20:30:30";
    SetSystemTime(pt);
**************************************************/
int SetSystemTime(char *dt)
{
    struct tm ltm;
    struct tm _tm;
    struct timeval tv;
    time_t timep;
    
    sscanf(dt, "%d-%d-%d %d:%d:%d", &ltm.tm_year, &ltm.tm_mon, &ltm.tm_mday, &ltm.tm_hour, &ltm.tm_min, &ltm.tm_sec);
    _tm.tm_sec  = ltm.tm_sec;
    _tm.tm_min  = ltm.tm_min;
    _tm.tm_hour = ltm.tm_hour;
    _tm.tm_mday = ltm.tm_mday;
    _tm.tm_mon  = ltm.tm_mon - 1;
    _tm.tm_year = ltm.tm_year - 1900;
    
    timep = mktime(&_tm);
    tv.tv_sec = timep;
    tv.tv_usec = 0;
    if (settimeofday(&tv, (struct timezone *)0) < 0)
    {
        printf("Set system datatime error!\n");
        return -1;
    }
    
    return 0;
}

int InitLoopMem(LoopMem *pLoopMem, int lsize)
{
    pLoopMem->m_head  = malloc(lsize);;
    if (pLoopMem->m_head == NULL)
        return -1;
    
    pLoopMem->m_size  = lsize;
    pLoopMem->m_free  = lsize;
    pLoopMem->m_read  = 0;
    pLoopMem->m_write = 0;
    
    return 0;
}

int LoopMemRead(LoopMem *pLoopMem, char *pBuff, int lsize)
{
    int rsize = 0;
    
    if ((pLoopMem == NULL) || (lsize == 0) || (lsize > (pLoopMem->m_size - pLoopMem->m_free)))
        return -1;
    
    if ((pLoopMem->m_read + lsize) < pLoopMem->m_size)
    {
        memcpy(pBuff, LOOPREADPOS, lsize);
        pLoopMem->m_read += lsize;
        pLoopMem->m_free += lsize;
    }
    else
    {
        // 分两部分拷贝，第一部分读取到内存空间最后处
        rsize = pLoopMem->m_size - pLoopMem->m_read;
        memcpy(pBuff, LOOPREADPOS, rsize);
        
        // 第二部分将剩余部分从内存空间开始处读取
        pLoopMem->m_read = 0;
        memcpy(pBuff + rsize, LOOPREADPOS, lsize - rsize);
        pLoopMem->m_read += lsize - rsize;
        pLoopMem->m_free += lsize;
    }
    
    return 0;
}

int LoopMemWrite(LoopMem *pLoopMem, char *pBuff, int lsize)
{
    int wsize = 0;
    
    if ((pLoopMem == NULL) || (lsize > pLoopMem->m_free))
        return -1;
    
    if ((pLoopMem->m_write + lsize) < pLoopMem->m_size)
    {
        memcpy(LOOPWRITEPOS, pBuff, lsize);
        pLoopMem->m_write += lsize;
        pLoopMem->m_free  -= lsize;
    }
    else
    {
        // 分两部分拷贝，第一部分拷贝到内存空间最后处
        wsize = pLoopMem->m_size - pLoopMem->m_write;
        memcpy(LOOPWRITEPOS, pBuff, wsize);
        
        // 第二部分将剩余部分从内存空间开始处拷贝
        pLoopMem->m_write  = 0;
        memcpy(LOOPWRITEPOS, pBuff+wsize, lsize - wsize);
        pLoopMem->m_write += lsize - wsize;
        pLoopMem->m_free  -= lsize;
    }
    
    return 0;
}

