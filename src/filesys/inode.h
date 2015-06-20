#ifndef FILESYS_INODE_H
#define FILESYS_INODE_H

#include <stdbool.h>
#include "filesys/off_t.h"
#include "devices/block.h"
#include <list.h>

struct bitmap;

/* Size Attributes */
#define DIR_BLOCKS 12
#define INDIR_BLOCKS 1
#define DINDIR_BLOCKS 1

/* 4 bytes pointers on a 512 bytes sector */
#define INDIR_BLOCK_PTRS 128

/* On-disk inode. Must be exactly BLOCK_SECTOR_SIZE(512) bytes long. */
struct inode_disk
  {
    off_t length;                       /* File size in bytes. */
    unsigned magic;                     /* Magic number. */
    block_sector_t sector;              /* Location of itself */
    bool isdir;

    /* Data blocks */
    off_t dir_cnt;
    block_sector_t direct[DIR_BLOCKS]; 
    off_t indir_cnt;
    off_t indir_curr_usage;
    block_sector_t indirect[INDIR_BLOCKS];   /* Single indirect */
    off_t dindir_cnt;
    off_t dindir_curr_usage;
    off_t dindir_lv2_curr_usage;
    block_sector_t dindirect[DINDIR_BLOCKS]; /* Double indirect */
    
    uint32_t unused[104];               /* Not used. */
  };

/* In-memory inode. */
struct inode 
  {
    struct list_elem elem;              /* Element in inode list. */
    block_sector_t sector;              /* Sector number of disk location. */
    int open_cnt;                       /* Number of openers. */
    bool removed;                       /* True if deleted, false otherwise. */
    int deny_write_cnt;                 /* 0: writes ok, >0: deny writes. */
    struct inode_disk data;             /* Inode content. */
  };


void inode_init (void);
bool inode_create (block_sector_t, off_t);
struct inode *inode_open (block_sector_t);
struct inode *inode_reopen (struct inode *);
block_sector_t inode_get_inumber (const struct inode *);
void inode_close (struct inode *);
void inode_remove (struct inode *);
off_t inode_read_at (struct inode *, void *, off_t size, off_t offset);
off_t inode_write_at (struct inode *, const void *, off_t size, off_t offset);
void inode_deny_write (struct inode *);
void inode_allow_write (struct inode *);
off_t inode_length (const struct inode *);

#endif /* filesys/inode.h */
