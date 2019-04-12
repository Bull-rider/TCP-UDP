#ifndef PARSE_STREAM_H
#define PARSE_STREAM_H

#include <semaphore.h>

#define RUNLOG_FILE     "/data/log.dat"
#define MEMLOG_FILE     "/data/memlog.dat"
#define RUNLOGBAK_FILE  "/data/log_bak.dat"

// 经过测试，调整接收缓冲区大小为7K为最佳
#define MAX_LENGTH      1024
//#define MAX_LENGTH      7168

#define THREADSTACKSIZE 4194304

#define PRINTFLOG(a)            write_log(0, (a)); printf("%s", (a))
#define PRINTNEWLINELOG(a)      write_log(1, (a)); printf("%s", (a))

extern int fpga_fd;
extern sem_t logfile_lock;
extern LoopMem gLoopMemLog;
extern int isskiptapeflag;
extern int skiptapeswitch;
extern int beginindex;
extern int capeindex;
extern int utfpgaversion;
extern int countdown2_buf_pos;
extern int beginindex_buf_pos;
extern int cuttingout_buf_pos;
extern int isAutoTrigger;

void cmd_len_table_init(void);
int  parse_stream(char *buf, int len, int *filelen);
int  func_getcd32pos(void);
void parse_file(int fpga_fd);
void parse_init(int fpga_fd);

void write_log(int bNewline, char *buf);

void logfile_proc(void);
void omc_handle(int socket_id);

#endif
