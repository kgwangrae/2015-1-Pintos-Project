#include "userprog/syscall.h"
#include "userprog/process.h"
#include "userprog/pagedir.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/synch.h"
#include "threads/malloc.h"
#include "threads/vaddr.h"
#include "devices/shutdown.h"
#include "devices/input.h"
#include "filesys/filesys.h"
#include "filesys/file.h"
#include "filesys/directory.h"
#include "filesys/free-map.h"
#include "filesys/inode.h"

struct child* get_child (struct thread *t, tid_t tid);
static void syscall_handler (struct intr_frame *);

#define MAX_ARGS 3 // 3 args are enough for system calls.
void get_arg (struct intr_frame *f, int *arg, int n);

#define UADDR_BASE (const void *) 0x08048000
int ptr_user_to_kernel(const void *vaddr);
void ptr_validate (const void *vaddr);
void buf_validate (const void *buf, unsigned size);


void syscall_init (void) 
{
  lock_init(&fs_lock);
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
}

// Note: f is the user stack frame.
static void syscall_handler (struct intr_frame *f UNUSED) 
{
  int arg[MAX_ARGS];
  ptr_validate((const void *) f->esp);
  switch (* (int *) f->esp)
  {
    //void halt (void)
    case SYS_HALT:
      {
        halt();
        break;
      }
    //void exit (int status)
    case SYS_EXIT:
      {
        get_arg(f, &arg[0], 1);
        exit(arg[0]);
        break;
      }
    //pid_t exec (const char *file)
    case SYS_EXEC:
      {
        get_arg(f, &arg[0], 1);
        arg[0] = ptr_user_to_kernel((const void *) arg[0]);
        f->eax = exec((const char *) arg[0]);
        break;
      }
    //int wait (pid_t pid)
    case SYS_WAIT:
      {
        get_arg(f, &arg[0], 1);
        f->eax = wait(arg[0]);
        break;
      }
    //bool create (const char *file, unsigned initial_size)
    case SYS_CREATE:
      {
        get_arg(f, &arg[0], 2);
        arg[0] = ptr_user_to_kernel((const void *) arg[0]);
        f->eax = create((const char *)arg[0], (unsigned) arg[1]);
        break;
      }
    //bool remove (const char *file)
    case SYS_REMOVE:
      {
        get_arg(f, &arg[0], 1);
        arg[0] = ptr_user_to_kernel((const void *) arg[0]);
        f->eax = remove((const char *) arg[0]);
        break;
      }
    //int open (const char *file)
    case SYS_OPEN:
      {
        get_arg(f, &arg[0], 1);
        arg[0] = ptr_user_to_kernel((const void *) arg[0]);
        f->eax = open((const char *) arg[0]);
        break;
      }
    //int filesize (int fd)
    case SYS_FILESIZE:
      {
        get_arg(f, &arg[0], 1);
        f->eax = filesize(arg[0]);
        break;
      }
    //int read (int fd, void *buffer, unsigned length)
    case SYS_READ:
      {
        get_arg(f, &arg[0], 3);
        buf_validate((const void *) arg[1], (unsigned) arg[2]);
        arg[1] = ptr_user_to_kernel((const void *) arg[1]);
        f->eax = read(arg[0], (void *) arg[1], (unsigned) arg[2]);
        break;
      }
    //int write (int fd, const void *buffer, unsigned length)
    case SYS_WRITE:
      {
        get_arg(f, &arg[0], 3);
        buf_validate((const void *) arg[1], (unsigned) arg[2]);
        arg[1] = ptr_user_to_kernel((const void *) arg[1]);
        f->eax = write(arg[0], (const void *) arg[1],
            (unsigned) arg[2]);
        break;
      }
    //void seek (int fd, unsigned position)
    case SYS_SEEK:
      {
        get_arg(f, &arg[0], 2);
        seek(arg[0], (unsigned) arg[1]);
        break;
      }
    //unsigned tell (int fd)
    case SYS_TELL:
      {
        get_arg(f, &arg[0], 1);
        f->eax = tell(arg[0]);
        break;
      }
    //void close (int fd)
    case SYS_CLOSE:
      {
        get_arg(f, &arg[0], 1);
        close(arg[0]);
        break;
      } 
    //bool chdir (const char *dir)
    case SYS_CHDIR:
      {
        get_arg(f, &arg[0], 1);
        arg[0] = ptr_user_to_kernel((const void *) arg[0]);
        f->eax = chdir((const char *) arg[0]);
        break;
      }
    //bool mkdir (const char *dir)
    case SYS_MKDIR:
      {
        get_arg(f, &arg[0], 1);
        arg[0] = ptr_user_to_kernel((const void *) arg[0]);
        f->eax = mkdir((const char *) arg[0]);
        break;
      }
    //bool readdir (int fd, char *name)
    case SYS_READDIR:
      {
        get_arg(f, &arg[0], 2);
        arg[1] = ptr_user_to_kernel((const void *) arg[1]);
        f->eax = readdir(arg[0], (char *) arg[1]);
        break;
      }
    //bool isdir (int fd)
    case SYS_ISDIR:
      {
        get_arg(f, &arg[0], 1);
        f->eax = isdir(arg[0]);
        break;
      }
    //int inumber (int fd)
    case SYS_INUMBER:
      {
        get_arg(f, &arg[0], 1);
        f->eax = inumber(arg[0]);
        break;
      }
  }
}

// File system operations below

int pf_add (struct file *new_file)
{
  struct process_file *pf = malloc(sizeof(struct process_file));
  pf->file = new_file;
  pf->fd = thread_current()->fd_avail;
  if (inode_is_dir (file_get_inode (new_file)))
    pf->dir = dir_open (inode_reopen (file_get_inode (new_file)));
  else
    pf->dir = NULL;
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
      if (pf->dir)
        dir_close (pf->dir);
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
    if (pf->dir)
      dir_close (pf->dir);
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

int read (int fd, void *buffer, unsigned length) 
{
  if (fd == STDIN_FILENO) 
  {
    uint8_t *buf = (uint8_t *) buffer; // 1byte char array
    unsigned i;
    for (i = 0; i < length; i++) 
    {
      buf[i] = input_getc();
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

bool
chdir (const char *dir)
{
  struct dir *ch_dir = get_dir (dir, true);
  if (ch_dir == NULL)
    return false;

  dir_close (thread_current ()->dir);
  thread_current ()->dir = ch_dir;

  return true;
}

bool
mkdir (const char *dir)
{
  struct dir *cur_dir = get_dir (dir, false);
  char *new_dir = get_filename (dir);
  struct inode *inode;
  block_sector_t sector = -1;

  bool success = (cur_dir != NULL
		  && !dir_lookup (cur_dir, new_dir, &inode)
		  && free_map_allocate (1, &sector)
		  && dir_create (sector, 16, cur_dir)
		  && dir_add (cur_dir, new_dir, sector));

  if (cur_dir)
    dir_close (cur_dir);
  
  if(!success && sector != -1)
    free_map_release (sector, 1);

  return success;
}

bool
readdir (int fd, char *name)
{
  struct thread *cur = thread_current ();
  struct list_elem *e;
  struct process_file *pf = NULL;
  struct file *f = NULL;

  for (e = list_begin (&cur->files); e != list_end (&cur->files); e = list_next (e))
  {
    pf = list_entry (e, struct process_file, elem);
    if (pf->fd == fd)
    {
      f = pf->file;
      break;
    }
  }

  if (pf == NULL)
    return false;

  if (f == NULL)
    return false;

  if (!inode_is_dir (file_get_inode (f)))
    return false;

  return dir_readdir (pf->dir, name);
}

bool
isdir (int fd)
{
  struct file *f = pf_get (fd);

  if (f == NULL)
    return false;

  return inode_is_dir (file_get_inode (f));
}

int inumber (int fd)
{
  struct file *f = pf_get (fd);
  
  if (f == NULL)
    return false;

  return inode_number (file_get_inode (f));
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

  if (!list_empty (&cur->parent->children))
  {
    struct child *child = get_child (cur->parent, cur->tid);
   
    if (child != NULL)
    {
      child->exit_status = status;
      child->exit = true;

      if (cur->parent->waiting_child == cur->tid)
        sema_up (&cur->parent->sema_wait);    
    }
  }

  thread_exit ();
}

pid_t exec (const char *file)
{
  lock_acquire (&fs_lock);
  pid_t pid = process_execute (file);
  lock_release (&fs_lock);

  return pid;
}

int wait (pid_t pid)
{
  return process_wait (pid);
}

/* Operations for memory management and argument passing */

/*
 * -----------
 * param 3  // esp + 12
 * param 2  // esp + 8
 * param 1  // esp + 4
 * return addr <---- esp
 * -----------
 * Refer to 80x86 calling convention for details.
 */
void get_arg (struct intr_frame *f, int *arg, int n) 
{
  int i, *ptr;
  for (i = 1; i <= n; i++)
  {
    ptr = (int *) f->esp + i;
    ptr_validate((const void *) ptr);
    arg[i-1] = *ptr;
  }
}

int ptr_user_to_kernel(const void *vaddr) 
{
  ptr_validate(vaddr);
  void *ptr = pagedir_get_page(thread_current()->pagedir, vaddr);
  if (ptr) return (int) ptr;
  else exit(SYSCALL_ERROR);
}

/*
 * Validate all user memory access attempts.
 * PHYS_BASE > vaddr >= UADDR_BASE 
 */
void ptr_validate (const void *vaddr)
{
  if (!is_user_vaddr(vaddr)) goto err;
  if (vaddr < UADDR_BASE) goto err;
  if (pagedir_get_page(thread_current()->pagedir, vaddr) == NULL) goto err;
  return;
err:
  exit(SYSCALL_ERROR);
}

/* Prevents buffer overflow. 
 * Assume any buffer resides on memory continuously.
 * TODO : Is the assumption above safe?
 */
void buf_validate (const void *buf, unsigned size)
{
  ptr_validate(buf);
  ptr_validate(buf+size-1);
}
