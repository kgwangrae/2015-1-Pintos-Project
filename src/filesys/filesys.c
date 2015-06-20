#include "filesys/filesys.h"
#include <debug.h>
#include <stdio.h>
#include <string.h>
#include "filesys/file.h"
#include "filesys/free-map.h"
#include "filesys/inode.h"
#include "filesys/directory.h"
#include "threads/thread.h"

/* Partition that contains the file system. */
struct block *fs_device;
struct dir* get_final_dir(const char* path);
char* get_filename(const char* path);

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

/* Creates a file named NAME with the given INITIAL_SIZE.
   Returns true if successful, false otherwise.
   Fails if a file named NAME already exists,
   or if internal memory allocation fails. */
bool
filesys_create (const char *name, off_t initial_size) 
{
  block_sector_t inode_sector = 0;
  struct dir *dir = dir_open_root ();
  bool success = (dir != NULL
                  && free_map_allocate (1, &inode_sector)
                  && inode_create (inode_sector, initial_size)
                  && dir_add (dir, name, inode_sector));
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
  struct dir *dir = dir_open_root ();
  struct inode *inode = NULL;

  if (dir != NULL)
    dir_lookup (dir, name, &inode);
  dir_close (dir);

  return file_open (inode);
}

/* Deletes the file named NAME.
   Returns true if successful, false on failure.
   Fails if no file named NAME exists,
   or if an internal memory allocation fails. */
bool
filesys_remove (const char *name) 
{
  struct dir *dir = dir_open_root ();
  bool success = dir != NULL && dir_remove (dir, name);
  dir_close (dir); 

  return success;
}

/* Formats the file system. */
static void
do_format (void)
{
  printf ("Formatting file system...");
  free_map_create ();
  if (!dir_create (ROOT_DIR_SECTOR, 16))
    PANIC ("root directory creation failed");
  free_map_close ();
  printf ("done.\n");
}

struct dir* get_final_dir (const char* path)
{
    int path_length = strlen(path);
    struct dir* dir;

    if(path_length == 0 || thread_current()->dir == NULL)
        return NULL;

    char path_copy[path_length+1];
    memcpy(path_copy, path, path_length+1);

    if(path_copy[0] == '/')
        dir = dir_open_root();
    else
        dir = dir_reopen(thread_current()->dir);

    char *save_ptr, *next = NULL, *token = strtok_r(path_copy, "/", &save_ptr);

    if (token)
        next = strtok_r(NULL, "/", &save_ptr);

    while(true)
    {
        if(next == NULL)
            break;

        if(strcmp(token, ".") != 0)
        {
            struct inode *inode;
            
            if(strcmp(token, "..") == 0)
            {
                if(!dir_get_parent(dir, &inode))
                    return NULL;
             }
            else
            {
                if (!dir_lookup(dir, token, &inode))
                {
                    dir_close(dir);
                    return NULL;
                }
            }
            if(inode->isdir)
            {
                dir_close(dir);
                dir = dir_open(inode);
            }
            else
                inode_close(inode);

        }
        token = next;
        next = strtok_r(NULL, "/", &save_ptr);
    }
        return dir;
}

char* get_filename (const char* path)
{
    if(path==NULL || strlen(path)==0)
        return NULL;

    char *ptr_to_slash = strrchr(path, '/');

    if(ptr_to_slash == NULL)
        return (char*) path;
    else
        return ptr_to_slash+1;
}
