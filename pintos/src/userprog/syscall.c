#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"

static void syscall_handler (struct intr_frame *);

void
syscall_init (void) 
{
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
}

static void
syscall_handler (struct intr_frame *f) 
{
  uint32_t *p=f->esp;
  /* see syscall-nr.h */
  switch(*p){
    case SYS_EXIT:
    {
      /* this value might be used by WS, this is the'return code' */
      int status = *(p + 1);
      thread_exit ();
    }
    
    case SYS_WRITE: 
      {
        int fd = *(p + 1);
        char *buffer = (char *)*(p + 2);
        unsigned size = *(p + 3);
        if(fd==STDOUT_FILENO)
        /* STDOUT_FILE is 1, defined in lib/stdio.h */
        {
          /* putbuf is in lib/kernel/console */
          putbuf (buffer, size);
        }
        break;
        }
    default:{
      printf("Unhandled SYSCALL(%d)\n",*p);
      }
    }
}

