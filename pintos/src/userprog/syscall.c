#include "userprog/syscall.h"
#include <stdio.h>
#include "threads/vaddr.h"
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "userprog/pagedir.h"

static void syscall_handler (struct intr_frame *);
extern uint32_t* lookup_page (uint32_t *pd, const void *vaddr, bool create);

/* Reads a byte at user virtual address UADDR.
UADDR must be below PHYS_BASE.
Returns the byte value if successful, -1 if a segfault
occurred. */

void
syscall_init (void) 
{
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
}

static void
syscall_handler (struct intr_frame *f) 
{
  uint32_t *p=f->esp;
  //[X]防止读到code段以下的bad
  if(p<=0x08084000-64*1024*1024||pagedir_mapped(thread_current()->pagedir,p)==0||(*p)==NULL||(*p)>=PHYS_BASE)
  {		  
	printf ("%s: exit(%d)\n", thread_name(),-1);
	thread_current()->ret_status=-1;
	thread_exit();
  } 
  /* see syscall-nr.h */
  switch(*p){
    case SYS_HALT:{                   /* Halt the operating system. */
      /*L: we just power-off here, right?
       * no return, just an one-way call */
      shutdown();
    }
    case SYS_EXIT:{                   /* Terminate this process. */
      /*L: here i need a place to store "return code"
       * please add a new member(int) in struct thread
       * still an one-way call, no break is needed */
      thread_current ()->ret_status = *((int*)(p+1));
      if(p+1>=PHYS_BASE)
        thread_current ()->ret_status=-1;
      printf ("%s: exit(%d)\n", thread_name(), thread_current()->ret_status);
      thread_exit ();
    }
    case SYS_EXEC:{                   /* Start another process. */
      /*L: IN: filename in (p+1)
       * OUT : return a pid_t(in eax) or fail 
       * CALL: process_execute()
       * this is a return-needed intr-handler that is a "break;"
       * from this one, almost handler below need a break */
      if(*(p+1)==NULL||*(p+1)>=PHYS_BASE||pagedir_mapped(thread_current()->pagedir,*(p+1))==0)
      {		  
		printf ("%s: exit(%d)\n", thread_name(),-1);
		thread_current()->ret_status=-1;
		thread_exit();
		break;
	  } 
      f->eax = process_execute(*(p+1));
      break;
    }
    case SYS_WAIT:{                   /* Wait for a child process to die. */
      /*L: IN: tid in (p+1)
       * OUT : -1(killed) or child_status
       * CALL: process_wait()
       * process_wait need more code work
       * */
      f->eax = process_wait(*(p+1));
      break;
    }
    case SYS_CREATE:{                 /* Create a file. */
      /*L: IN: filename(p+1), initial_size(p+2)
       * OUT : f->eax = some result;
       * */
      if(*(p+1)==NULL||*(p+1)>=PHYS_BASE||pagedir_mapped(thread_current()->pagedir,*(p+1))==0)
      {		  
		printf ("%s: exit(%d)\n", thread_name(),-1);
		thread_current()->ret_status=-1;
		thread_exit();
		break;
	  } 
      f->eax = filesys_create (*(p+1), *(p+2));
      break;
    }
    case SYS_REMOVE:{                 /* Delete a file. */
      /*L: IN:filename(p+1)
       * OUT :f->eax = some result;
       * */
      f->eax = filesys_remove(*(p+1));
      break;
    }
    case SYS_OPEN:{                   /* Open a file. */
      /*L: IN: filename
       * OUT : Returns the new file* if successful or a null pointer
       * TODO: check the file descreptor */
      /* L:handle the null filename */
      if(*(p+1)==NULL||*(p+1)>=PHYS_BASE||pagedir_mapped(thread_current()->pagedir,*(p+1))==0)
      {		  
		printf ("%s: exit(%d)\n", thread_name(),-1);
		thread_current()->ret_status=-1;
		thread_exit();
		return;
	  }
      struct file *file = filesys_open (*(p+1));
      /*L: check if file is opened successfully or return -1 */
      if (file == NULL){
       f->eax = -1;
       return;
      }
      /*L:the file desc thing */
      struct list_elem *e;
      struct list* fd_list = &thread_current()->fd_list;
      struct file_desc *file_d = (struct file_desc *)malloc(sizeof(struct file_desc));
      
      /*L: 0,1 are stdios,
       * here only one fd is supported, more file need more code work */
      int maxfd;
      
      if(list_empty(fd_list)){
		  maxfd=1;
	  }
	  else{
		  e = list_begin (fd_list);
		  maxfd = list_entry (e, struct file_desc, elem)->fd;
	  }
      file_d->fd = maxfd + 1;
      file_d->file = file;
      list_push_front (fd_list, &file_d->elem);

      f->eax = file_d->fd;
      break; 
    }
    case SYS_FILESIZE:{               /* Obtain a file's size. */
      /*L:IN :filename
       * Out :file_length
       * */
      struct list_elem *e;
      struct file_desc *file_d;
      struct list* fd_list = &thread_current()->fd_list;
      /*L: find the one, and call file_length */
      for (e = list_begin (fd_list); e != list_end (fd_list); 
           e = list_next (e))
        {   
          file_d = list_entry (e, struct file_desc, elem);
          if (file_d->fd == *(p+1))
           {
             f->eax = file_length (file_d->file);
             break;
           }
        }
      break;
    }
    case SYS_READ:{                   /* Read from a file. */
      /*L:IN :filedesc(p+1),buf(p+2),size(p+3)
       * OUT :f->eax=count
       * read from kb, or read from a real file is different */
      int count=0;
      if (*(p+1) == 0)
       {
         while (count < *(p+3))
           {
             char ch = input_getc ();
             /*L:i put an '\0' for '\n', don't know if it is right */
             if (ch == '\n'){
              *((char*)*(p+2)+count)='\0';
              break;
             }
             *((char*)*(p+2)+count)=ch;
             count++;
           }
         f->eax = count;
         return;
       }
      /* a normal file, find the fd */
      if(*(p+2)==NULL||*(p+2)>=PHYS_BASE||pagedir_mapped(thread_current()->pagedir,*(p+2))==0)
      {		  
		printf ("%s: exit(%d)\n", thread_name(),-1);
		thread_current()->ret_status=-1;
		thread_exit();
		break;
	  }
      struct list_elem *e;
      struct file_desc *file_d;
      struct list* fd_list = &thread_current()->fd_list;
      for (e = list_begin (fd_list); e != list_end (fd_list); 
           e = list_next (e))
        {
          file_d = list_entry (e, struct file_desc, elem);
          if (file_d->fd == *(p+1)){ 
             count = file_read (file_d->file, *(p+2), *(p+3));
             f->eax = count;
             break;
           }
        }
      break; 
    }
    case SYS_WRITE:{
	   /* Write to a file. */
      /*L:IN :fd(p+1),buf(p+2),size(p+3)
       * OUT :eax=count 
       * NOTICE the diff between stdout and a normal file */
      if (*(int*)(p+3) <= 0)
      {
        f->eax = 0;
        return;
      }
      if(*(p+1)==STDOUT_FILENO)
      /* STDOUT_FILE is 1, defined in lib/stdio.h */
      {
        /* putbuf is in lib/kernel/console */
        putbuf ((char*)*(p+2), *(int*)(p+3));
        /* putbuf have no return, so eax=size */
        f->eax=*(p+3);
      }
      
      if(*(p+2)==NULL||*(p+2)>=PHYS_BASE||pagedir_mapped(thread_current()->pagedir,*(p+2))==0)
      {		  
		printf ("%s: exit(%d)\n", thread_name(),-1);
		thread_current()->ret_status=-1;
		thread_exit();
		break;
	  }
      /* check fd in fd_list */
      struct list_elem *e;
      struct file_desc *file_d;
      struct list* fd_list = &thread_current()->fd_list;
      for (e = list_begin (fd_list); e != list_end (fd_list); 
           e = list_next (e)){
          file_d = list_entry (e, struct file_desc, elem);
          if (file_d->fd == *(p+1))
           {
             int count = 0;
             count = file_write (file_d->file, *(p+2), *(p+3));
             f->eax = count;
             break;
           }
        }
      break;
    }
    case SYS_SEEK:{                   /* Change position in a file. */
      /*L:IN :fd,pos
       * OUT :none?
       * */
      struct list_elem *e;
      struct file_desc *file_d;
      struct list* fd_list = &thread_current()->fd_list;
      for (e = list_begin (fd_list); e != list_end (fd_list); 
           e = list_next (e)){
          file_d = list_entry (e, struct file_desc, elem);
          if (file_d->fd == *(p+1))
           {
             /* this func returns void */
             file_seek (file_d->file, *(p+2));
             break;
           }
        }
      break;
    }
    case SYS_TELL:{                   /* Report current position in a file. */
      /*L: IN:fd
       * OUT :eax=pos
       * */
      struct list_elem *e;
      struct file_desc *file_d;
      struct list* fd_list = &thread_current()->fd_list;
      for (e = list_begin (fd_list); e != list_end (fd_list); 
           e = list_next (e)){
          file_d = list_entry (e, struct file_desc, elem);
          if (file_d->fd == *(p+1))
           {
             f->eax = file_tell (file_d->file);
             break; 
           }
        }
      break;
    }
    case SYS_CLOSE:{                  /* Close a file. */
      /*L: out of bat....*/
      struct list_elem *e;
      struct file_desc *file_d;
      struct list* fd_list = &thread_current()->fd_list;
      for (e = list_begin (fd_list); e != list_end (fd_list); 
           e = list_next (e)){
          file_d = list_entry (e, struct file_desc, elem);
          if (file_d->fd == *(p+1)){
             file_close (file_d->file);
             list_remove (e);
             free (file_d);
             break;
           }
        }
      break; 
    }
    default:{
      printf("Unhandled SYSCALL(%d)\n",*p);
      thread_exit ();
      }
    }
}
