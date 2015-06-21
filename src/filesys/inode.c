#include "filesys/inode.h"
#include <list.h>
#include <debug.h>
#include <round.h>
#include <string.h>
#include "filesys/filesys.h"
#include "filesys/free-map.h"
#include "threads/malloc.h"

/* Identifies an inode. */
#define INODE_MAGIC 0x494e4f44

off_t dinode_extend (struct inode_disk *dinode, off_t new_length);
void dinode_free (struct inode_disk *dinode);

struct indir_block 
{
  block_sector_t ptr[INDIR_BLOCK_PTRS];
};

/* Returns the number of total data sectors to allocate for an inode SIZE bytes long. */
static inline size_t
bytes_to_total_sectors (off_t size)
{
  return DIV_ROUND_UP (size, BLOCK_SECTOR_SIZE);
}

/* Returns the block device sector that contains byte offset POS within INODE.
   Returns -1 if INODE does not contain data for a byte at offset POS. */
static block_sector_t
byte_to_sector (const struct inode *inode, off_t pos) 
{
  ASSERT (inode != NULL);
  if (pos < inode->data.length && pos >= 0) 
  {
    if (pos < BLOCK_SECTOR_SIZE * DIR_BLOCKS)
    {
      return inode->data.direct[pos / BLOCK_SECTOR_SIZE];
    }
    /* Single indirect */
    else if (pos < BLOCK_SECTOR_SIZE * (DIR_BLOCKS + INDIR_BLOCK_PTRS * INDIR_BLOCKS)) 
    {
      pos -= BLOCK_SECTOR_SIZE * DIR_BLOCKS; 
      uint32_t indir_idx = pos / (BLOCK_SECTOR_SIZE * INDIR_BLOCK_PTRS);
      uint32_t indir_block[INDIR_BLOCK_PTRS];

      /* Fetch indirect block contents */
      block_read (fs_device, inode->data.indirect[indir_idx], &indir_block);

      pos %= BLOCK_SECTOR_SIZE * INDIR_BLOCK_PTRS; /* Offset within a block */ 
      return indir_block[pos / BLOCK_SECTOR_SIZE];
    }
    /* Double indirect */
    else 
    {
      /* Fetch double indirect block contents */
      pos -= BLOCK_SECTOR_SIZE * (DIR_BLOCKS + INDIR_BLOCK_PTRS * INDIR_BLOCKS); 
      uint32_t indir_idx = pos / (BLOCK_SECTOR_SIZE * INDIR_BLOCK_PTRS * INDIR_BLOCK_PTRS);
      uint32_t indir_block[INDIR_BLOCK_PTRS];

      if (indir_idx < DINDIR_BLOCKS) block_read (fs_device, inode->data.dindirect[indir_idx], &indir_block);
      else return -1; /* Exceeded maximum file size. */

      pos %= BLOCK_SECTOR_SIZE * INDIR_BLOCK_PTRS * INDIR_BLOCK_PTRS;
      
      /* Fetch indirect block contents */
      indir_idx = pos / (BLOCK_SECTOR_SIZE * INDIR_BLOCK_PTRS);
      uint32_t indir_ptr = indir_block[indir_idx];

      block_read (fs_device, indir_ptr, &indir_block);

      pos %= BLOCK_SECTOR_SIZE * INDIR_BLOCK_PTRS; 
      return indir_block[pos / BLOCK_SECTOR_SIZE];
    }
  }
  else return -1;
}

/* List of open inodes, so that opening a single inode twice
   returns the same `struct inode'. */
static struct list open_inodes;

/* Initializes the inode module. */
void
inode_init (void) 
{
  list_init (&open_inodes);
}

/* Returns actual new length of the inode. It may differ from the given new_length if an error occurs. */
off_t dinode_extend (struct inode_disk *dinode, off_t new_length)
{
  static char zeros[BLOCK_SECTOR_SIZE] = {0}; 
  size_t new_data_sectors = bytes_to_total_sectors(new_length) - bytes_to_total_sectors(dinode->length);

  /* Contraction is not allowed.*/
  ASSERT (new_data_sectors >= 0);

  /* Extension in the same sector only modifies dinode->length information */
  if (new_data_sectors == 0) goto done; 

  /* Extension to direct block */
  while (dinode->dir_cnt < DIR_BLOCKS)
  {
    dinode->dir_cnt++;
    free_map_allocate (1, &dinode->direct[dinode->dir_cnt-1]);
    block_write(fs_device, dinode->direct[dinode->dir_cnt-1], zeros);
    new_data_sectors--;
    if (new_data_sectors == 0) goto done;
  }

  /* Extension to single indirect block */
  while (dinode->indir_cnt < INDIR_BLOCKS)
  {
    struct indir_block block; 

    /* No indirect block exists or current indirect block is full. */
    if (dinode->indir_cnt == 0 || dinode->indir_curr_usage == INDIR_BLOCK_PTRS)
    {
      dinode->indir_cnt++;
      free_map_allocate(1, &dinode->indirect[dinode->indir_cnt-1]);
      dinode->indir_curr_usage = 0;
    }
    
    block_read(fs_device, dinode->indirect[dinode->indir_cnt-1], &block);

    while (dinode->indir_curr_usage < INDIR_BLOCK_PTRS)
    {
      dinode->indir_curr_usage++;
      free_map_allocate(1, &block.ptr[dinode->indir_curr_usage-1]);
      block_write(fs_device, block.ptr[dinode->indir_curr_usage-1], zeros);
      new_data_sectors--;
      if (new_data_sectors == 0) break;
    }

    block_write(fs_device, dinode->indirect[dinode->indir_cnt-1], &block);
    if (new_data_sectors == 0) goto done;
  }

  /* Extension to double indirect block */
  while (dinode->dindir_cnt < DINDIR_BLOCKS)
  {
    struct indir_block d_block, block;

    /* No double indirect block exists or current block is full. */
    if (dinode->dindir_cnt == 0 || dinode->dindir_curr_usage == INDIR_BLOCK_PTRS)
    {
      dinode->dindir_cnt++;
      free_map_allocate(1, &dinode->dindirect[dinode->dindir_cnt-1]);
      dinode->dindir_curr_usage = 0;
    }
    
    block_read(fs_device, dinode->dindirect[dinode->dindir_cnt-1], &d_block);
    
    /* We've just got the level 1 block, so now we have to get the level 2 block. */

    while (dinode->dindir_curr_usage < INDIR_BLOCK_PTRS)
    { 
      /* No level 2 block exists or current level 2 block is full. */
      if (dinode->dindir_curr_usage == 0 || dinode->dindir_lv2_curr_usage == INDIR_BLOCK_PTRS)
      {
        dinode->dindir_curr_usage++;
        free_map_allocate(1, &d_block.ptr[dinode->dindir_curr_usage-1]);
        dinode->dindir_lv2_curr_usage = 0;
      }

      block_read(fs_device, d_block.ptr[dinode->dindir_curr_usage-1], &block);
      
      /* We've just got the level 2 block, so now we have to get the actual data block. */

      while (dinode->dindir_lv2_curr_usage < INDIR_BLOCK_PTRS)
      {
        dinode->dindir_lv2_curr_usage++;
        free_map_allocate(1, &block.ptr[dinode->dindir_lv2_curr_usage-1]);
        block_write(fs_device, block.ptr[dinode->dindir_lv2_curr_usage-1], zeros);
        new_data_sectors--;
        if (new_data_sectors == 0) break;
      } 
      
      /* writing back the level 2 block */

      block_write(fs_device, d_block.ptr[dinode->dindir_curr_usage-1], &block);
      if (new_data_sectors == 0) break;
    }
    
    /* writing back the level 1 block */

    block_write(fs_device, dinode->dindirect[dinode->dindir_cnt-1], &d_block);
    if (new_data_sectors == 0) goto done;
  }
  
  /* Immediately write back because there's no buffer cache. */
  /* This failure may happen when the given file size exceeds the maximum. */
  dinode->length = new_length - new_data_sectors*BLOCK_SECTOR_SIZE;
  block_write (fs_device, dinode->sector, dinode);
  return dinode->length;
  
done:
  dinode->length = new_length;
  block_write (fs_device, dinode->sector, dinode);
  return new_length;
}

void dinode_free (struct inode_disk *dinode)
{
  /* Free double indirect block */
  while (dinode->dindir_cnt != 0)
  {
    struct indir_block d_block, block;
    
    block_read(fs_device, dinode->dindirect[dinode->dindir_cnt-1], &d_block);
     /* We've just got the level 1 block, so now we have to free the level 2 block. */

    while (dinode->dindir_curr_usage != 0)
    { 
      block_read(fs_device, d_block.ptr[dinode->dindir_curr_usage-1], &block);
      /* We've just got the level 2 block, so now we have to free the actual data block. */

      while (dinode->dindir_lv2_curr_usage != 0)
      {
        free_map_release(block.ptr[dinode->dindir_lv2_curr_usage-1],1);
        dinode->dindir_lv2_curr_usage--;
      } 
      
      /* erase the level 2 block */
      free_map_release(d_block.ptr[dinode->dindir_curr_usage-1],1);
      dinode->dindir_curr_usage--;
      if(dinode->dindir_curr_usage != 0) dinode->dindir_lv2_curr_usage = INDIR_BLOCK_PTRS;
    }
    
    /* erase the level 1 block */
    free_map_release(dinode->dindirect[dinode->dindir_cnt-1],1);
    dinode->dindir_cnt--;
    if(dinode->dindir_cnt != 0) dinode->dindir_curr_usage = INDIR_BLOCK_PTRS;
  }
  
  /* Free single indirect block */
  while (dinode->indir_cnt != 0)
  {
    struct indir_block block; 

    block_read(fs_device, dinode->indirect[dinode->indir_cnt-1], &block);

    while (dinode->indir_curr_usage != 0)
    {
      free_map_release(block.ptr[dinode->indir_curr_usage-1],1);
      dinode->indir_curr_usage--;
    }

    free_map_release(dinode->indirect[dinode->indir_cnt-1],1);
    dinode->indir_cnt--;
    if(dinode->indir_cnt != 0) dinode->indir_curr_usage = INDIR_BLOCK_PTRS;
  }

  while (dinode->dir_cnt != 0)
  {
    free_map_release (dinode->direct[dinode->dir_cnt-1],1);
    dinode->dir_cnt--;
  }
}

/* Initializes an inode with LENGTH bytes of data and writes the new inode 
 * to sector SECTOR on the file system device. 
 * Returns true if successful. Returns false if any allocation fails. */
bool
inode_create (block_sector_t sector, off_t length, bool isdir)
{
  struct inode_disk *disk_inode = NULL;
  bool success = false;

  ASSERT (length >= 0);

  /* If this assertion below fails, the inode structure is not exactly
     one sector in size, and you should fix that. */
  ASSERT (sizeof *disk_inode == BLOCK_SECTOR_SIZE);

  disk_inode = calloc (1, sizeof *disk_inode);
  if (disk_inode != NULL)
  {
    disk_inode->magic = INODE_MAGIC;
    disk_inode->sector = sector;
    disk_inode->isdir = isdir;

    if (length == dinode_extend (disk_inode, length)) success = true;

    free (disk_inode);
  }
  return success;
}

/* Reads an inode from SECTOR and returns a `struct inode' that contains it.
   Returns a null pointer if memory allocation fails. */
struct inode *
inode_open (block_sector_t sector)
{
  struct list_elem *e;
  struct inode *inode;

  /* Check whether this inode is already open. */
  for (e = list_begin (&open_inodes); e != list_end (&open_inodes);
       e = list_next (e)) 
    {
      inode = list_entry (e, struct inode, elem);
      if (inode->sector == sector) 
        {
          inode_reopen (inode);
          return inode; 
        }
    }

  /* Allocate memory. */
  inode = malloc (sizeof *inode);
  if (inode == NULL)
    return NULL;

  /* Initialize. */
  list_push_front (&open_inodes, &inode->elem);
  inode->sector = sector;
  inode->open_cnt = 1;
  inode->deny_write_cnt = 0;
  inode->removed = false;
  block_read (fs_device, inode->sector, &inode->data);
  return inode;
}

/* Reopens and returns INODE. */
struct inode *
inode_reopen (struct inode *inode)
{
  if (inode != NULL)
    inode->open_cnt++;
  return inode;
}

/* Returns INODE's inode number. */
block_sector_t
inode_get_inumber (const struct inode *inode)
{
  return inode->sector;
}

/* Closes INODE. If this was the last reference to INODE, frees its memory.
   If INODE was also a removed inode, frees its blocks. 
   NOTE : writing back to disk was done at every moment, so there's no need to do that again.*/
void
inode_close (struct inode *inode) 
{
  /* Ignore null pointer. */
  if (inode == NULL)
    return;

  /* Release resources if this was the last opener. */
  if (--inode->open_cnt == 0)
  {
    /* Remove from inode list and release lock. */
    list_remove (&inode->elem);

    /* Deallocate all related blocks if removed. */
    if (inode->removed) 
    {
      free_map_release (inode->sector, 1);
      dinode_free(&inode->data);
    }
    
    free (inode); 
  }
}

/* Marks INODE to be deleted when it is closed by the last caller who
   has it open. */
void
inode_remove (struct inode *inode) 
{
  ASSERT (inode != NULL);
  inode->removed = true;
}

/* Reads SIZE bytes from INODE into BUFFER, starting at position OFFSET.
   Returns the number of bytes actually read, which may be less
   than SIZE if an error occurs or end of file is reached. */
off_t
inode_read_at (struct inode *inode, void *buffer_, off_t size, off_t offset) 
{
  uint8_t *buffer = buffer_;
  off_t bytes_read = 0;
  uint8_t *bounce = NULL;

  while (size > 0) 
    {
      /* Disk sector to read, starting byte offset within sector. */
      block_sector_t sector_idx = byte_to_sector (inode, offset);
      int sector_ofs = offset % BLOCK_SECTOR_SIZE;

      /* Bytes left in inode, bytes left in sector, lesser of the two.
       * If bytes left in inode (bytes needs to be read for this request) is smaller, 
       * then this is the last sector to read. */
      off_t inode_left = inode_length (inode) - offset;
      int sector_left = BLOCK_SECTOR_SIZE - sector_ofs;
      int min_left = inode_left < sector_left ? inode_left : sector_left;

      /* Number of bytes to actually copy out of this sector. 
       * In cases where size go beyond length of the actual data */
      int chunk_size = size < min_left ? size : min_left;
      if (chunk_size <= 0)
        break;

      if (sector_ofs == 0 && chunk_size == BLOCK_SECTOR_SIZE)
        {
          /* Read full sector directly into caller's buffer. */
          block_read (fs_device, sector_idx, buffer + bytes_read);
        }
      else 
        {
          /* Read sector into bounce buffer, then partially copy
             into caller's buffer. */
          if (bounce == NULL) 
            {
              bounce = malloc (BLOCK_SECTOR_SIZE);
              if (bounce == NULL)
                break;
            }
          block_read (fs_device, sector_idx, bounce);
          memcpy (buffer + bytes_read, bounce + sector_ofs, chunk_size);
        }
      
      /* Advance. */
      size -= chunk_size;
      offset += chunk_size;
      bytes_read += chunk_size;
    }
  free (bounce);

  return bytes_read;
}


/* Writes SIZE bytes from BUFFER into INODE, starting at OFFSET.
   Returns the number of bytes actually written, which may be
   less than SIZE if end of file is reached or an error occurs.*/
off_t
inode_write_at (struct inode *inode, const void *buffer_, off_t size, off_t offset)
{
  const uint8_t *buffer = buffer_;
  off_t bytes_written = 0;
  uint8_t *bounce = NULL;

  if (inode->deny_write_cnt)
    return 0;

  if (offset + size > inode_length(inode))
  {
    /* file extension needed */
    inode->data.length = dinode_extend (&inode->data, offset+size);
    if (inode_length(inode) != offset+size) return -1;
  }

  while (size > 0) 
    {
      /* Sector to write, starting byte offset within sector. */
      block_sector_t sector_idx = byte_to_sector (inode, offset);
      int sector_ofs = offset % BLOCK_SECTOR_SIZE;

      /* Bytes left in inode, bytes left in sector, lesser of the two. */
      off_t inode_left = inode_length (inode) - offset;
      int sector_left = BLOCK_SECTOR_SIZE - sector_ofs;
      int min_left = inode_left < sector_left ? inode_left : sector_left;

      /* Number of bytes to actually write into this sector. */
      int chunk_size = size < min_left ? size : min_left;
      if (chunk_size <= 0)
        break;

      if (sector_ofs == 0 && chunk_size == BLOCK_SECTOR_SIZE)
        {
          /* Write full sector directly to disk. */
          block_write (fs_device, sector_idx, buffer + bytes_written);
        }
      else 
        {
          /* We need a bounce buffer. */
          if (bounce == NULL) 
            {
              bounce = malloc (BLOCK_SECTOR_SIZE);
              if (bounce == NULL)
                break;
            }

          /* If the sector contains data before or after the chunk
             we're writing, then we need to read in the sector
             first.  Otherwise we start with a sector of all zeros. */
          if (sector_ofs > 0 || chunk_size < sector_left) 
            block_read (fs_device, sector_idx, bounce);
          else
            memset (bounce, 0, BLOCK_SECTOR_SIZE);
          memcpy (bounce + sector_ofs, buffer + bytes_written, chunk_size);
          block_write (fs_device, sector_idx, bounce);
        }

      /* Advance. */
      size -= chunk_size;
      offset += chunk_size;
      bytes_written += chunk_size;
    }
  free (bounce);

  return bytes_written;
}

/* Disables writes to INODE.
   May be called at most once per inode opener. */
void
inode_deny_write (struct inode *inode) 
{
  inode->deny_write_cnt++;
  ASSERT (inode->deny_write_cnt <= inode->open_cnt);
}

/* Re-enables writes to INODE.
   Must be called once by each inode opener who has called
   inode_deny_write() on the inode, before closing the inode. */
void
inode_allow_write (struct inode *inode) 
{
  ASSERT (inode->deny_write_cnt > 0);
  ASSERT (inode->deny_write_cnt <= inode->open_cnt);
  inode->deny_write_cnt--;
}

/* Returns the length, in bytes, of INODE's data. */
off_t
inode_length (const struct inode *inode)
{
  return inode->data.length;
}

/* Returns whether inode is directory or not */
bool
inode_is_dir (struct inode *inode)
{
  return inode->data.isdir;
}

/* Returns whether inode is removed or not */
bool
inode_is_removed (struct inode *inode)
{
  return inode->removed;
}

/* Returns the sector number of inode */
int
inode_number (struct inode *inode)
{
  return (int) inode->sector;
}
