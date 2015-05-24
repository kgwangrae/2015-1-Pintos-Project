#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/synch.h"
#include "threads/thread.h"
#include "threads/malloc.h"
#include "devices/shutdown.h"
#include "devices/Input.h"
#include "filesys/file.h"
#include "filesys/filesys.h"

#define MAX_ARGS 3

struct lock fs_lock;

static void syscall_handler (struct intr_frame *);

void syscall_init (void) 
{
  lock_init(&fs_lock);
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
}

// Note: f is the user stack frame.
static void syscall_handler (struct intr_frame *f UNUSED) 
{
  int arg[MAX_ARGS];
  switch (* (int *) f->esp)
  {
    case SYS_HALT:
      {
        halt();
        break;
      }
    case SYS_EXIT:
      {
        break;
      }
    case SYS_EXEC:
      {
        break;
      }
    case SYS_WAIT:
      {
        break;
      }
    case SYS_CREATE:
      {
        break;
      }
    case SYS_REMOVE:
      {
        break;
      }
    case SYS_OPEN:
      {
        break;
      }
    case SYS_FILESIZE:
      {
        break;
      }
    case SYS_READ:
      {
        break;
      }
    case SYS_WRITE:
      {
        break;
      }
    case SYS_SEEK:
      {
        break;
      }
    case SYS_TELL:
      {
        break;
      }
    case SYS_CLOSE:
      {
        break;
      }
  }
}

// File system operations below

struct process_file 
{
  struct file *file;
  int fd;
  struct list_elem elem;
};

int pf_add (struct file *new_file)
{
  struct process_file *pf = malloc(sizeof(struct process_file));
  pf->file = new_file;
  pf->fd = thread_current()->fd_avail;
  thread_current()->fd_avail++;
  list_push_back(&thread_current()->files, &pf->elem);
  return pf->fd;
}

struct file* pf_get (int fd)
{
  if (fd < 0) return NULL;

  struct thread *t = thread_current();
  struct list_elem *e;
  struct process_file *pf;

  for (e = list_begin (&t->files); e != list_end (&t->files);
       e = list_next (e))
  {
    pf = list_entry (e, struct process_file, elem);
    if (pf->fd == fd) return pf->file;
  }

  return NULL;
}

void pf_close (int fd) 
{
  if (fd < 0) return;

  struct thread *t = thread_current();
  struct list_elem *e, *next;
  struct process_file *pf;

  for (e = list_begin (&t->files); e != list_end (&t->files);
       e = next)
  {
    pf = list_entry (e, struct process_file, elem);
    next = list_next (e);
    if (pf->fd == fd) 
    {
      file_close(pf->file);
      list_remove(&pf->elem);
      free(pf);
      return;
    }
  }
}

void pf_close_all () 
{
  struct thread *t = thread_current();
  struct list_elem *e, *next;
  struct process_file *pf;

  for (e = list_begin (&t->files); e != list_end (&t->files);
       e = next)
  {
    pf = list_entry (e, struct process_file, elem);
    next = list_next (e);
    file_close(pf->file);
    list_remove(&pf->elem);
    free(pf);
  }
}

bool create (const char *file, unsigned initial_size) 
{
  lock_acquire(&fs_lock);
  bool success = filesys_create(file, initial_size);
  lock_release(&fs_lock);
  return success;
}

bool remove (const char *file)
{
  lock_acquire(&fs_lock);
  bool success = filesys_remove(file);
  lock_release(&fs_lock);
  return success;
}

int open (const char *file)
{ 
  lock_acquire(&fs_lock);
  struct file *f = filesys_open(file); 
  int fd;

  if (f) fd = pf_add (f);
  else fd = SYSCALL_ERROR;

  lock_release(&fs_lock);
  return fd;
}

int filesize (int fd) 
{
  lock_acquire(&fs_lock);
  struct file *f = pf_get(fd); 
  int result;

  if (f) result = file_length(f);
  else result = SYSCALL_ERROR;

  lock_release(&fs_lock);
  return result;
}

int read (int fd, void *buffer, unsigned length) {
  if (fd == STDIN_FILENO) 
  {
    uint8_t *buf = (uint8_t *) buffer; // 1byte char array
    for (unsigned i = 0; i < length; i++) 
    {
      buf[i] = Input_getc();
      // buf size limit?
    } 
    return length;
  }
  // From filesystem
  else 
  {
    lock_acquire(&fs_lock);
    struct file *f = pf_get(fd); 
    int bytes_read;

    if (f) bytes_read = file_read(f, buffer, length);
    else bytes_read = SYSCALL_ERROR;

    lock_release(&fs_lock);
    return bytes_read; 
  }
}

int write (int fd, const void *buffer, unsigned length) 
{
  if (fd == STDOUT_FILENO) 
  {
    putbuf(buffer, length); 
    return length;
  }
  // To filesystem
  else 
  {
    lock_acquire(&fs_lock);
    struct file *f = pf_get(fd); 
    int bytes_written;

    if (f) bytes_written = file_write(f, buffer, length);
    else bytes_written = SYSCALL_ERROR;

    lock_release(&fs_lock);
    return bytes_written; 
  }
}

// Changes the next byte to read in a file (file start : position 0)
void seek (int fd, unsigned position) 
{
  lock_acquire(&fs_lock);
  struct file *f = pf_get(fd); 

  if (f) file_seek(f, position);

  lock_release(&fs_lock);
}

// next byte to read
unsigned tell (int fd) 
{
  lock_acquire(&fs_lock);
  struct file *f = pf_get(fd);
  off_t offset;

  if (f) offset = file_tell(f);
  else offset = SYSCALL_ERROR;

  lock_release(&fs_lock);
  return offset;
}

void close (int fd)
{
  lock_acquire(&fs_lock);
  pf_close(fd);
  lock_release(&fs_lock); 
}


void halt (void)
{
  shutdown_power_off ();
}

// Operations for process management

void exit (int status)
{
  struct thread *cur = thread_current ();
  
  cur->exit_status = status;
  thread_exit ();
}

pid_t exec (const char *file)
{
  pid_t pid = process_execute (file);

  struct thread *t = get_thread (pid);
  sema_down (&t->sema_success);
  sema_up (&t->sema_success);

  return t->tid;
}

int wait (pid_t pid)
{
  return process_wait (pid);
}
