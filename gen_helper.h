#ifndef GEN_HELPER_H
#define GEN_HELPER_H

typedef struct _LoopMem
{
    char *m_head;
    int  m_size;
    int  m_free;
    int  m_read;
    int  m_write;
    int  m_cycletimes;
} LoopMem;

int GetProfileString(char *profile, char *AppName, char *KeyName, char *KeyVal);
int SetSystemTime(char *dt);

int InitLoopMem(LoopMem *pLoopMem, int lsize);
int LoopMemRead(LoopMem *pLoopMem, char *pBuff, int lsize);
int LoopMemWrite(LoopMem *pLoopMem, char *pBuff, int lsize);

#endif

