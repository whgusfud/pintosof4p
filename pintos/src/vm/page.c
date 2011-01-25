//      page.c
//      
//      Copyright 2011 mayli <mayli.he@gmail.com>
//   
#include "vm/page.h"
#include "threads/thread.h"
struct list_elem *page_find (void *upage){
  struct thread* cur=thread_current();
  struct spt_elem* spte;
  struct list_elem *e;
  /* L: load page according to the spt, if not in spt, do nothing */
  for (e = list_begin (&cur->spt); e != list_end (&cur->spt);
         e = list_next (e))
      {
        spte = (struct spt_elem *)list_entry (e, struct spt_elem, elem);
        if(upage==spte->upage){
          return e;
        }
      }
  /* l: not found */
  return 0;
}
