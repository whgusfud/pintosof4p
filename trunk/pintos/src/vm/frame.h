//      frame.h
//      
//      Copyright 2011 mayli <mayli.he@gmail.com>
//      
#ifndef VM_FRAME_H
#define VM_FRAME_H

#include <list.h>
#include "threads/thread.h"

/* L: a basic frame elem, need more vars. */
struct frame{
  /* physics address */
  void* paddr;
  /* current working @some user page(virtual addr) */
  void* upage;
  /* the owner thread */
  struct thread* owner;
  /* protect bytes 
   * char flag;*/
  //[X]记录最近使用装况 
  int recent;
  
  struct list_elem elem;
  };

/* L: a list init, should be called at system start */
void frame_table_init(void);
/* L: etc */
bool frame_table_full (void);
/* L: frame_get must be called with an upage */
void* frame_get (bool,void* upage);
void frame_free (void *);
struct frame* frame_find (void *);
struct frame* LRU(void);
void changerec(void);
#endif /* vm/frame.h */
