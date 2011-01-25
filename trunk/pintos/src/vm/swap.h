//      swap.h
//      
//      Copyright 2011 sneakerkg <xiaotj1990327@gmail.com>
//      
#ifndef VM_SWAP_H
#define VM_SWAP_H

#include "vm/frame.h"
#include "vm/page.h"

#include <list.h>
#include "threads/palloc.h"
#include "threads/thread.h"
#include "devices/block.h"
#include "userprog/pagedir.h"

struct slot{
	//[X]链表项
	struct list_elem elem;
	//[X]虚存地址
	uint8_t *upage;
	//[X]全局的swap分区防止弄混地址空间
	tid_t pid; 
	//[X]起始扇区号除以4
	block_sector_t slid;
};

//[X]初始化
void  swap_init(void);
//[X]替换
void* evict(void *upage,struct thread* newown);
//[X]写回
bool readback(void* upage,void* fp);
//[X]查找
struct list_elem *swap_find (void *upage);
#endif /* vm/swap.h */
