//      page.h
//      
//      Copyright 2011 mayli <mayli.he@gmail.com>,sneakerkg<xiaotj1990327@gmail.com>
//      
#ifndef VM_PAGE_H
#define VM_PAGE_H

#include <list.h>
#include "filesys/file.h"
#include "vm/swap.h"
#include "vm/frame.h"

/* L: a basic page elem, more vars is needed */
struct spt_elem{
  struct thread* owner;
  /* L: the upage this entry is descrepting */
  uint8_t *upage;
  /* L: we can get a file ptr directly in load_seg, the following
   * keeps the file information */
  struct file *fileptr;
  off_t ofs;
  uint32_t read_bytes;
  uint32_t zero_bytes;
  bool writable;
  
  void *kpage;
  
  //[X]用来记录文件映射的信息
  bool needclose;
  bool needremove;
  int mapid;
  
  /* a swap slot */
  
  /* a list elem */
  struct list_elem elem;
};

/* L: look up the spt to find a entry or return null */
struct list_elem *page_find (void *upage);
#endif /* vm/page.h */
