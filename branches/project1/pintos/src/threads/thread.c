#include "threads/thread.h"
#include <debug.h>
#include <stddef.h>
#include <random.h>
#include <stdio.h>
#include <string.h>
#include "fixed-point.h"
#include "threads/flags.h"
#include "threads/interrupt.h"
#include "threads/intr-stubs.h"
#include "threads/palloc.h"
#include "threads/switch.h"
#include "threads/synch.h"
#include "threads/vaddr.h"
#ifdef USERPROG
#include "userprog/process.h"
#endif

/* Random value for struct thread's `magic' member.
   Used to detect stack overflow.  See the big comment at the top
   of thread.h for details. */
#define THREAD_MAGIC 0xcd6abf4b

/* List of processes in THREAD_READY state, that is, processes
   that are ready to run but not actually running. */
static struct list ready_list;
/* By W:list of thread in THREAD_READY state*/
static struct list block_list;
/* List of all processes.  Processes are added to this list
   when they are first scheduled and removed when they exit. */
static struct list all_list;

/*[X]System load average*/
int64_t load_avg;

/* Idle thread. */
static struct thread *idle_thread;

/* Initial thread, the thread running init.c:main(). */
static struct thread *initial_thread;

/* Lock used by allocate_tid(). */
static struct lock tid_lock;

/* Stack frame for kernel_thread(). */
struct kernel_thread_frame 
  {
    void *eip;                  /* Return address. */
    thread_func *function;      /* Function to call. */
    void *aux;                  /* Auxiliary data for function. */
  };

/* Statistics. */
static long long idle_ticks;    /* # of timer ticks spent idle. */
static long long kernel_ticks;  /* # of timer ticks in kernel threads. */
static long long user_ticks;    /* # of timer ticks in user programs. */

/* Scheduling. */
#define TIME_SLICE 4            /* # of timer ticks to give each thread. */
static unsigned thread_ticks;   /* # of timer ticks since last yield. */

/* If false (default), use round-robin scheduler.
   If true, use multi-level feedback queue scheduler.
   Controlled by kernel command-line option "-o mlfqs". */
bool thread_mlfqs;

static void kernel_thread (thread_func *, void *aux);

static void idle (void *aux UNUSED);
static struct thread *running_thread (void);
static struct thread *next_thread_to_run (void);
static void init_thread (struct thread *, const char *name, int priority);
static bool is_thread (struct thread *) UNUSED;
static void *alloc_frame (struct thread *, size_t size);
static void schedule (void);
void thread_schedule_tail (struct thread *prev);
static tid_t allocate_tid (void);
/* L: This 2 funcs handles donate passively */
bool donate_check (void);
void donate_do (void);

/* Initializes the threading system by transforming the code
   that's currently running into a thread.  This can't work in
   general and it is possible in this case only because loader.S
   was careful to put the bottom of the stack at a page boundary.

   Also initializes the run queue and the tid lock.

   After calling this function, be sure to initialize the page
   allocator before trying to create any threads with
   thread_create().

   It is not safe to call thread_current() until this function
   finishes. */
void
thread_init (void) 
{
  ASSERT (intr_get_level () == INTR_OFF);

  lock_init (&tid_lock);
  list_init (&ready_list);
  /*By W*/
  list_init (&block_list);
  list_init (&all_list);
  /* L: init lock list */
  list_init (&lock_list);
  /*[X] init load_avg*/
  load_avg=0;

  /* Set up a thread structure for the running thread. */
  initial_thread = running_thread ();
  init_thread (initial_thread, "main", PRI_DEFAULT);
  initial_thread->status = THREAD_RUNNING;
  initial_thread->tid = allocate_tid ();
}

/* Starts preemptive thread scheduling by enabling interrupts.
   Also creates the idle thread. */
void
thread_start (void) 
{
  /* Create the idle thread. */
  struct semaphore idle_started;
  sema_init (&idle_started, 0);
  thread_create ("idle", PRI_MIN, idle, &idle_started);

  /* Start preemptive thread scheduling. */
  intr_enable ();

  /* Wait for the idle thread to initialize idle_thread. */
  sema_down (&idle_started);
}

/*[X] calculate the recent_cpu of every thread*/
void renew_recent_cpu(struct thread* t)
{
	/*乘法与加法顺序*/
	t->recent_cpu=FMUL(FDIV(FMULI(load_avg,2),FADDI(FMULI(load_avg,2),1)),t->recent_cpu);
	t->recent_cpu=FADDI(t->recent_cpu,t->nice);
}

/*[X] calculate the ready_threads*/
int64_t get_ready_threads(){
	int64_t rt=0;
	rt=rt+list_size(&ready_list);
	//printf("[X]1ready threads %d\n",rt);
	if(thread_current()!=idle_thread)
		rt++;
	//printf("[X]ready threads %d\n",rt);
	return rt;
}

/*[X] renew priority*/
void renew_priority(struct thread* t)
{
	t->priority=PRI_MAX-F2ITNEAR(FDIVI(t->recent_cpu,4))-(t->nice*2);
	if(t->priority>PRI_MAX)
		t->priority=PRI_MAX;
	if(t->priority<PRI_MIN)
		t->priority=PRI_MIN;
		
}

void renew_all_priority()
{
	struct list_elem *e;
	//printf("list size:%d\n",list_size(&all_list));
	for (e = list_begin (&all_list); e != list_end (&all_list); e = list_next (e))
	{
		if(list_entry(e,struct thread,allelem)==idle_thread)
			continue;
		renew_priority(list_entry(e,struct thread,allelem));
		//printf("[X]renew %s %d\n",list_entry(e,struct thread,allelem)->name,list_entry(e,struct thread,allelem)->priority);
	}
	list_sort(&ready_list,priority_higher,NULL);
	list_sort(&block_list,sleep_less,NULL);
	intr_yield_on_return ();	
}
/* [X] renew all the threads */
void thread_all_renew(void)
{
	ASSERT (intr_get_level () == INTR_OFF);
	struct list_elem *e;
	for (e = list_begin (&all_list); e != list_end (&all_list); e = list_next (e))
	{
		renew_recent_cpu(list_entry(e,struct thread,allelem));
	}
}

/*[X] renew load_avg*/
void renew_load_avg(void)
{
	
	load_avg=FMUL(load_avg,FDIVI(INT2FLO(59),60))+FMULI(get_ready_threads(),FDIVI(INT2FLO(1),60));
}


/* Called by the timer interrupt handler at each timer tick.
   Thus, this function runs in an external interrupt context. */
/*void
thread_tick (void) 
{
  struct thread *t = thread_current ();

  /* Update statistics. 
  if (t == idle_thread)
    idle_ticks++;
#ifdef USERPROG
  else if (t->pagedir != NULL)
    user_ticks++;
#endif
  else
    kernel_ticks++;

  /* Enforce preemption. 
  if (++thread_ticks >= TIME_SLICE)
    intr_yield_on_return ();
}

*/

/* Called by the timer interrupt handler at each timer tick.
   Thus, this function runs in an external interrupt context. */
void
thread_tick (void) 
{	
  struct thread *t = thread_current ();

  /* Update statistics. */
  if (t == idle_thread)
    idle_ticks++;
#ifdef USERPROG
  else if (t->pagedir != NULL)
  {
    user_ticks++;
    /*[X]renew recent_cpu*/
	if(thread_mlfqs)
	{
		t->recent_cpu=FADDI(t->recent_cpu,1);
	}
  }
#endif
  else
  {
    kernel_ticks++;
    /*[X]renew recent_cpu*/
	if(thread_mlfqs)
	{
		t->recent_cpu=FADDI(t->recent_cpu,1);
	}
}
  /*[X]summerize recent_cpu*/
  if(thread_mlfqs)
  {
	if(timer_ticks()%100==0)
	{
	   	/*更新recent_cpu,load_avg*/
	   	renew_load_avg();
	   	thread_all_renew();
     }
  }

 if(thread_mlfqs&&timer_ticks()%4==0)
		renew_all_priority();
  /* Enforce preemption. */
  if (++thread_ticks >= TIME_SLICE)
  {
    intr_yield_on_return ();
  }
}




/* Prints thread statistics. */
void
thread_print_stats (void) 
{
  printf ("Thread: %lld idle ticks, %lld kernel ticks, %lld user ticks\n",
          idle_ticks, kernel_ticks, user_ticks);
}

/* Creates a new kernel thread named NAME with the given initial
   PRIORITY, which executes FUNCTION passing AUX as the argument,
   and adds it to the ready queue.  Returns the thread identifier
   for the new thread, or TID_ERROR if creation fails.

   If thread_start() has been called, then the new thread may be
   scheduled before thread_create() returns.  It could even exit
   before thread_create() returns.  Contrariwise, the original
   thread may run for any amount of time before the new thread is
   scheduled.  Use a semaphore or some other form of
   synchronization if you need to ensure ordering.

   The code provided sets the new thread's `priority' member to
   PRIORITY, but no actual priority scheduling is implemented.
   Priority scheduling is the goal of Problem 1-3. */
tid_t
thread_create (const char *name, int priority,
               thread_func *function, void *aux) 
{
  struct thread *t;
  struct kernel_thread_frame *kf;
  struct switch_entry_frame *ef;
  struct switch_threads_frame *sf;
  tid_t tid;
  enum intr_level old_level;

  ASSERT (function != NULL);

  /* Allocate thread. */
  t = palloc_get_page (PAL_ZERO);
  if (t == NULL)
    return TID_ERROR;

  /* Initialize thread. */
  init_thread (t, name, priority);
  tid = t->tid = allocate_tid ();

  /* Prepare thread for first run by initializing its stack.
     Do this atomically so intermediate values for the 'stack' 
     member cannot be observed. */
  old_level = intr_disable ();

  /* Stack frame for kernel_thread(). */
  kf = alloc_frame (t, sizeof *kf);
  kf->eip = NULL;
  kf->function = function;
  kf->aux = aux;

  /* Stack frame for switch_entry(). */
  ef = alloc_frame (t, sizeof *ef);
  ef->eip = (void (*) (void)) kernel_thread;

  /* Stack frame for switch_threads(). */
  sf = alloc_frame (t, sizeof *sf);
  sf->eip = switch_entry;
  sf->ebp = 0;

  intr_set_level (old_level);
  /* Add to run queue. */
  thread_unblock (t);
  //printf("[CREATE %s,]",t->name);
  //ready_list_dump();
  return tid;
}

/* Puts the current thread to sleep.  It will not be scheduled
   again until awoken by thread_unblock().

   This function must be called with interrupts turned off.  It
   is usually a better idea to use one of the synchronization
   primitives in synch.h. */
void
thread_block (void) 
{
  ASSERT (!intr_context ());
  ASSERT (intr_get_level () == INTR_OFF);

  thread_current ()->status = THREAD_BLOCKED;
  schedule ();
}

/* Transitions a blocked thread T to the ready-to-run state.
   This is an error if T is not blocked.  (Use thread_yield() to
   make the running thread ready.)

   This function does not preempt the running thread.  This can
   be important: if the caller had disabled interrupts itself,
   it may expect that it can atomically unblock a thread and
   update other data. */
void
thread_unblock (struct thread *t) 
{
  enum intr_level old_level;
  ASSERT (is_thread (t));
  
  old_level = intr_disable ();
  ASSERT (t->status == THREAD_BLOCKED);
  /* L: put it to the ready_list order by priority */
  list_insert_ordered(&ready_list,&t->elem,priority_higher,NULL);
  
  t->status = THREAD_READY;
  
  /* L: Some thread t_low may unblock a higher priority thread t_high,
   * in such case t_low must yield cpu to t_high immediately */
  struct list_elem *ready_e = list_max (&ready_list, priority_lower, NULL);
  int max_priority = list_entry(ready_e,struct thread, elem)->priority;
  
  /* L: We just handle normal thread priority change, idle is not
   * one of them. We do not handle it here, just let it pass. */
  if((thread_current ()->priority < max_priority) &&(thread_current() != idle_thread))
  {
    /* L: unblock maybe called in intr-context or non-intr-context,
     * they are different in handling. */
    if (intr_context ())
      intr_yield_on_return ();
    else
      thread_yield ();
  }
  
  intr_set_level (old_level);
}

/* Returns the name of the running thread. */
const char *
thread_name (void) 
{
  return thread_current ()->name;
}

/* Returns the running thread.
   This is running_thread() plus a couple of sanity checks.
   See the big comment at the top of thread.h for details. */
struct thread *
thread_current (void) 
{
  struct thread *t = running_thread ();
  
  /* Make sure T is really a thread.
     If either of these assertions fire, then your thread may
     have overflowed its stack.  Each thread has less than 4 kB
     of stack, so a few big automatic arrays or moderate
     recursion can cause stack overflow. */
  ASSERT (is_thread (t));
  //ASSERT (t->status == THREAD_RUNNING);

  return t;
}

/* Returns the running thread's tid. */
tid_t
thread_tid (void) 
{
  return thread_current ()->tid;
}

/* Deschedules the current thread and destroys it.  Never
   returns to the caller. */
void
thread_exit (void) 
{
  ASSERT (!intr_context ());

#ifdef USERPROG
  process_exit ();
#endif

  /* Remove thread from all threads list, set our status to dying,
     and schedule another process.  That process will destroy us
     when it calls thread_schedule_tail(). */
  intr_disable ();
  list_remove (&thread_current()->allelem);
  thread_current ()->status = THREAD_DYING;
  schedule ();
  NOT_REACHED ();
}

/* Yields the CPU.  The current thread is not put to sleep and
   may be scheduled again immediately at the scheduler's whim. */
void
thread_yield (void) 
{
  struct thread *cur = thread_current ();
  enum intr_level old_level;
  
  ASSERT (!intr_context ());

  old_level = intr_disable ();
  
  if (cur != idle_thread) 
    /* L:put it to the ready_list with priority */
    list_insert_ordered(&ready_list,&cur->elem,priority_higher,NULL);
  cur->status = THREAD_READY;
  
  schedule ();
  intr_set_level (old_level);
}

/* Invoke function 'func' on all threads, passing along 'aux'.
   This function must be called with interrupts off. */
void
thread_foreach (thread_action_func *func, void *aux)
{
  struct list_elem *e;

  ASSERT (intr_get_level () == INTR_OFF);

  for (e = list_begin (&all_list); e != list_end (&all_list);
       e = list_next (e))
    {
      struct thread *t = list_entry (e, struct thread, allelem);
      func (t, aux);
    }
}

/* Sets the current thread's priority to NEW_PRIORITY. */
void
thread_set_priority (int new_priority) 
{
  /*[X] if we use advanced scheculer, we can't set priority by ourselves*/
  if(thread_mlfqs)
	return;
  struct thread *cur = thread_current();
  if(cur->priority == cur->priority_old)
  {
    cur->priority = new_priority;
    cur->priority_old = new_priority;
  }
  else
  {
    cur->priority_old = new_priority;
  }
  
  /* L: priority-change test requires an immediately yield when
   * cur->priority < max_priority(ready_list).
   * A check is needed here. */
  if(!list_empty(&ready_list))
  {
    struct list_elem *ready_e = list_max (&ready_list, priority_higher, NULL);
    int max_priority = list_entry(ready_e,struct thread, elem)->priority;
    
    if(thread_current ()->priority < max_priority)
      thread_yield();
  }
}

/* Returns the current thread's priority. */
int
thread_get_priority (void) 
{
  return thread_current ()->priority;
}

/* Sets the current thread's nice value to NICE. */
void
thread_set_nice (int nice) 
{
  /* [X]set nice value */
  thread_current()->nice=nice;
}

/* Returns the current thread's nice value. */
int
thread_get_nice () 
{
  /*[X] return the nice value of the thread */
  return thread_current()->nice;
}

/* Returns 100 times the system load average. */
int
thread_get_load_avg (void) 
{
  /* [X] the int64 problem */
  return F2ITNEAR(FMULI(load_avg,100));
}

/* Returns 100 times the current thread's recent_cpu value. */
int
thread_get_recent_cpu (void) 
{
  /* [X] */
  return F2ITNEAR(FMULI(thread_current()->recent_cpu,100));
}

/* Idle thread.  Executes when no other thread is ready to run.

   The idle thread is initially put on the ready list by
   thread_start().  It will be scheduled once initially, at which
   point it initializes idle_thread, "up"s the semaphore passed
   to it to enable thread_start() to continue, and immediately
   blocks.  After that, the idle thread never appears in the
   ready list.  It is returned by next_thread_to_run() as a
   special case when the ready list is empty. */
static void
idle (void *idle_started_ UNUSED) 
{
  struct semaphore *idle_started = idle_started_;
  idle_thread = thread_current ();
  sema_up (idle_started);

  for (;;) 
    {
      /* Let someone else run. */
      intr_disable ();
      thread_block ();

      /* Re-enable interrupts and wait for the next one.

         The `sti' instruction disables interrupts until the
         completion of the next instruction, so these two
         instructions are executed atomically.  This atomicity is
         important; otherwise, an interrupt could be handled
         between re-enabling interrupts and waiting for the next
         one to occur, wasting as much as one clock tick worth of
         time.

         See [IA32-v2a] "HLT", [IA32-v2b] "STI", and [IA32-v3a]
         7.11.1 "HLT Instruction". */
      asm volatile ("sti; hlt" : : : "memory");
    }
}

/* Function used as the basis for a kernel thread. */
static void
kernel_thread (thread_func *function, void *aux) 
{
  ASSERT (function != NULL);

  intr_enable ();       /* The scheduler runs with interrupts off. */
  function (aux);       /* Execute the thread function. */
  thread_exit ();       /* If function() returns, kill the thread. */
}

/* Returns the running thread. */
struct thread *
running_thread (void) 
{
  uint32_t *esp;

  /* Copy the CPU's stack pointer into `esp', and then round that
     down to the start of a page.  Because `struct thread' is
     always at the beginning of a page and the stack pointer is
     somewhere in the middle, this locates the curent thread. */
  asm ("mov %%esp, %0" : "=g" (esp));
  return pg_round_down (esp);
}

/* Returns true if T appears to point to a valid thread. */
static bool
is_thread (struct thread *t)
{
  return t != NULL && t->magic == THREAD_MAGIC;
}

/* Does basic initialization of T as a blocked thread named
   NAME. */
static void
init_thread (struct thread *t, const char *name, int priority)
{
  ASSERT (t != NULL);
  ASSERT (PRI_MIN <= priority && priority <= PRI_MAX);
  ASSERT (name != NULL);

  memset (t, 0, sizeof *t);
  t->status = THREAD_BLOCKED;
  strlcpy (t->name, name, sizeof t->name);
  t->stack = (uint8_t *) t + PGSIZE;
  
  if(thread_mlfqs)
  {
	t->recent_cpu=0;
	t->nice=0;
	renew_priority(t);
	//printf("[X]%d\n",t->priority);
  }
  else
  {
  t->priority = priority;
  /* Initialize old_priority */
  t->priority_old=t->priority;
}
  t->magic = THREAD_MAGIC;
  list_push_back (&all_list, &t->allelem);
}

/* Allocates a SIZE-byte frame at the top of thread T's stack and
   returns a pointer to the frame's base. */
static void *
alloc_frame (struct thread *t, size_t size) 
{
  /* Stack data is always allocated in word-size units. */
  ASSERT (is_thread (t));
  ASSERT (size % sizeof (uint32_t) == 0);

  t->stack -= size;
  return t->stack;
}

/* Chooses and returns the next thread to be scheduled.  Should
   return a thread from the run queue, unless the run queue is
   empty.  (If the running thread can continue running, then it
   will be in the run queue.)  If the run queue is empty, return
   idle_thread. */
static struct thread *
next_thread_to_run (void) 
{
  if (list_empty (&ready_list))
    return idle_thread;
  else
    {
     //return list_entry (list_pop_front (&ready_list), struct thread, elem);
     struct list_elem *ready_e = list_max (&ready_list, priority_lower, NULL);
     return list_entry (ready_e, struct thread, elem);
    }
}

/* Completes a thread switch by activating the new thread's page
   tables, and, if the previous thread is dying, destroying it.

   At this function's invocation, we just switched from thread
   PREV, the new thread is already running, and interrupts are
   still disabled.  This function is normally invoked by
   thread_schedule() as its final action before returning, but
   the first time a thread is scheduled it is called by
   switch_entry() (see switch.S).

   It's not safe to call printf() until the thread switch is
   complete.  In practice that means that printf()s should be
   added at the end of the function.

   After this function and its caller returns, the thread switch
   is complete. */
void
thread_schedule_tail (struct thread *prev)
{
  struct thread *cur = running_thread ();
  
  ASSERT (intr_get_level () == INTR_OFF);

  /* Mark us as running. */
  cur->status = THREAD_RUNNING;

  /* Start new time slice. */
  thread_ticks = 0;

#ifdef USERPROG
  /* Activate the new address space. */
  process_activate ();
#endif

  /* If the thread we switched from is dying, destroy its struct
     thread.  This must happen late so that thread_exit() doesn't
     pull out the rug under itself.  (We don't free
     initial_thread because its memory was not obtained via
     palloc().) */
  if (prev != NULL && prev->status == THREAD_DYING && prev != initial_thread) 
    {
      ASSERT (prev != cur);
      palloc_free_page (prev);
    }
}

/* Schedules a new process.  At entry, interrupts must be off and
   the running process's state must have been changed from
   running to some other state.  This function finds another
   thread to run and switches to it.

   It's not safe to call printf() until thread_schedule_tail()
   has completed. */
static void
schedule (void) 
{
  /* L: donate may change the next_thread_to_run ()
   * We must perform a donate check before call it. */
  if (donate_check ())
      donate_do();
  struct thread *cur = running_thread ();
  struct thread *next = next_thread_to_run ();
  struct thread *prev = NULL;

  ASSERT (intr_get_level () == INTR_OFF);
  ASSERT (cur->status != THREAD_RUNNING);
  ASSERT (is_thread (next));

  /* L: Move the remove from next_thread_to_run to here. */
  list_remove (&next->elem);
  if (cur != next)
  {
    prev = switch_threads (cur, next);
  }
  thread_schedule_tail (prev);
}

/* Returns a tid to use for a new thread. */
static tid_t
allocate_tid (void) 
{
  static tid_t next_tid = 1;
  tid_t tid;

  lock_acquire (&tid_lock);
  tid = next_tid++;
  lock_release (&tid_lock);

  return tid;
}

/* Offset of `stack' member within `struct thread'.
   Used by switch.S, which can't figure it out on its own. */
uint32_t thread_stack_ofs = offsetof (struct thread, stack);

/*By W:Sleep the thread,called by timer_sleep()*/
void
thread_sleep(int64_t s_ticks) 
{
  struct thread *cur = thread_current ();
  enum intr_level old_level;
  
  ASSERT (!intr_context ());
  old_level = intr_disable ();  
  
  if (cur != idle_thread) 
  { 
      cur->wakeup_tick=timer_ticks()+s_ticks;
      cur->status = THREAD_BLOCKED;
      list_insert_ordered(&block_list,&cur->elem,sleep_less,NULL);
  }
  
  schedule();
  intr_set_level (old_level);
}

/* By W:Wakeup the block thread*/
void 
thread_wakeup()
{
  struct list_elem *temp,*mid;
  struct thread *st;

  for (temp= list_begin (&block_list); temp != list_end (&block_list);
       )
    {  
      st = list_entry (temp, struct thread, elem);
        
      if(timer_ticks()>=st->wakeup_tick)
        {
          mid=list_remove(temp);
          st = list_entry (temp, struct thread, elem);
          thread_unblock(st);
          temp=mid;
        }
        else 
        {
          break;
        }
    } 
}

/*By W:Compares the value of two thread's wakeup_tick A and B,*/
bool sleep_less (const struct list_elem *a,
                  const struct list_elem *b,void *aux)
{
  struct thread *a_thread,*b_thread;
  a_thread=list_entry (a, struct thread, elem);
  b_thread=list_entry (b, struct thread,elem);

  return(a_thread->wakeup_tick<b_thread->wakeup_tick);
}

/* L: Used in the unblock to insert thread to ready_list ordered by
 * thread's priority. When schedule, we just take the top one out.
 * NOW IT's DANGEROUS TO USE IT, it will cause list_max() returns min*/
bool priority_higher (const struct list_elem *a,
                  const struct list_elem *b,void *aux)
{
  struct thread *a_thread,*b_thread;
  a_thread=list_entry (a, struct thread, elem);
  b_thread=list_entry (b, struct thread,elem);

  return(a_thread->priority > b_thread->priority);
}
/* L: priority Lower func */
bool priority_lower (const struct list_elem *a,
                  const struct list_elem *b,void *aux)
{ 
   struct thread *a_thread,*b_thread;
   a_thread=list_entry (a, struct thread, elem);
   b_thread=list_entry (b, struct thread,elem);
   
   return(a_thread->priority < b_thread->priority);
}

bool lock_lower (const struct list_elem *a,
           const struct list_elem *b,
           void *aux UNUSED)
{ 
  struct thread *ah = list_entry (a, struct lock_elem, elem)->lock->holder;
  struct thread *bh = list_entry (b, struct lock_elem, elem)->lock->holder;
  int ap, bp;
  if (ah == NULL) ap = 0;
  else ap = ah->priority;
  if (bh == NULL) bp = 0;
  else bp = bh->priority;
  return (ap < bp);
}

bool cond_lower (const struct list_elem *a,
           const struct list_elem *b,
           void *aux UNUSED)
{
  struct semaphore *as = &list_entry (a, struct semaphore_elem, elem)->semaphore;
  struct semaphore *bs = &list_entry (b, struct semaphore_elem, elem)->semaphore;
  int ap = list_entry (list_begin (&as->waiters) , struct thread, elem)->priority;
  int bp = list_entry (list_begin (&bs->waiters) , struct thread, elem)->priority;
  if (ap < bp) return true;
  return false;
}

/* L:Debug func dump the ready_list */
/* Debug:dump the ready list */
void ready_list_dump(void)
{
  struct list_elem *e;
  struct thread *f;
  if(list_size(&ready_list)!=0){
  printf("[%lld,dump ready list]\n",(uint64_t)timer_ticks());
  for (e = list_begin (&ready_list); e != list_end (&ready_list);
           e = list_next (e))
        {
          f = list_entry (e, struct thread, elem);
          printf("[* %s is ready,pri:%d]\n",f->name,f->priority);
        }
      }
}

/* L: Check if some waiter has a higher priority,
 * that is, we need a donation. */
bool donate_check (void)
{
  struct list_elem *e;

  for (e = list_begin (&lock_list); e != list_end (&lock_list); 
       e = list_next (e))
    {
      struct lock_elem *lock_e = list_entry (e, struct lock_elem, elem);

      /* L: no holder or no waiter means no donate. */
      if (lock_e->lock->holder == NULL)
         continue;
      if (list_empty (&lock_e->lock->semaphore.waiters))
         continue;
      /* L: Find the max priorty in waiters */
      struct list_elem *max_e = list_max (&lock_e->lock->semaphore.waiters, 
                                          priority_lower, NULL);
      struct thread *max_waiter = list_entry (max_e, struct thread, elem);

      if (max_waiter == NULL)
         continue;
      /* L: A donation is needed */
      if (lock_e->lock->holder->priority < max_waiter->priority)
         return true;
     }
   return false;
}

/* L: Find the max priority in waiters, and donate it to holder. */
void donate_do (void)
{ 
  list_sort (&lock_list, lock_lower, NULL);
  struct list_elem *e;
  /* L: The higher priority is at the end, So we search it from back to
   * front. */
  for (e = list_rbegin (&lock_list); e != list_rend (&lock_list);
       e = list_prev (e))
    {
      struct lock_elem *lock_e = list_entry (e, struct lock_elem, elem);
      struct thread *holder = lock_e->lock->holder;
      if (holder == NULL)
         continue;
      struct list_elem *max_e = list_rbegin (&lock_e->lock->semaphore.waiters);
      struct thread *max_waiter = list_entry (max_e, struct thread, elem);
      if (max_waiter == NULL)
        continue;
      if (holder->priority < max_waiter->priority)
      {
        holder->priority = max_waiter->priority;
      }
    }
}

