/*************************************************
Copyright (C), 2007-2017, ShenZhen Hymson Laser Technologies Co., Ltd.
File name: parse_stream.c
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
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <sys/time.h>
#include <pthread.h>
#include <stdarg.h>

#include "gen_helper.h"
#include "parse_stream.h"

#define OFFLINE_FILE                "/data/offline.bin"
#define OFFLINE_FILE1                "/data/offline1.bin"
#define INVALID_FPGA_VERSION        "[FPGA Version: invalid date]"
#define FPGA_DEV_IOC_MAGIC          'F'
#define FPGA_DEV_IOC_READ_ENCODER   _IOR(FPGA_DEV_IOC_MAGIC, 0x01, int)

#define PAI                         3.1415926
#define LOGFILE_SIZE                2097152
#define CK_TIME                     1

static int work_speed   = 400;
static int axis_dia     = 20000;    //20000*4;//36600;
static int sub_pulse    = 31416;    //104720;//120000;//65536;//
static char io_input    = 0x00;
static sem_t file_lock;

int countdown2_buf_pos  = 0;        //倒数第2个极耳数据偏移位置
int beginindex_buf_pos  = 0;        //切入后起切极耳数据偏移位置
int cuttingout_buf_pos  = 0;        //切出线数据偏移位置
int MarkPlus            = 0;
int file_exist          = 0;
sem_t logfile_lock;

#define LOOPMEM_SIZE    65536
int loopmem_flag        = 0;        //是否内存打印     0: 关闭      1: 启动
int loopmem_pipe        = 1;        //内存打印输出管道 0: 日志文件  1: 网络
LoopMem gLoopMemLog;
char pMsgBuff[LOOPMEM_SIZE];

int  pole_index         = 0;        // parse_file统计步数
int  group_counts       = 0;        // parse_file统计产量
int  fpga_pole_index    = 0;        // FPGA统计步数
int  fpga_group_counts  = 0;        // FPGA统计产量
int  fpga_group_index   = 0;
int fpga_group_start   = 0;
int  pole_count         = 1;        // 极耳个数
char is_follow_speed    = 0;        // 速度跟随，用于红光测试

int  p3trigger_counts   = 0;
int  p4trigger_counts   = 0;

int  pp0trigger_counts  = 0;        // INT0触发统计
int  pp2trigger_counts  = 0;        // INT2触发统计
int  pp3trigger_counts  = 0;        // INT3触发统计
int  pp4trigger_counts  = 0;        // INT4触发统计
int  pp5trigger_counts  = 0;        // INT5触发统计

int  pp0trigger_status  = 0;        // INT0状态
int  pp2trigger_status  = 0;        // INT2状态
int  pp3trigger_status  = 0;        // INT2状态
int  pp4trigger_status  = 0;        // INT2状态
int  pp5trigger_status  = 0;        // INT5状态

// CPU占用率统计
float cpuusage          = 0;
int isAutoTrigger       = 0;       // 0: 非自触发, 1: 自触发, -1: 初始未定
int isStartEdge         = 0;        // 0: 非起始边, 1: 起始边
int ArmRunModel         = 0;        // 0: offline模式, 1: online模式
int isSingleMode        = 0;        // 0: 循环模式, 1: 单次模式
int isOnlineModelFlag   = 0;        // 0: 未启动测试,  1: 启动测试

int isskiptapeflag      = 1;        // 0: 不支持切胶带(切直线后切最后两个极耳)，1: 支持切胶带
int skiptapeswitch      = 0;        // 0: 低电平切胶带, 1: 高电平切胶带
int beginindex          = 0;        // 切入后开始起切的极耳序号, 序号从0开始，负数表示倒数
int capeindex			= 0;        // 过胶带后开始起切的极耳序号, 序号从0开始，负数表示倒数
int utfpgaversion       = 1;        // 0: 171204之前版本 1: 171204版本

static int fpga_read_input(int fpga_fd);
void thread_fpga_getspeed_create(int fpga_fd);
void WriteBufToFpga(int fpga_fd, char *cBuf, int nSize, int bIsWirte);
void thread_getcd32pos_create(char *pbuf);

enum data_type {
    cmd_setfq               = 1,
    cmd_setpower            = 2,
    cmd_movescanner         = 3,
    cmd_laseron             = 4,
    cmd_laseroff            = 5,
    cmd_setredlight         = 6,
    cmd_setshutter          = 7,
    cmd_setmarkstart        = 8,
    cmd_setmarkstop         = 9,
    cmd_setio_output        = 10,
    cmd_setmindelay         = 11,
    cmd_setsubdelay         = 12,
    cmd_setlasertype        = 13,
    cmd_setiooutputtype     = 14,
    cmd_setstartmarkmode    = 15,
    cmd_cut_stop_point      = 16,
    
    // suport SPI laser 2017.08.01
    cmd_spiwave             = 17,
    cmd_setspicomm          = 18,
    cmd_setbrightmode       = 19,
    
    cmd_getio               = 51,
    cmd_getlaser_status     = 52,
    cmd_getsys_status       = 53,
    cmd_getshutter_status   = 54,
    cmd_set_encoder_level   = 55,
    cmd_skip_fifodata_end   = 56,
    
    // support start edge 2017.11.20
    cmd_startedge_model     = 57,
    cmd_startedge_distance  = 58,
    
    cmd_FTSetOffLineMode    = 61,
    cmd_FTLoadDataStart     = 62,
    cmd_FTLoadDataEnd       = 63,
    cmd_FTPoleCounts        = 64,
    cmd_FTWorkSpeed         = 65,
    cmd_FTAxisDia           = 66,
    cmd_FTIsFollowSpeed     = 67,
    cmd_FTBlockStart        = 68,
    cmd_FTBlockEnd          = 69,
    cmd_FSubPulse           = 70,
    
    // suport auto trigger 2017.08.16
    cmd_FPoleWidthByNo      = 71,
    cmd_FSetAsix            = 72,
    cmd_FPoleOffsetY        = 73,
    
    // suport cut tape 2017.09.18
    cmd_FCutTape1           = 74,
    cmd_FCutTape2           = 75,
    cmd_FFeedSpace          = 76,
    
    // support ARM online mode 2017.12.07
    cmd_FRunModel           = 77,
    
    cmd_TABLEEND
};

enum omc_type {
    omc_reset_index         = 0x01,
    omc_reset_group         = 0x02,
    omc_set_index           = 0x03,
    omc_set_group           = 0x04,
    omc_query_indexgroup    = 0x05,
    
    omc_get_loopmemflag     = 0x11,
    omc_get_loopmempipe     = 0x12,
    omc_set_loopmemflag     = 0x13,
    omc_set_loopmempipe     = 0x14,
    omc_query_jierdata      = 0x15,
    
    omc_reset_trigcnts      = 0x21,
    omc_query_trigcnts      = 0x22,
    
    omc_set_systime         = 0x23,
    omc_get_version         = 0x24,
    omc_get_fpgadata        = 0x25,
    omc_direct_fpgacmd      = 0x26,
    
    omc_set_fpgatriggermode = 0x31,
    omc_set_startedgemode   = 0x32,

	omc_get_1 = 0x33,	//极耳间距
	omc_get_2 = 0x34,	//起始边到激光的距离
	omc_get_3 = 0x35,	//起始边使能方式
	omc_get_4 = 0x36,	//急停后切直线距离
	omc_get_5 = 0x37,	//过胶带功能启用状态
	omc_get_6 = 0x38,	//胶带感应器到激光距离
	omc_get_7 = 0x39,	//过胶带后切直线距离
    
    omc_TABLEEND
};

static char cmd_len_table[cmd_TABLEEND];

void cmd_len_table_init(void)
{
    cmd_len_table[cmd_setfq]              = 4;
    cmd_len_table[cmd_setpower]           = 4;
    cmd_len_table[cmd_movescanner]        = 4;
    cmd_len_table[cmd_laseron]            = 0;
    cmd_len_table[cmd_laseroff]           = 0;
    cmd_len_table[cmd_setredlight]        = 1;
    cmd_len_table[cmd_setshutter]         = 1;
    cmd_len_table[cmd_setmarkstart]       = 0;
    cmd_len_table[cmd_setmarkstop]        = 0;
    cmd_len_table[cmd_setio_output]       = 1;
    cmd_len_table[cmd_setmindelay]        = 4;
    cmd_len_table[cmd_setsubdelay]        = 4;
    cmd_len_table[cmd_setlasertype]       = 1;
    cmd_len_table[cmd_setiooutputtype]    = 1;
    cmd_len_table[cmd_setstartmarkmode]   = 1;
    cmd_len_table[cmd_cut_stop_point]     = 0;
    
    // suport SPI laser 2017.08.01
    cmd_len_table[cmd_spiwave]            = 1;
    cmd_len_table[cmd_setspicomm]         = 1;
    cmd_len_table[cmd_setbrightmode]      = 1;
    
    cmd_len_table[cmd_getio]              = 0;
    cmd_len_table[cmd_getlaser_status]    = 0;
    cmd_len_table[cmd_getsys_status]      = 0;
    cmd_len_table[cmd_getshutter_status]  = 0;
    cmd_len_table[cmd_set_encoder_level]  = 1;
    cmd_len_table[cmd_skip_fifodata_end]  = 0;
    
    // support start edge 2017.11.20
    cmd_len_table[cmd_startedge_model]    = 1;
    cmd_len_table[cmd_startedge_distance] = 4;
    
    cmd_len_table[cmd_FTSetOffLineMode]   = 1;
    cmd_len_table[cmd_FTLoadDataStart]    = 0;
    cmd_len_table[cmd_FTLoadDataEnd]      = 0;
    cmd_len_table[cmd_FTPoleCounts]       = 4;
    cmd_len_table[cmd_FTWorkSpeed]        = 4;
    cmd_len_table[cmd_FTAxisDia]          = 4;
    cmd_len_table[cmd_FTIsFollowSpeed]    = 1;
    cmd_len_table[cmd_FTBlockStart]       = 1;
    cmd_len_table[cmd_FTBlockEnd]         = 1;
    cmd_len_table[cmd_FSubPulse]          = 4;
    
    // suport auto trigger 2017.08.16
    cmd_len_table[cmd_FPoleWidthByNo]     = 4;
    cmd_len_table[cmd_FSetAsix]           = 1;
    cmd_len_table[cmd_FPoleOffsetY]       = 4;
    
    // suport cut tape 2017.09.18
    cmd_len_table[cmd_FCutTape1]          = 4;
    cmd_len_table[cmd_FCutTape2]          = 4;
    cmd_len_table[cmd_FFeedSpace]         = 4;
    
    // support ARM online mode 2017.12.07
    cmd_len_table[cmd_FRunModel]          = 1;
}

void parse_init(int fpga_fd)
{
    cmd_len_table_init();
    
    sem_init(&file_lock, 0, 1);
    thread_fpga_getspeed_create(fpga_fd);
    sleep(1);
    fpga_read_input(fpga_fd);
}


void getNowTime(char *strTime)
{
    time_t now;
    struct tm *timenow;
    
    time(&now);
    timenow = localtime(&now);
    
    sprintf(strTime, "[%04d%02d%02d %02d:%02d:%02d]", timenow->tm_year+1900, timenow->tm_mon+1, timenow->tm_mday, timenow->tm_hour, timenow->tm_min, timenow->tm_sec);
}

void write_log(int bNewline, char *buf)
{
    FILE *fp = NULL;
    char strTime[64], strMsg[MAX_LENGTH];
    
    // Initialize strMsg with initial value of 0
    memset(strMsg, 0, MAX_LENGTH);
    memset(strTime, 0, 64);
    getNowTime(strTime);
    
    // To determine whether the log file exists
    if ((0 == file_exist) && (access(RUNLOG_FILE, 0) == 0))
        file_exist = 1;
    
    // 如果日志文件存在，则第一行不空行
    if ((1 == bNewline) && (1 == file_exist))
        sprintf(strMsg, "\n\n%s %s", strTime, buf);
    else
        sprintf(strMsg, "%s %s", strTime, buf);
    
    if (sem_wait(&logfile_lock) == 0)
    {
        fp = fopen(RUNLOG_FILE, "a+");
        fwrite(strMsg, strlen(strMsg), 1, fp);
        
        fclose(fp);
        sem_post(&logfile_lock);
        
        if (0 == file_exist)
            file_exist = 1;
    }
}

void memprintf(const char* strMsg, ...)
{
    char strBuffer[256] = {0};
    char strTime[64], strResult[MAX_LENGTH];
    int  iLength = 0;
    
    if (loopmem_flag == 0)
        return;
    
    va_list vlArgs;
    va_start(vlArgs, strMsg);
    vsnprintf(strBuffer, sizeof(strBuffer)-1, strMsg, vlArgs);
    va_end(vlArgs);
    
    // Initialize strMsg with initial value of 0
    memset(strResult, 0, MAX_LENGTH);
    memset(strTime, 0, 64);
    getNowTime(strTime);
    
    sprintf(strResult, "%s %s", strTime, strBuffer);
    iLength = strlen(strResult);
    if (iLength < gLoopMemLog.m_free)
        LoopMemWrite(&gLoopMemLog, strResult, iLength);
}

void memwrite_log(void)
{
    FILE *fp = NULL;
    char pBuff[LOOPMEM_SIZE];
    int  rsize = 0;
    
    if (loopmem_pipe != 0)
        return;
    
    rsize = gLoopMemLog.m_size - gLoopMemLog.m_free;
    if (rsize == 0)
        return;
    
    fp = fopen(MEMLOG_FILE, "a+");
    LoopMemRead(&gLoopMemLog, pBuff, rsize);
    fwrite(pBuff, rsize, 1, fp);
    
    fclose(fp);
}

int parse_stream(char *buf, int len, int *filelen)
{
    static char cmd_len;
    char write_len;
    static char cmd_left_len=0; //when cmd does not complete
    static int data_len=0;
    static FILE *fp=NULL;
    static int data_loading=0;
    
    char pMsgbuf[256];
    
    if (cmd_left_len > 0)
    {
        if (cmd_left_len < len)
        {
            write_len = cmd_left_len;
            if (fp)
            {
                fwrite(buf, write_len, 1, fp);
                data_len += write_len;
            }
            
            buf += write_len;
            len -= write_len;
        }
        else
        {
            write_len = len;
            if (fp)
            {
                fwrite(buf, len, 1, fp);
                data_len += write_len;
            }
            
            cmd_left_len = cmd_left_len - len;
            
            return 0;
        }
    }
    
    if (*buf > cmd_TABLEEND)
    {
        sprintf(pMsgbuf, "data stream error =========\n\t\t    fp=%p, *buf=0x%02x, len=%d, write_len=%d, cmd_left_len=%d, data_len=%d\n", 
            fp, *buf, len, write_len, cmd_left_len, data_len);
        PRINTFLOG(pMsgbuf);
        
        // 上传出现错误，重传数据
        if (fp != NULL)
        {
            fclose(fp);
            sem_post(&file_lock);
            fp = NULL;
            
            PRINTFLOG("writing file, received error data stream!\n");
        }
        
        return -EAGAIN;
    }
    
    while(len > 0)
    {
        cmd_len = 0;
        switch(*buf)
        {
        // support ARM online mode 2017.12.07
        case cmd_FRunModel:
            buf++;
            ArmRunModel  = ((((*buf)&0x01) == 0x01) ? 1 : 0);
            isSingleMode = ((((*buf)&0x02) == 0x02) ? 1 : 0);
            len -= 2;
            
            /*sprintf(pMsgbuf, "Set ARM Running Model: %d, isSingleMode: %d\n", ArmRunModel, isSingleMode);
            PRINTFLOG(pMsgbuf);*/
            continue;
        case cmd_FTSetOffLineMode:
            cmd_len = 1;
            //buf++;
            break;
        case cmd_FTLoadDataStart:
            if(sem_trywait(&file_lock) != 0)
            {
                return -EAGAIN;             // 锁定失败
            }
            
            if(fp == NULL) {
                fp = fopen(OFFLINE_FILE, "w+");
                data_loading=1;
                data_len = 0;
                printf("open file\n");
                fwrite(buf, 1, 1, fp);      // 写入文件头
                data_len += 1;
            }
            else
                printf("error: file already opened\n");
            
            buf++;
            len--;
            continue;
            //break;
            
        case cmd_FTLoadDataEnd:
            if(data_loading)
            {
                data_loading = 0;
                fwrite(buf, 1, 1, fp);      // 写入文件尾
                data_len += 1;
                if(fp != NULL)
                {
                    printf("data_len = %d\n", data_len);
                    fclose(fp);
                    sem_post(&file_lock);
                    fp = NULL;
                    *filelen = data_len;     // 返回文件长度
                    
                    return 2;
                }
                else
                    printf("error: file already closed\n");
            }
            
            buf++;
            len--;
            continue;
            //break;
            
        case cmd_FTPoleCounts:
            break;
        case cmd_FTWorkSpeed:
            break;
        case cmd_FTAxisDia:
            break;
        case cmd_FSubPulse:
            break;
        case cmd_FTIsFollowSpeed:
            break;
        case cmd_FTBlockStart:
            break;
        case cmd_FTBlockEnd:
            break;
            
        case cmd_setfq:
            break;
        case cmd_setpower:
            break;
        case cmd_movescanner:
            //printf("movescanner\n");
            break;
        case cmd_setredlight:
            break;
        case cmd_setshutter:
            break;
        case cmd_setio_output:
            break;
        case cmd_setmindelay:
            break;
        case cmd_setsubdelay:
            break;
        case cmd_setlasertype:
            break;
        case cmd_setiooutputtype:
            break;
        case cmd_setstartmarkmode:
            //printf("set mark mode\n");
            break;
        
        // suport SPI laser 2017.08.01
        case cmd_spiwave:
            break;
        case cmd_setspicomm:
            break;
        case cmd_setbrightmode:
            break;
        
        // suport auto trigger 2017.08.16
        case cmd_FPoleWidthByNo:
            break;
        case cmd_FSetAsix:
            break;
        case cmd_FPoleOffsetY:
            break;
        
        // suport cut tape 2017.09.18
        case cmd_FCutTape1:
            break;
        case cmd_FCutTape2:
            break;
        case cmd_FFeedSpace:
            break;
        
        // support start edge 2017.11.20
        case cmd_startedge_model:
            break;
        case cmd_startedge_distance:
            break;
        
        // 如下是指令长度为零的情况
        case cmd_laseron:
            break;
        case cmd_laseroff:
            break;
        case cmd_setmarkstart:
            break;
        case cmd_setmarkstop:
            break;
        case cmd_cut_stop_point:
            break;
        case cmd_getio:
            break;
        case cmd_getlaser_status:
            break;
        case cmd_getsys_status:
            break;
        case cmd_getshutter_status:
            break;
        default:
            break;
        }
        
        cmd_len = cmd_len_table[(int)*buf];
        len--;  // cmd byte
        write_len = 1;
        
        if(cmd_len>0) {
            if(cmd_len<=len) {
                write_len += cmd_len;
                cmd_left_len = 0;
            }
            else {
                cmd_left_len = cmd_len - len;
                write_len += len;
            }
        }
        
        if(fp) {
            fwrite(buf, write_len, 1, fp);
            data_len += write_len; 
        }
        
        buf += write_len;
        len -= cmd_len; // if(len-cmd_len < 0), no problem.
    }
    
    return 0;
}

//0表示不工作，1表示工作
int is_work()
{
    //5号端口为低电不做踏点处理
    if ((io_input&0x20) == 0x20)
        return 0;
    else
        return 1;
}

void is_skiptape()
{
    static int CutLineTrigcounts = 0;
    
    //3号端口保持高电平, 下降沿触发切胶带
    if ((((io_input&0x08) == 0x08) && (skiptapeswitch == 0)) ||
        (((io_input&0x08) != 0x08) && (skiptapeswitch == 1)))
    {
        if (CutLineTrigcounts >= 3)
        {
            p3trigger_counts++;
            MarkPlus = 1;
        }
        
        CutLineTrigcounts = 0;
    }
    else
        CutLineTrigcounts++;
   

    if ((io_input&0x20) == 0x20)
        MarkPlus = 0;
}

void is_triggerstat()
{
    static char pre_io_input = 0;
    
    // 0号端口信号触发统计
    if (((io_input&0x01) != 0x01) && ((io_input&0x01) != (pre_io_input&0x01)))
        pp0trigger_counts++;
    
    // 2号端口信号触发统计
    if (((io_input&0x04) != 0x04) && ((io_input&0x04) != (pre_io_input&0x04)))
        pp2trigger_counts++;
    
    // 3号端口信号触发统计
    if (((io_input&0x08) != 0x08) && ((io_input&0x08) != (pre_io_input&0x08)))
        pp3trigger_counts++;
    
    // 4号端口信号触发统计
    if (((io_input&0x10) != 0x10) && ((io_input&0x10) != (pre_io_input&0x10)))
        pp4trigger_counts++;
    
    // 5号端口触发统计
    if (((io_input&0x20) != 0x20) && ((io_input&0x20) != (pre_io_input&0x20)))
        pp5trigger_counts++;
    
	pp0trigger_status = ((io_input&0x01) != 0x01);
	pp2trigger_status = ((io_input&0x04) != 0x04);
	pp3trigger_status = ((io_input&0x08) != 0x08);
	pp4trigger_status = ((io_input&0x10) != 0x10);
	pp5trigger_status = ((io_input&0x20) != 0x20);

    pre_io_input = io_input;
}

//写块数据
void WriteBufToFpga(int fpga_fd, char *cBuf, int nSize, int bIsWirte)
{
    static int nBufSize=0;
    static char cBufW[30]={0};
    
    if (bIsWirte == 0)
    {
        memcpy(cBufW+nBufSize, cBuf, nSize);
        nBufSize += nSize;
        
        if (nBufSize>=20)
        {
            write(fpga_fd, cBufW, nBufSize);
            nBufSize = 0;
        }
    }
    else
    {
        if(nBufSize > 0)
        {
            write(fpga_fd, cBufW, nBufSize);
            nBufSize = 0;
        }
    }
}

//文件解析
void parse_file(int fpga_fd)
{
    FILE *offline_file_fp = NULL;
    int  cmd;
    int  cmd_len;
    char cmd_buf[5];
    int  block_pos[3] = {0,0,0};    // 三个字节，0：表示开始打标的数据块，1：表示积耳数据块，2：表示结束打标数据块
    char is_first_run    = 1;
    char is_single_run   = 0;       // 在线模式时, 是否单次运行
    char mathine_type    = 0;
    int  start_flag      = 0;
    int  file_size       = 0;
    char *offline_buf_p  = 0;
    int  offline_buf_pos = 0;
    char _pulse10us      = 2;
    struct timeval pretv, nowtv;
	func_getcd32pos();
    int pre_cmd = 0;
    while (1)
    {
        // is_work() 0表示不工作，1表示工作. 5号端口为低
        if (((ArmRunModel == 0) && (is_work() == 0)) || 
            ((ArmRunModel == 1) && (is_single_run == 1)))
        {
            pole_index        = 0;
            group_counts      = 0;
            fpga_pole_index   = 0;
            fpga_group_counts = 0;
			fpga_group_index = 0;
            memwrite_log();
			
            // 测试完成后, 需要将单次执行标识复位
            if (ArmRunModel == 0)
                is_single_run = 0;
            
            if (start_flag == 0)
            {
                usleep(5000);
                continue;
            }
        }
        else
        {   //5号端口为高
            if (start_flag == 0)
            {
                sem_wait(&file_lock);       // 锁定文件，不可上传文件
                if (offline_file_fp == NULL)
                {
                    PRINTFLOG("######  Begin Woking!!!  ######\n");
                    offline_file_fp = fopen(OFFLINE_FILE, "r");
                    if(offline_file_fp == NULL)     // 打开文件失败，释放信号量，重新尝试
                    {
                        perror("open file :");
                        sem_post(&file_lock);
                        sleep(1);
                        continue;
                    }
                    fseek(offline_file_fp, 0, SEEK_END);
                    file_size = ftell(offline_file_fp);
                    offline_buf_p = malloc(file_size);
                    if(offline_buf_p == NULL)
                    {
                        PRINTFLOG("[Core Error!!!]malloc error\n");
                        sleep(1);
                        
                        exit(0);
                    }
                    fseek(offline_file_fp, 0, SEEK_SET);
                    fread(offline_buf_p, 1, file_size, offline_file_fp);
                    fclose(offline_file_fp);
                    offline_buf_pos    = 0;
                    pole_index         = 0;
                    group_counts       = 0;
                    p3trigger_counts   = 0;
                    p4trigger_counts   = 0;
                    is_first_run       = 1;     // added by wmp 2017.11.29
                    is_single_run      = 0;
                    
                    // Support Single Running Mode, added by wmp 2018.02.27
                    if (ArmRunModel == 0)
                        isSingleMode = 0;
                    
                    gettimeofday(&nowtv, NULL);
                    pretv = nowtv;
                    // 解析文件时间过长导致起切位置无效
                    //thread_getcd32pos_create(offline_buf_p);     // 创建查找倒数第三/二个极耳偏移位置线程
                }
                start_flag = 1;
                //fseek(offline_file_fp, 0, SEEK_SET);
            }
        }
        
//      cmd = fgetc(offline_file_fp);
        cmd = offline_buf_p[offline_buf_pos++];
        if(offline_buf_pos > file_size)
        {
            //rewind(offline_file_fp);
            printf("=========data error \n");
            offline_buf_pos = 0;
            continue;
        }
		/*if (pre_cmd != cmd || cmd != 3)
		{
			sprintf(pMsgBuff,"pre-cmd:(%d),cmd:(%d)\r\n",pre_cmd,cmd);
			PRINTFLOG(pMsgBuff);
			pre_cmd = cmd;
		}*/
        cmd_len = cmd_len_table[cmd];
        if(cmd_len)
        {
            memcpy(cmd_buf+1, &offline_buf_p[offline_buf_pos], cmd_len);
            offline_buf_pos += cmd_len;
        }
        cmd_buf[0] = cmd;
        switch(cmd)
        {
        case cmd_FTSetOffLineMode:
            continue;
        case cmd_FTLoadDataStart:  //数据开始 
            continue;
        case cmd_FTLoadDataEnd:  //数据结束
             //跳回到数据开始
            WriteBufToFpga(fpga_fd, cmd_buf, 0, 1);
            offline_buf_pos = block_pos[0];
            if(offline_file_fp != NULL)
            {
                free(offline_buf_p);
                offline_file_fp = NULL;
                sem_post(&file_lock);
                start_flag = 0;
				
                if ((ArmRunModel == 1) && (isSingleMode == 1))
                    is_single_run = 1;
                
                PRINTFLOG("######  End Woking!!!  ######\n");
            }
            
            continue;
        case cmd_FTPoleCounts:
            memcpy(&pole_count, cmd_buf+1, sizeof(pole_count));
            continue;
        case cmd_FTIsFollowSpeed://是否踏点处理，0、不踏点，1、踏点
            memcpy(&is_follow_speed, cmd_buf+1, sizeof(is_follow_speed));
            //printf("follow speed met [%d.%d]\n", (int)(*(cmd_buf)), (int)(*(cmd_buf+1)));
            break;
            //continue;
        case cmd_FTBlockStart:
            //printf("cmd_FTBlockStart = %d\n", cmd_buf[1]);
            if (cmd_buf[1] == 1) //积耳数据块
            {
                block_pos[1] = offline_buf_pos-2;
                pole_index   = 0;
				fpga_group_index = 0;
                if ((isAutoTrigger == 1) && (isOnlineModelFlag == 0) && (ArmRunModel == 1))
                {
                    isOnlineModelFlag = 1;
                    cmd_buf[0]        = cmd_FRunModel;
                    cmd_buf[1]        = 1;
                    WriteBufToFpga(fpga_fd, cmd_buf, 2, 0);
                }
				/*sprintf(pMsgBuff,"isAutoTrigger:(%d),ArmRunModel:(%d)\r\n",isAutoTrigger,ArmRunModel);
				PRINTFLOG(pMsgBuff);*/
            }
            else if (cmd_buf[1] == 0x0B)
            {
                // 增加OUT1极耳组反馈信息
                /*if ((isAutoTrigger == 0) && (utfpgaversion >= 1) && ((pole_index == 0) || (pole_index == (pole_count-1))))
                {
                    cmd_buf[0]  = cmd_FTBlockEnd;
                    cmd_buf[1]  = ((pole_index == 0) ? 1 : 0);
                    WriteBufToFpga(fpga_fd, cmd_buf, 2, 0);
					memprintf("OB:[%d],[%d],[%d], [%d]\r\n", isAutoTrigger, utfpgaversion, pole_index, pole_count);
	             }*/
				/*if ((isAutoTrigger == 0) && (utfpgaversion >= 1) && is_follow_speed != 0)
				{
					if (fpga_group_index  == 0 || fpga_group_index  >= 3 )
					{
						cmd_buf[0]  = cmd_FTBlockEnd;
						cmd_buf[1]  = 1;
						WriteBufToFpga(fpga_fd, cmd_buf, 2, 0);
						memprintf("OB:[0],\r\n");
						fpga_group_start = 0;
						fpga_group_index  = 1;
					}

					if (fpga_group_start == 1)
					{
						fpga_group_index ++;
					}
				}*/
				
				/*sprintf(pMsgBuff,"isSingleMode:(%d),ArmRunModel:(%d),is_first_run:(%d)\r\n",isSingleMode,ArmRunModel,is_first_run);
				PRINTFLOG(pMsgBuff);*/
                if ((ArmRunModel == 1) && (isSingleMode == 1) && (is_first_run == 0))
                {
                    offline_buf_pos = cuttingout_buf_pos;
                    continue;
                }
                
                // 对于非自触发版本，要求从倒数第三个极耳开始工作  added by weimoping 2017.11.29
                // 要求非起始边(起始边还是要从第一个开始)  added by weimoping 2017.12.04
                // 支持自触发版本 added by weimoping 2018.01.11
                if ((beginindex != 0) && (isStartEdge == 0) && (pole_index == 0) && (is_first_run == 1))
                {
                    offline_buf_pos = beginindex_buf_pos;

					if ((ArmRunModel == 1) && (pole_count < 2))
					{
						offline_buf_pos = beginindex_buf_pos+2;//当极耳总数为1时，手动启动会造成死循环，beginindex_buf_pos+2跳过当前指令
					}
                    pole_index      = ((beginindex % pole_count) + pole_count) % pole_count;
                    continue;
                }
                else
                {
                    is_first_run = 0;
                }

				/*sprintf(pMsgBuff,"beginindex:(%d),isStartEdge:(%d),pole_index:(%d)\r\n",beginindex,isStartEdge,pole_index);
				PRINTFLOG(pMsgBuff);*/

                gettimeofday(&nowtv, NULL);
                if (nowtv.tv_usec >= pretv.tv_usec)
                    memprintf("[CPU:%6.2f\%]Intervals Time[pole_index:%d/%d]: %ds, %dus\r\n", cpuusage, pole_index, group_counts, nowtv.tv_sec - pretv.tv_sec, nowtv.tv_usec - pretv.tv_usec);
                else
                    memprintf("[CPU:%6.2f\%]Intervals Time[pole_index:%d/%d]: %ds, %dus\r\n", cpuusage, pole_index, group_counts, nowtv.tv_sec - pretv.tv_sec - 1, 1000000 + nowtv.tv_usec - pretv.tv_usec);
                pretv = nowtv;
                
                if (MarkPlus == 1)
                {
					MarkPlus = 0;
                    offline_buf_pos = countdown2_buf_pos;
                    
                    // 新增ARM与FPGA之间切胶带命令，用于指示FPGA切直线结束 2017.10.16
                    // 增加自触发分支才下发此指令的判断 2017.11.20
                   // if (isAutoTrigger == 1)
                    {
                        cmd_buf[0] = cmd_skip_fifodata_end;
                        WriteBufToFpga(fpga_fd, cmd_buf, 1, 0);
						PRINTFLOG("cmd_skip_fifodata_end down!\n");
                    }
                    
                    memprintf("[CPU:%6.2f\%]Cutting Tape[cmd_FTBlockStart]... pole_index=[%d/%d]\r\n", cpuusage, pole_index, group_counts);
                    pole_index = ((capeindex % pole_count) + pole_count) % pole_count;
					//pole_index = (pole_count-1) - 2;
                }
                
                if (is_follow_speed != 0)
                {
                    pole_index++;
                    if (pole_index % pole_count == 0)
                        pole_index = 0;
                    
                    p4trigger_counts++;
                }
            }
            continue;
        case cmd_FTBlockEnd:
            if ((is_work() == 1) || ((ArmRunModel == 1) && (isSingleMode == 0)))//工作状态，做循环极耳处理
            {
			
                if(cmd_buf[1] == 1) //积耳数据块
                {
					
                    offline_buf_pos = block_pos[1];
                    
                    if (is_follow_speed != 0)
                        group_counts++;



					if ((isAutoTrigger == 0) && (utfpgaversion >= 1) && is_follow_speed != 0)
					{
						cmd_buf[0]  = cmd_FTBlockEnd;
						cmd_buf[1]  = 1;
						WriteBufToFpga(fpga_fd, cmd_buf, 2, 0);
						memprintf("grout info:[1]\r\n");
						fpga_group_start = 1;
					}

					/*sprintf(pMsgBuff,"ArmRunModel:(%d),isSingleMode:(%d),isAutoTrigger:(%d),utfpgaversion:(%d),is_follow_speed:(%d)\r\n",ArmRunModel,isSingleMode,isAutoTrigger,utfpgaversion,is_follow_speed);
					PRINTFLOG(pMsgBuff);*/
					
                    
                    // 自触发情况下，启用起始边功能时下发极耳组数据结束标识 2017.11.20
                    //if ((isAutoTrigger == 1) && (isStartEdge == 1))
                    //    break;
					// 自触发情况下，不启用起始边功能时也下发极耳组数据结束标识 2018.11.9
					if (isAutoTrigger == 1)
						break;
                }				
            }
            else if ((isAutoTrigger == 1) && (isOnlineModelFlag == 1) && (ArmRunModel == 0))
            {
                isOnlineModelFlag = 0;
                cmd_buf[0]        = cmd_FRunModel;
                cmd_buf[1]        = 0;
                WriteBufToFpga(fpga_fd, cmd_buf, 2, 0);
            }
            continue;   // 继续循环
        case cmd_FTWorkSpeed:
            memcpy(&work_speed, cmd_buf+1, sizeof(work_speed));
            continue;
        case cmd_FTAxisDia:
            memcpy(&axis_dia, cmd_buf+1, sizeof(axis_dia));
            continue;
        case cmd_FSubPulse:
            memcpy(&sub_pulse, cmd_buf+1, sizeof(sub_pulse));
            //计算10微秒脉冲数
            _pulse10us = (work_speed/((axis_dia/1000.0)*PAI))*sub_pulse/100000.0;
            cmd_buf[0] = cmd_set_encoder_level;
            cmd_buf[1] = _pulse10us;
//          cmd_buf[1] = 7;
            WriteBufToFpga(fpga_fd, cmd_buf, 2, 0);
            continue;
        case cmd_movescanner:
            break;
            
        case cmd_setfq:
            break;
        case cmd_setpower:
            break;
        case cmd_setredlight:
            break;
        case cmd_setshutter:
            break;
        case cmd_setio_output:
            break;
        case cmd_setmindelay:
            break;
        case cmd_setsubdelay:
            break;
        case cmd_setlasertype:
            memcpy(&mathine_type, cmd_buf+1, sizeof(mathine_type));
            break;
        case cmd_setiooutputtype:
            break;
        case cmd_setstartmarkmode:
            break;
        
        // suport SPI laser 2017.08.01
        case cmd_spiwave:
            break;
        case cmd_setspicomm:
            break;
        case cmd_setbrightmode:
            break;
        
        // suport auto trigger 2017.08.16
        case cmd_FPoleWidthByNo:
            break;
        case cmd_FSetAsix:
            break;
        case cmd_FPoleOffsetY:
            break;
        
        // suport cut tape 2017.09.18
        case cmd_FCutTape1:
            break;
        case cmd_FCutTape2:
            break;
        case cmd_FFeedSpace:
            break;
        // support start edge 2017.11.20
        case cmd_startedge_model:
            if (cmd_buf[1] != 0)
                isStartEdge = 1;
            else
                isStartEdge = 0;
            break;
        case cmd_startedge_distance:
            break;
        
        // 如下是指令长度为零的情况
        case cmd_laseron:
            break;
        case cmd_laseroff:
            break;
        case cmd_setmarkstart:
            break;
        case cmd_setmarkstop:
            break;
        case cmd_cut_stop_point:
            //printf("cut_stop_point\n");
            break;
        case cmd_getio:
            break;
        case cmd_getlaser_status:
            break;
        case cmd_getsys_status:
            break;
        case cmd_getshutter_status:
            break;
        default:
            break;
        }
        
        WriteBufToFpga(fpga_fd, cmd_buf, cmd_len+1, 0);
    }
}

static int fpga_read_input(int fpga_fd)
{
    int     input;
    int     rc;
    
    rc = ioctl(fpga_fd, FPGA_DEV_IOC_READ_ENCODER+1, &input);
    if (rc < 0)
    {
        perror("fpga_read_input read address failed");
    }
    
    return input;
}

void fpga_read_tape(int fpga_fd)
{
	int     input;
	int     rc;
	static int CutLineTrigcounts = 0;

	rc = ioctl(fpga_fd, FPGA_DEV_IOC_READ_ENCODER+2, &input);
	if (rc < 0)
	{
		perror("fpga_read_input read address failed");
	}
	

	//3号端口保持高电平, 下降沿触发切胶带
	if ((((input&0x80) == 0x80) && (skiptapeswitch == 0)) ||
		(((input&0x80) != 0x80) && (skiptapeswitch == 1)))
	{
		if (CutLineTrigcounts >= 3)
		{
			p3trigger_counts++;
			MarkPlus = 1;
		}

		CutLineTrigcounts = 0;
	}
	else
		CutLineTrigcounts++;


	if ((io_input&0x20) == 0x20)
		MarkPlus = 0;
}

void thread_fpga_getspeed(int fpga_fd)
{
    while(1)
    {
        struct timespec ts;
        ts.tv_sec = 0;
        ts.tv_nsec = 1000000;
        if(nanosleep(&ts, NULL) != 0)
            perror("");
        
        io_input = fpga_read_input(fpga_fd);
        
        // 配合松山湖现场问题，增加切胶带开关 weimoping 2017.12.05
        if (isskiptapeflag == 1)
		{
			if (isAutoTrigger == 1)
			{
				is_skiptape();
			}
            else
			{
				is_skiptape();
				//fpga_read_tape(fpga_fd);
			}
		}
        
        // 统计0/2/3/4/5号端口的物理触发统计
        is_triggerstat();
    }
}

void thread_fpga_getspeed_create(int fpga_fd)
{
    pthread_t thread_id = 0;
    pthread_attr_t attr;
    
    if(thread_id)
    {
        pthread_cancel(thread_id);
    }
    
    pthread_attr_init(&attr);
    pthread_attr_setstacksize(&attr, THREADSTACKSIZE/8);
    
    if (pthread_create(&thread_id, &attr, (void*)&thread_fpga_getspeed, (void*)fpga_fd))
    {
        PRINTFLOG("ERROR: can't create thread_fpga_getspeed thread!\n");
    }
    else
    {
        PRINTFLOG("thread_fpga_getspeed started!\n");
        pthread_detach(thread_id);
    }
}

unsigned long long fpga_read_testdistance(int fpga_fd)
{
	int encoder, rc=0;
    long long llencoder;
    unsigned long long result=0;
    
	rc = ioctl(fpga_fd, FPGA_DEV_IOC_READ_ENCODER+18, &encoder);
	if(rc < 0)
		perror("read address failed");
    
    result += encoder&0x00ff;
	rc = ioctl(fpga_fd, FPGA_DEV_IOC_READ_ENCODER+17, &encoder);
	if(rc < 0)
		perror("read address failed");
    
    result += ((encoder&0x00ff) << 8);
	rc = ioctl(fpga_fd, FPGA_DEV_IOC_READ_ENCODER+16, &encoder);
	if(rc < 0)
		perror("read address failed");
    
    result += ((encoder&0x00ff) << 16);
	rc = ioctl(fpga_fd, FPGA_DEV_IOC_READ_ENCODER+15, &encoder);
	if(rc < 0)
		perror("read address failed");
    
    result += ((encoder&0x00ff) << 24);
	rc = ioctl(fpga_fd, FPGA_DEV_IOC_READ_ENCODER+14, &encoder);
	if(rc < 0)
		perror("read address failed");
    
    llencoder = encoder;
    result += ((llencoder&0x00ff) << 32);
	return result;
}

unsigned long long fpga_read_encoderdistance(int fpga_fd)
{
	int encoder, rc=0;
    long long llencoder;
    unsigned long long result=0;
    
	rc = ioctl(fpga_fd, FPGA_DEV_IOC_READ_ENCODER+13, &encoder);
	if(rc < 0)
		perror("read address failed");
    
    result += encoder&0x00ff;
	rc = ioctl(fpga_fd, FPGA_DEV_IOC_READ_ENCODER+12, &encoder);
	if(rc < 0)
		perror("read address failed");
    
    result += ((encoder&0x00ff) << 8);
	rc = ioctl(fpga_fd, FPGA_DEV_IOC_READ_ENCODER+11, &encoder);
	if(rc < 0)
		perror("read address failed");
    
    result += ((encoder&0x00ff) << 16);
	rc = ioctl(fpga_fd, FPGA_DEV_IOC_READ_ENCODER+10, &encoder);
	if(rc < 0)
		perror("read address failed");
    
    result += ((encoder&0x00ff) << 24);
	rc = ioctl(fpga_fd, FPGA_DEV_IOC_READ_ENCODER+9, &encoder);
	if(rc < 0)
		perror("read address failed");
    
    llencoder = encoder;
    result += ((llencoder&0x00ff) << 32);
	return result;
}

// 非自触发模式编码器位置数据读取
unsigned long long fpga_read_utencoderdistance(int fpga_fd)
{
	int encoder, rc=0;
    long long llencoder;
    unsigned long long result=0;
    
	rc = ioctl(fpga_fd, FPGA_DEV_IOC_READ_ENCODER+12, &encoder);
	if(rc < 0)
		perror("read address failed");
    
    result += encoder&0x00ff;
	rc = ioctl(fpga_fd, FPGA_DEV_IOC_READ_ENCODER+11, &encoder);
	if(rc < 0)
		perror("read address failed");
    
    result += ((encoder&0x00ff) << 8);
	rc = ioctl(fpga_fd, FPGA_DEV_IOC_READ_ENCODER+10, &encoder);
	if(rc < 0)
		perror("read address failed");
    
    result += ((encoder&0x00ff) << 16);
	rc = ioctl(fpga_fd, FPGA_DEV_IOC_READ_ENCODER+9, &encoder);
	if(rc < 0)
		perror("read address failed");
    
    result += ((encoder&0x00ff) << 24);
	rc = ioctl(fpga_fd, FPGA_DEV_IOC_READ_ENCODER+8, &encoder);
	if(rc < 0)
		perror("read address failed");
    
    llencoder = encoder;
    result += ((llencoder&0x00ff) << 32);
	return result;
}

int fpga_read_encoderspeed(int fpga_fd)
{
	int encoder, encoder1;
	unsigned int rc=0;
    
	rc = ioctl(fpga_fd, FPGA_DEV_IOC_READ_ENCODER, &encoder);
	if(rc < 0)
		perror("read address failed");
    
	encoder &= 0x7fff;
	rc = ioctl(fpga_fd, FPGA_DEV_IOC_READ_ENCODER+3, &encoder1);
	if(rc < 0)
		perror("read address failed");
    
	return (encoder<<8) + encoder1;
}

unsigned int fpga_read_p4trigger(int fpga_fd)
{
	int encoder, rc=0;
    unsigned int result=0;
    
	rc = ioctl(fpga_fd, FPGA_DEV_IOC_READ_ENCODER+7, &encoder);
	if(rc < 0)
		perror("read address failed");
    
    result += encoder&0x00ff;
	rc = ioctl(fpga_fd, FPGA_DEV_IOC_READ_ENCODER+6, &encoder);
	if(rc < 0)
		perror("read address failed");
    
    result += ((encoder&0x00ff) << 8);
	rc = ioctl(fpga_fd, FPGA_DEV_IOC_READ_ENCODER+5, &encoder);
	if(rc < 0)
		perror("read address failed");
    
    result += ((encoder&0x00ff) << 16);
	rc = ioctl(fpga_fd, FPGA_DEV_IOC_READ_ENCODER+4, &encoder);
	if(rc < 0)
		perror("read address failed");
    
    result += ((encoder&0x00ff) << 24);
	return result;
}

void fpga_read_version(int fpga_fd, char *pVersion)
{
	int rc, encoder, encoder1;
    int year, month, day;
    int md[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    
    //if (pVersion == NULL)
    //    return;
    if (isAutoTrigger == 1)
    {
		rc = ioctl(fpga_fd, FPGA_DEV_IOC_READ_ENCODER+7, &encoder);
		if(rc < 0)
			perror("read address failed");

		rc = ioctl(fpga_fd, FPGA_DEV_IOC_READ_ENCODER+8, &encoder1);
		if(rc < 0)
			perror("read address failed");
    }
	else
	{
		rc = ioctl(fpga_fd, FPGA_DEV_IOC_READ_ENCODER+13, &encoder);
		if(rc < 0)
			perror("read address failed");

		rc = ioctl(fpga_fd, FPGA_DEV_IOC_READ_ENCODER+14, &encoder1);
		if(rc < 0)
			perror("read address failed");
	}
    
    //year  = (((encoder&0x00f0)>>4)+2010);
    //month = encoder&0x000f;
    //day   = encoder1&0x00ff;
    //
    //// 闰年调整
    //md[1] = ((((year%4 == 0) && (year%100 != 0)) || (year%400 == 0)) ? 29 : 28);
    //if ((year >= 2017 && year <= 2020) && (month > 0 && month < 13) && (day>0 && day <= md[month - 1]))
    //    sprintf(pVersion, "[FPGA Version:%d-%d-%d]", year, month, day);
    //else
    //    sprintf(pVersion, INVALID_FPGA_VERSION);
	
	year  = ((encoder&0x00f0)>>4);
	month = encoder&0x000f;
	day   = encoder1&0x00ff;
    sprintf(pVersion, "[FPGA Version:%d.%d.%d]", year, month, day);

	return;
}

unsigned char fpga_read_data(int fpga_fd, int offset)
{
	int rc, encoder;
    unsigned char result=0;
    
	rc = ioctl(fpga_fd, FPGA_DEV_IOC_READ_ENCODER+offset, &encoder);
	if(rc < 0)
		perror("read address failed");
    
    result = encoder&0x00ff;
    
	return result;
}

unsigned int fpga_read_filmtrigger(int fpga_fd)
{
	int rc, encoder;
    unsigned int result=0;
    
	rc = ioctl(fpga_fd, FPGA_DEV_IOC_READ_ENCODER+6, &encoder);
	if(rc < 0)
		perror("read address failed");
    
    result += encoder&0x00ff;
	rc = ioctl(fpga_fd, FPGA_DEV_IOC_READ_ENCODER+5, &encoder);
	if(rc < 0)
		perror("read address failed");
    
    result += ((encoder&0x00ff) << 8);
	rc = ioctl(fpga_fd, FPGA_DEV_IOC_READ_ENCODER+4, &encoder);
	if(rc < 0)
		perror("read address failed");
    
    result += ((encoder&0x00ff) << 16);
	rc = ioctl(fpga_fd, FPGA_DEV_IOC_READ_ENCODER+3, &encoder);
	if(rc < 0)
		perror("read address failed");
    
    result += ((encoder&0x00ff) << 24);
	return result;
}

unsigned char fpga_read_cuttapetrigger(int fpga_fd)
{
	int rc, encoder;
    unsigned char result=0;
    
	rc = ioctl(fpga_fd, FPGA_DEV_IOC_READ_ENCODER, &encoder);
	if(rc < 0)
		perror("read address failed");
    
    result = encoder&0x00ff;
	return result;
}

void get_cpuusage(void)
{
    FILE *fp;
    char buf[128];
    char cpu[5];
    long int user, nice, sys, idle, iowait, irq, softirq;
    long int all1, all2, idle1, idle2;
    
    fp = fopen("/proc/stat", "r");
    if(fp == NULL)
    {
        PRINTFLOG("[get_cpuusage]fopen error.\n");
        return;
    }
    
    fgets(buf, sizeof(buf), fp);
    sscanf(buf, "%s%ld%ld%ld%ld%ld%ld%ld", cpu, &user, &nice, &sys, &idle, &iowait, &irq, &softirq);
    
    all1 = user + nice + sys + idle + iowait + irq + softirq;
    idle1 = idle;
    rewind(fp);
    
    /*第二次取数据*/
    sleep(CK_TIME);
    memset(buf, 0, sizeof(buf));
    cpu[0] = '\0';
    user=nice=sys=idle=iowait=irq=softirq=0;
    
    fgets(buf, sizeof(buf), fp);
    sscanf(buf, "%s%ld%ld%ld%ld%ld%ld%ld", cpu, &user, &nice, &sys, &idle, &iowait, &irq, &softirq);
    all2 = user + nice + sys + idle + iowait + irq + softirq;
    idle2 = idle;
    
    cpuusage = (float)(all2 - all1 - (idle2 - idle1))/(all2 - all1)*100;
    fclose(fp);
}

void logfile_proc(void)
{
    FILE *logfile_fp = NULL;
    int file_size=0, lencspeed;
    char strBuffer[MAX_LENGTH];
    char strFPGAVer[64];
    unsigned long long ulllencdis, ulltestdis;
    unsigned char ucP3Trigger;
    unsigned int  uiP4Trigger, uiCounts=0;
    
    if (InitLoopMem(&gLoopMemLog, LOOPMEM_SIZE) == -1)
    {
        printf("gLoopMemLog malloc error\n");
        sleep(1);
        
        exit(0);
    }
    
    while (1)
    {
        if (sem_trywait(&logfile_lock) == 0)
        {
            logfile_fp = fopen(RUNLOG_FILE, "r");
            if (logfile_fp == NULL)
            {
                sem_post(&logfile_lock);
                sleep(1);
                
                continue;
            }
            
            fseek(logfile_fp, 0, SEEK_END);
            file_size = ftell(logfile_fp);
            fclose(logfile_fp);
            
            //if logfile size large than 2M, then backup it.
            if (file_size >= LOGFILE_SIZE)
            {
                rename(RUNLOG_FILE, RUNLOGBAK_FILE);
                file_exist = 0;
            }
            
            sem_post(&logfile_lock);
        }
        
        // 如果FPGA版本未定状态，获取版本信息
        if (isAutoTrigger == -1)
        {
            fpga_read_version(fpga_fd, strFPGAVer);
            if (strstr(strFPGAVer, INVALID_FPGA_VERSION) != NULL)
                isAutoTrigger = 0;
            else
                isAutoTrigger = 1;
        }
        
        // 获取CPU占用率数据
        get_cpuusage();
        
        uiCounts++;
        if (uiCounts%60 == 59)
        {
            if (isAutoTrigger == 1)
            {
                ulllencdis  = fpga_read_encoderdistance(fpga_fd);
                ulltestdis  = fpga_read_testdistance(fpga_fd);
                ucP3Trigger = fpga_read_cuttapetrigger(fpga_fd);
                uiP4Trigger = fpga_read_filmtrigger(fpga_fd);
                sprintf(strBuffer, "[ENC1]:%llu, [ENC2]:%llu; [PHY]P0:%u, P2:%u, P3:%u, P4:%u, P5:%u; [ARM]P3:%u, P4:%u; [FPGA]P3:%u, P4:%u.\r\n", 
                    ulllencdis, ulltestdis, pp0trigger_counts, pp2trigger_counts, pp3trigger_counts, pp4trigger_counts, pp5trigger_counts, p3trigger_counts, p4trigger_counts, ucP3Trigger, uiP4Trigger);
            }
            else
            {
                lencspeed   = fpga_read_encoderspeed(fpga_fd);
                if (utfpgaversion >= 1)
                {
                    ulllencdis  = fpga_read_utencoderdistance(fpga_fd);
                    uiP4Trigger = fpga_read_p4trigger(fpga_fd);
                    sprintf(strBuffer, "[ENC1]:%u, [ENC2]:%llu; [PHY]P2:%u, P3:%u, P4:%u, P5:%u; [ARM]P3:%u, P4:%u; [FPGA]P4:%u.\r\n", 
                        lencspeed, ulllencdis, pp2trigger_counts, pp3trigger_counts, pp4trigger_counts, pp5trigger_counts, p3trigger_counts, p4trigger_counts, uiP4Trigger);
                }
                else
                {
                    sprintf(strBuffer, "[ENC]:%u; [PHY]P2:%u, P3:%u, P4:%u, P5:%u; [ARM]P3:%u, P4:%u.\r\n", 
                        lencspeed, pp2trigger_counts, pp3trigger_counts, pp4trigger_counts, pp5trigger_counts, p3trigger_counts, p4trigger_counts);
                }
            }
            PRINTFLOG(strBuffer);
        }
    }
}

int func_getcd32pos(void)
{
    FILE *offline_file_fp = NULL;
    int  file_size       = 0;
    char *offline_buf_p  = 0;
    
    int  cmd;
    int  cmd_len;
    char cmd_buf[5];
    int  buf_pos = 0;
    int  ipole_count  = 1;      // 极耳个数
    int  bspole_index = 0;      // 极耳位置序号数
    int  ibeginindex  = 0;      // 起切序号
	int  icapeindex   = 0;		// 过胶带序号
    
    if(sem_trywait(&file_lock) != 0)
        return -EAGAIN;
    
    offline_file_fp = fopen(OFFLINE_FILE, "r");
    if(offline_file_fp == NULL)     // 打开文件失败，释放信号量，重新尝试
    {
        perror("open file :");
        sem_post(&file_lock);
        sleep(1);
        
        return -EAGAIN;
    }
    
    fseek(offline_file_fp, 0, SEEK_END);
    file_size = ftell(offline_file_fp);
    offline_buf_p = malloc(file_size);
    if(offline_buf_p == NULL)
    {
        PRINTFLOG("[Core Error!!!]malloc error\n");
        sem_post(&file_lock);
        sleep(1);
        
        return -EAGAIN;
    }
    
    fseek(offline_file_fp, 0, SEEK_SET);
    fread(offline_buf_p, 1, file_size, offline_file_fp);
    fclose(offline_file_fp);
    
    while (buf_pos < file_size)
    {
        cmd = offline_buf_p[buf_pos++];
        // 如果命令字非法，则退出
        if (cmd >= cmd_TABLEEND)
        {
            free(offline_buf_p);
            offline_file_fp = NULL;
            sem_post(&file_lock);
            
            return -EAGAIN;
        }
        
        cmd_len = cmd_len_table[cmd];
        if(cmd_len)
        {
            if ((cmd_FTPoleCounts == cmd) || (cmd_FTBlockStart == cmd) || (cmd_FTBlockEnd == cmd))
                memcpy(cmd_buf+1, &offline_buf_p[buf_pos], cmd_len);
            
            buf_pos += cmd_len;
        }
        
        switch(cmd)
        {
        case cmd_FTPoleCounts:
            memcpy(&ipole_count, cmd_buf+1, sizeof(ipole_count));
            ibeginindex = ((beginindex % ipole_count) + ipole_count) % ipole_count;
			icapeindex = ((capeindex % ipole_count) + ipole_count) % ipole_count;
            continue;
            
        case cmd_FTBlockStart:
            if (cmd_buf[1] == 1)
            {
                bspole_index = 0;
            }
            else if (cmd_buf[1] == 0x0B)    //积耳数据块
            {
                if ((beginindex_buf_pos == 0) && (bspole_index == ibeginindex))
                    beginindex_buf_pos = buf_pos - 2;
                if ((countdown2_buf_pos == 0) && (bspole_index == icapeindex))//if ((countdown2_buf_pos == 0) && ((bspole_index+2) == ipole_count))
				{
					countdown2_buf_pos = buf_pos - 2;
					//sprintf(pMsgBuff,"countdown2_buf_pos:(%d),bspole_index:(%d)\r\n",countdown2_buf_pos,bspole_index);
					//PRINTFLOG(pMsgBuff);
				}
                
                bspole_index++;
            }
            continue;
            
        case cmd_FTBlockEnd:
            if (cmd_buf[1] == 1)
            {
                // 找出切出线位置
                if (cuttingout_buf_pos == 0)
                    cuttingout_buf_pos = buf_pos;
                
                free(offline_buf_p);
                offline_file_fp = NULL;
                sem_post(&file_lock);
                
                return 0;
            }
            
        default:
            break;
        }
    }
    
    free(offline_buf_p);
    offline_file_fp = NULL;
    sem_post(&file_lock);
    
    return 0;
}
//未使用
void thread_getcd32pos(char *pbuf)
{
    int  cmd;
    int  cmd_len;
    char cmd_buf[5];
    int  buf_pos = 0;
    int  ipole_count  = 1;      // 极耳个数
    int  bspole_index = 0;      // 极耳位置序号数
    int  ibeginindex  = 0;      // 起切序号
    
    if (pbuf == NULL)
    {
        PRINTFLOG("**** thread_getcd2pos : pbuf is NULL.\n");
        return;
    }
    
    while (1)
    {
        cmd = pbuf[buf_pos++];
        cmd_len = cmd_len_table[cmd];
        if(cmd_len)
        {
            if ((cmd_FTPoleCounts == cmd) || (cmd_FTBlockStart == cmd) || (cmd_FTBlockEnd == cmd))
                memcpy(cmd_buf+1, &pbuf[buf_pos], cmd_len);
            
            buf_pos += cmd_len;
        }
        
        switch(cmd)
        {
        case cmd_FTPoleCounts:
            memcpy(&ipole_count, cmd_buf+1, sizeof(ipole_count));
            ibeginindex = ((beginindex % ipole_count) + ipole_count) % ipole_count;
            continue;
            
        case cmd_FTBlockStart:
            if (cmd_buf[1] == 1)
            {
                bspole_index = 0;
            }
            else if (cmd_buf[1] == 0x0B)    //积耳数据块
            {
                if ((beginindex_buf_pos == 0) && (bspole_index == ibeginindex))
                    beginindex_buf_pos = buf_pos - 2;
                if ((countdown2_buf_pos == 0) && ((bspole_index+2) == ipole_count))
                    countdown2_buf_pos = buf_pos - 2;
                
                bspole_index++;
            }
            continue;
            
        case cmd_FTBlockEnd:
            if (cmd_buf[1] == 1)
                return;
            
        default:
            break;
        }
    }
}
//未使用
void thread_getcd32pos_create(char *pbuf)
{
    pthread_t thread_id = 0;
    pthread_attr_t attr;
    
    if(thread_id)
    {
        pthread_cancel(thread_id);
    }
    
    pthread_attr_init(&attr);
    pthread_attr_setstacksize(&attr, THREADSTACKSIZE);
    
    if (pthread_create(&thread_id, &attr, (void *)&thread_getcd32pos, (char *)pbuf))
    {
        PRINTFLOG("ERROR: can't create thread_getcd32pos_create thread!\n");
    }
    else
    {
        PRINTFLOG("thread_getcd32pos_create started!\n");
        pthread_detach(thread_id);
    }
}

//void getARMVersion(char *pstrDate)
//{
//    char cDate[32];
//    char s_month[5];
//    int month, day, year;
//    static const char month_names[] = "JanFebMarAprMayJunJulAugSepOctNovDec";
//    
//    if (pstrDate == NULL)
//        return;
//    
//    strcpy(cDate, __DATE__);
//    sscanf(cDate, "%s %d %d", s_month, &day, &year);
//    
//    month = (strstr(month_names, s_month)-month_names)/3 + 1;
//    sprintf(pstrDate, "[ARM Version:%d-%d-%d]", year, month, day);
//    
//    return;
//}

void getARMVersion(char *pstrDate)
{
	int month, day, year;
	year = 2;
	month = 0;
	day = 1;
	sprintf(pstrDate, "[ARM Version:%d.%d.%d]", year, month, day);
}
void omc_handle(int socket_id)
{
    ssize_t recvLength;
    char pBuffer[MAX_LENGTH];
    char strTime[64], strARMVer[64], strFPGAVer[64];
    int  omc_cmd, length, offset, leftLength, iLength, lencspeed;
    unsigned char ucValue;
    unsigned long long ulllencdis, ulltestdis;
    unsigned char ucP3Trigger;
    unsigned int  uiP4Trigger;
    
    while (1)
    {
        recvLength = read(socket_id, pBuffer, MAX_LENGTH);
        
        // Server Recieve Data Failed!
        //if ((recvLength < 0) && (errno != EINTR))
        if (recvLength <= 0)
            break;
        
        leftLength = recvLength;
        while (leftLength > 0)
        {
            omc_cmd = *((int *)pBuffer);
            leftLength -= sizeof(int);
            
            switch (omc_cmd)
            {
            case omc_reset_index:
                fpga_pole_index   = 0;
                PRINTFLOG("[OMC]omc_reset_index.\n");
                break;
                
            case omc_reset_group:
                fpga_group_counts = 0;
                PRINTFLOG("[OMC]omc_reset_group.\n");
                break;
                
            case omc_set_index:
                fpga_pole_index   = *((int *)pBuffer+1);
                leftLength -= sizeof(int);
                sprintf(pMsgBuff, "[OMC]omc_set_index:%d\n", fpga_pole_index);
                PRINTFLOG(pMsgBuff);
                break;
                
            case omc_set_group:
                fpga_group_counts = *((int *)pBuffer+1);
                leftLength -= sizeof(int);
                if (fpga_group_counts <= 0)
                    fpga_group_counts = 0;
                
                sprintf(pMsgBuff, "[OMC]omc_set_group:%d\n", fpga_group_counts);
                PRINTFLOG(pMsgBuff);
                break;
                
            case omc_query_indexgroup:
                if ((is_work() == 1) && (is_follow_speed != 0))
                {
                    uiP4Trigger       = fpga_read_filmtrigger(fpga_fd) + (((beginindex % pole_count) + pole_count) % pole_count);
                    fpga_pole_index   = uiP4Trigger % pole_count;
                    fpga_group_counts = uiP4Trigger / pole_count;
                }
                
                if (isAutoTrigger == 1)
                    sprintf(pMsgBuff, "pole_index:[%d/%d]\n", fpga_pole_index, fpga_group_counts);
                else
                    sprintf(pMsgBuff, "pole_index:[%d/%d]\n", pole_index, group_counts);
                length = strlen(pMsgBuff);
                
                write(socket_id, pMsgBuff, length);
                break;
                
            case omc_get_loopmemflag:
                (*(int *)pMsgBuff) = loopmem_flag;
                write(socket_id, pMsgBuff, sizeof(int));
                break;
                
            case omc_get_loopmempipe:
                (*(int *)pMsgBuff) = loopmem_pipe;
                write(socket_id, pMsgBuff, sizeof(int));
                break;
                
            case omc_set_loopmemflag:
                loopmem_flag = ((*((int *)pBuffer+1)) ? 1 : 0);
                leftLength -= sizeof(int);
                sprintf(pMsgBuff, "[OMC]omc_set_loopmemflag:%d\n", loopmem_flag);
                PRINTFLOG(pMsgBuff);
                break;
                
            case omc_set_loopmempipe:
                loopmem_pipe = ((*((int *)pBuffer+1)) ? 1 : 0);
                leftLength -= sizeof(int);
                sprintf(pMsgBuff, "[OMC]omc_set_loopmempipe:%d\n", loopmem_pipe);
                PRINTFLOG(pMsgBuff);
                break;
                
            case omc_query_jierdata:
                loopmem_pipe = 1;
                length = gLoopMemLog.m_size - gLoopMemLog.m_free;
                if (length > 0)
                {
                    LoopMemRead(&gLoopMemLog, pMsgBuff, length);
                    write(socket_id, pMsgBuff, length);
                }
                break;
                
            case omc_reset_trigcnts:
                pp0trigger_counts = 0;
                pp2trigger_counts = 0;
                pp3trigger_counts = 0;
                pp4trigger_counts = 0;
                pp5trigger_counts = 0;
                p3trigger_counts  = 0;
                p4trigger_counts  = 0;

				pp0trigger_status = 0;
				pp2trigger_status = 0;
				pp5trigger_status = 0;

                break;
                
            case omc_query_trigcnts:
                if (isAutoTrigger == 1)
                {
                    ulllencdis  = fpga_read_encoderdistance(fpga_fd);
                    ulltestdis  = fpga_read_testdistance(fpga_fd);
                    ucP3Trigger = fpga_read_cuttapetrigger(fpga_fd);
                    uiP4Trigger = fpga_read_filmtrigger(fpga_fd);
                    sprintf(pMsgBuff, "[ENC1]:%llu, [ENC2]:%llu; [PHY]P0:%u(%u), P2:%u(%u), P3:%u(%u), P4:%u(%u), P5:%u(%u); [ARM]P3:%u, P4:%u; [FPGA]P3:%u, P4:%u.\r\n", 
                        ulllencdis, ulltestdis, pp0trigger_counts,pp0trigger_status, pp2trigger_counts,pp2trigger_status, pp3trigger_counts,pp3trigger_status, pp4trigger_counts,pp4trigger_status, pp5trigger_counts,pp5trigger_status, p3trigger_counts, p4trigger_counts, ucP3Trigger, uiP4Trigger);
                }
                else
                {
                    lencspeed   = fpga_read_encoderspeed(fpga_fd);
                    if (utfpgaversion >= 1)
                    {
                        ulllencdis  = fpga_read_utencoderdistance(fpga_fd);
                        uiP4Trigger = fpga_read_p4trigger(fpga_fd);
                        sprintf(pMsgBuff, "[ENC1]:%u, [ENC2]:%llu; [PHY]P2:%u, P3:%u, P4:%u, P5:%u; [ARM]P3:%u, P4:%u; [FPGA]P4:%u.\r\n", 
                            lencspeed, ulllencdis, pp2trigger_status, pp3trigger_counts, pp4trigger_counts, pp5trigger_status, p3trigger_counts, p4trigger_counts, uiP4Trigger);
                    }
                    else
                    {
                        sprintf(pMsgBuff, "[ENC]:%u; [PHY]P2:%u, P3:%u, P4:%u, P5:%u; [ARM]P3:%u, P4:%u.\r\n", 
                            lencspeed, pp2trigger_status, pp3trigger_counts, pp4trigger_counts, pp5trigger_status, p3trigger_counts, p4trigger_counts);
                    }
                }
                write(socket_id, pMsgBuff, strlen(pMsgBuff));
                break;
                
            case omc_set_systime:
                strcpy(strTime, (char *)&pBuffer[4]);
                leftLength = leftLength - strlen(strTime) - 1;
                SetSystemTime(strTime);
                break;
                
            case omc_get_version:
                getARMVersion(strARMVer);
                fpga_read_version(fpga_fd, strFPGAVer);
                sprintf(pMsgBuff, "%s %s [Skip Tape Flag: %d] [Begin Index: %d] [Start Edge Mode: %d]\r\n", strARMVer, strFPGAVer, isskiptapeflag, beginindex, isStartEdge);
                write(socket_id, pMsgBuff, strlen(pMsgBuff));
                break;
                
            case omc_direct_fpgacmd:
                iLength = *(((int *)pBuffer+1));
                leftLength -= (sizeof(int)+iLength);
                write(fpga_fd, (char *)pBuffer+2*sizeof(int), iLength);
                break;
                
            case omc_get_fpgadata:
                offset = *((int *)pBuffer+1);
                leftLength -= sizeof(int);
                ucValue = fpga_read_data(fpga_fd, offset);
                write(socket_id, &ucValue, sizeof(ucValue));
                break;
                
            case omc_set_fpgatriggermode:
                isAutoTrigger = ((*((int *)pBuffer+1)) ? 1 : 0);
                leftLength -= sizeof(int);
                break;
                
            case omc_set_startedgemode:
                isStartEdge = ((*((int *)pBuffer+1)) ? 1 : 0);
                leftLength -= sizeof(int);
                break;
			case omc_get_1:	//极耳间距
				break;
			case omc_get_2:	//起始边到激光的距离
				break;
			case omc_get_3:	//起始边使能方式
				break;
			case omc_get_4:	//急停后切直线距离
				break;
			case omc_get_5:	//过胶带功能启用状态
				break;
			case omc_get_6:	//胶带感应器到激光距离
				break;
			case omc_get_7:	//过胶带后切直线距离
				break;              
            default:
                close(socket_id);
                PRINTFLOG("[OMC]invalid command, client exit!\n");
                return;
            }
            
            usleep(100);
        }
        
        usleep(1000);
    }
    
    // close client socket.
    close(socket_id);
    PRINTFLOG("[OMC]client exit!\n");
}
