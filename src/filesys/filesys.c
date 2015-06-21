#include "filesys/filesys.h"
#include <debug.h>
#include <stdio.h>
#include <string.h>
#include <list.h>
#include "filesys/file.h"
#include "filesys/free-map.h"
#include "filesys/inode.h"
#include "filesys/directory.h"
#include "threads/thread.h"
#include "threads/malloc.h"

/* Partition that contains the file system. */
struct block *fs_device;

static void do_format (void);

/* Initializes the file system module.
   If FORMAT is true, reformats the file system. */
void
filesys_init (bool format) 
{
  fs_device = block_get_role (BLOCK_FILESYS);
  if (fs_device == NULL)
    PANIC ("No file system device found, can't initialize file system.");

  inode_init ();
  free_map_init ();

  if (format) 
    do_format ();

  free_map_open ();
}

/* Shuts down the file system module, writing any unwritten data
   to disk. */
void
filesys_done (void) 
{
  free_map_close ();
}

/* Creates a file named NAME with the given INITIAL_SIZE. Returns true if successful, false otherwise.
   Fails if a file named NAME already exists, or if internal memory allocation fails. */
bool
filesys_create (const char *name, off_t initial_size) 
{
   if (*name == '\0') // empty name
    return false;

  block_sector_t inode_sector = 0;
  struct dir *dir = get_dir (name, false);
  char *filename = get_filename (name);
  bool success = (dir != NULL
                  && free_map_allocate (1, &inode_sector)
                  && inode_create (inode_sector, initial_size, false)
                  && dir_add (dir, filename, inode_sector));
  if (!success && inode_sector != 0) 
    free_map_release (inode_sector, 1);
  dir_close (dir);

  return success;
}

/* Opens the file with the given NAME.
   Returns the new file if successful or a null pointer
   otherwise.
   Fails if no file named NAME exists,
   or if an internal memory allocation fails. */
struct file *
filesys_open (const char *name)
{
  if (*name == '\0') // empty path
    return NULL;

  struct dir *dir = get_dir (name, false);
  
  if (dir == NULL)
    return NULL; 

  struct inode *inode = NULL;
  char *filename = get_filename (name); 
 
  if (*filename == '\0') // when given name refers to a directory
    inode = dir_get_inode (dir); 
  else // when given name refers to a file
  {
    dir_lookup (dir, filename, &inode);
    dir_close (dir);
  }

  if (inode == NULL || inode_is_removed (inode))
    return NULL;

  return file_open (inode);
}

/* Deletes the file named NAME.
   Returns true if successful, false on failure.
   Fails if no file named NAME exists,
   or if an internal memory allocation fails. */
bool
filesys_remove (const char *name) 
{
  struct dir *dir = get_dir (name, false);
  char *filename = get_filename (name);
  bool success = dir != NULL && dir_remove (dir, filename);
  dir_close (dir); 

  return success;
}

/* Formats the file system. */
static void
do_format (void)
{
  printf ("Formatting file system...");
  free_map_create ();
  if (!dir_create (ROOT_DIR_SECTOR, 16, NULL))
    PANIC ("root directory creation failed");
  free_map_close ();
  printf ("done.\n");
}

/* Open and returns the directory for given path */
struct dir* get_dir (const char* path, bool include_last_token)
{
    struct dir *dir;
    char *path_copy = (char *) malloc (strlen(path) + 1);
    strlcpy (path_copy, path, strlen(path) + 1); 

    if (path_copy[0] != '/') // relative path
    {
      if (thread_current()->dir)
        dir = dir_reopen(thread_current()->dir);
      else
        dir = dir_open_root ();
    }
    else // absolute path
    {
      dir = dir_open_root ();
      path_copy++;
    }
    
    char *save_ptr, *next = NULL, *token = strtok_r(path_copy, "/", &save_ptr);

    if (token)
      next = strtok_r(NULL, "/", &save_ptr);

    while (true)
    {
      if (next==NULL && !include_last_token) // ignore the last token for all case except chdir
        break;

      struct inode *inode;
 
      if (token != NULL)
      {      
        if (!dir_lookup(dir, token, &inode))
        {
          dir_close (dir);
          free (path_copy);        
          return NULL;
        }
        
        dir_close (dir);
        dir = dir_open (inode);
      }
       
      if (next == NULL)
        break;

      token = next;
      next = strtok_r(NULL, "/", &save_ptr);
    }
    free (path_copy);
   
    if (inode_is_removed (dir->inode))
      return NULL;
 
    return dir;
}

/* returns the file name for given path */
char* get_filename (const char* path)
{
  /* Invalid path */
  if(path==NULL || strlen(path)==0) return NULL;

  char *slash = strrchr(path, '/');
  if (slash == NULL) 
  {
    /* Opening files in cwd : there's no / in the path. */
    return path;
  }
  else return slash+1;
}
