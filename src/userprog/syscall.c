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
syscall_handler (struct intr_frame *f UNUSED) 
{
  printf ("system call!\n");
  thread_exit ();
}

void
halt (void)
{
  shutdown_power_off ();
}

void
exit (int status)
{
  struct thread *cur = thread_current ();
  
  cur->exit_status = status;
  thread_exit ();
}

pid_t 
exec (const char *file)
{
  pid_t pid = process_execute (file);

  struct thread *t = get_thread (pid);
  sema_down (&t->sema_success);
  sema_up (&t->sema_success);

  return t->tid;
}

int
wait (pid_t pid)
{
  return process_wait (pid);
}
