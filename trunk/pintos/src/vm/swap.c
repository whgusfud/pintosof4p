//      swap.c
//      
//      Copyright 2011 sneakerkg <xiaotj1990327@gmail.com>
//      
#include "vm/frame.h"
#include "vm/swap.h"
#include "vm/page.h"

#include <list.h>
#include <stdio.h>
#include "threads/palloc.h"
#include "threads/thread.h"
#include "devices/block.h"
#include "userprog/pagedir.h"
#include "threads/vaddr.h"

static bool used[8192];
//[X]初始化
/* X: try to solve some sync problems */
static struct lock block_lock;


void swap_init(void)
{
	int i;
	for(i=0;i<8192;i++)
		used[i]=false;
    lock_init(&block_lock);
}

struct list_elem *swap_find (void *upage){
  struct thread* cur=thread_current();
  struct slot* sp;
  struct list_elem *e;
  lock_acquire(&cur->swap_list_lock);
  /* X: load page according to the spt, if not in spt, do nothing */
  for (e = list_begin (&cur->swapt); e != list_end (&cur->swapt);
         e = list_next (e))
      {
        sp = (struct slot *)list_entry (e, struct slot, elem);
        if(upage==sp->upage){
		  lock_release (&cur->swap_list_lock);
          return e;
        }
      }
  /* X: not found */
  lock_release (&cur->swap_list_lock);
  return 0;
}


//[X]替换
void* evict(void *upage,struct thread* newown)
{
	//[X]选择要替换的页面
	lock_acquire(&block_lock);
	struct frame* fp=LRU();
	struct slot* sp;
	struct block *swapb=block_get_role(BLOCK_SWAP);
    struct thread* owner=fp->owner;
    struct list_elem *e;
	void* buf=fp->paddr;
	struct spt_elem* spte;
	//[X]设置slot表项	
	int i,j;
	block_sector_t tar;
	for(i=0;i<8192;i=i+8)
	{
		if(used[i]==false)
		{
			tar=i;
			used[i]=true;
			break;
		}
	}	
	sp=(struct slot *) malloc (sizeof(struct slot));
	sp->pid=owner->tid;
	sp->slid=tar/8;
	sp->upage=fp->upage;
	list_push_back(&thread_current()->swapt, &sp->elem);
	
	lock_release (&block_lock);
	//[X]解除原有映射
	pagedir_clear_page (owner->pagedir, pg_round_down(sp->upage));
	//[X]删除原有spt表项
	lock_acquire(&owner->spt_list_lock);
    for (e = list_begin (&owner->spt); e != list_end (&owner->spt);
           e = list_next (e))
    {
		spte = (struct spt_elem *)list_entry (e, struct spt_elem, elem);
		if(spte->upage==fp->upage)
		{
				list_remove(e);
				break;
		}	   
	}
	lock_release(&owner->spt_list_lock);
	//[X]将该页面的原始内容写入swap分区
	for(i=0;i<8;i++)
	{
		block_write (swapb,tar,buf);
		used[tar]=true;
		tar++;
		buf=buf+512;
	}	
	fp->upage=upage;
	fp->owner=newown;
	fp->recent=0;
	return fp->paddr;	
}

bool readback(void* upage,void* fp){
	struct thread* cur=thread_current();
	struct slot* sp;
	struct block *swapb=block_get_role(BLOCK_SWAP);
	struct list_elem *e;
	//struct frame* fp;
	block_sector_t tar;
	void* buf;
	int i;
	lock_acquire(&cur->swap_list_lock);
	for (e = list_begin (&cur->swapt); e != list_end (&cur->swapt);
         e = list_next (e))
	{
		sp=list_entry (e, struct slot, elem);
		if(sp->pid==cur->tid&&sp->upage==upage)
		{			
				tar=sp->slid*8;
				buf=fp;
				//[X]写回
				for(i=0;i<8;i++)
				{
					block_read (swapb,tar,buf);
					//hex_dump(buf,buf,100,1);
					used[tar]=false;
					tar++;
					buf=buf+512;
				}
				//[X]启用映射
				pagedir_set_page (cur->pagedir,upage,fp,true);	
				//[X]删除表项
				list_remove(sp);
				lock_release (&cur->swap_list_lock);
				return true;
		}
	}
	lock_release (&cur->swap_list_lock);
	return false;	
}
	
