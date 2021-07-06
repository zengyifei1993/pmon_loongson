#ifndef __GRUB_LIKE_CMD_INITRD__
#define __GRUB_LIKE_CMD_INITRD__
#include "pmon/loaders/loadfn.h"

int initrd_execed(void);
unsigned long long get_initrd_start(void);
unsigned int get_initrd_size(void);
int boot_initrd(const char* path, long long rdstart,int flags);
#ifndef INITRD_ADDR
#define INITRD_ADDR (int)0x84000000
#endif
#endif
