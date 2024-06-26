#include "filesys/inode.h"
#include <list.h>
#include <debug.h>
#include <round.h>
#include <string.h>
#include "filesys/filesys.h"
#include "filesys/free-map.h"
#include "threads/malloc.h"
#include <stdio.h>
#include "threads/synch.h"

/* Identifies an inode. */
#define INODE_MAGIC 0x494e4f44

#define DIRECT_BLOCKS ((size_t) 122)
#define INDIRECT_BLOCKS ((size_t) 128)
#define DOUBLE_INDIRECT_BLOCKS (INDIRECT_BLOCKS * INDIRECT_BLOCKS)
#define MAX_FILE_SIZE (DIRECT_BLOCKS + INDIRECT_BLOCKS + \
                       DOUBLE_INDIRECT_BLOCKS) * BLOCK_SECTOR_SIZE

/* On-disk inode.
   Must be exactly BLOCK_SECTOR_SIZE bytes long. */
struct inode_disk
  {
    off_t length;                       /* File size in bytes. */
    unsigned magic;                     /* Magic number. */
    bool directory;                     /* If this inode is a directory. */
    block_sector_t parent;              /* Parent directory. */

    /* Indexed file blocks. */
    block_sector_t direct_blocks[DIRECT_BLOCKS];
    block_sector_t indirect_block;
    block_sector_t double_indirect_block;
  };

/* On-disk indirect data block.
   Must be exactly BLOCK_SECTOR_SIZE bytes long. */
struct indirect_block
  {
    block_sector_t blocks[INDIRECT_BLOCKS];
  };

/* Returns the number of sectors to allocate for an inode SIZE
   bytes long. */
static inline size_t
bytes_to_sectors (off_t size)
{
  return DIV_ROUND_UP (size, BLOCK_SECTOR_SIZE);
}

/* In-memory inode. */
struct inode 
  {
    struct list_elem elem;              /* Element in inode list. */
    block_sector_t sector;              /* Sector number of disk location. */
    int open_cnt;                       /* Number of openers. */
    bool removed;                       /* True if deleted, false otherwise. */
    int deny_write_cnt;                 /* 0: writes ok, >0: deny writes. */
    struct inode_disk data;             /* Inode content. */
    struct lock lock;                   /* Filesystem lock for this inode. */
  };

/* List of open inodes, so that opening a single inode twice
   returns the same `struct inode'. */
static struct list open_inodes;
/* Bounce buffer of size BLOCK_SECTOR_SIZE. */
static void* bounce;
/* A global lock to synchronize the inodes list. */
static struct lock inodes_list_lock;

static struct inode *_inode_reopen (struct inode *inode, bool owns_lock);

/* Initializes the inode module. */
void
inode_init (void) 
{
  list_init (&open_inodes);
  lock_init (&inodes_list_lock);
  bounce = malloc (BLOCK_SECTOR_SIZE);
}

static block_sector_t
_byte_to_sector (struct inode_disk* data, off_t offset, bool create) {
  struct indirect_block* indirect_block = NULL;
  struct indirect_block* double_indirect_block = NULL;
  block_sector_t sector_idx = 0;

  size_t index = offset / BLOCK_SECTOR_SIZE;
  if (index < DIRECT_BLOCKS) {
    block_sector_t* direct_block_sector = &data->direct_blocks[index];
    if (*direct_block_sector == 0) {
      if (!create) {
        goto release;
      }
      // Create new direct block, and initialize it.
      free_map_allocate (1, direct_block_sector);
      memset (bounce, 0, BLOCK_SECTOR_SIZE);
      block_write (fs_device, *direct_block_sector, bounce);
    }
    sector_idx = *direct_block_sector;
  } else if (index < DIRECT_BLOCKS + INDIRECT_BLOCKS) {
    index -= DIRECT_BLOCKS;
    indirect_block = malloc(BLOCK_SECTOR_SIZE);
    if (data->indirect_block == 0) {
      if (!create) {
        goto release;
      }
      free_map_allocate (1, &data->indirect_block);
      memset (indirect_block, 0, BLOCK_SECTOR_SIZE); 
      block_write (fs_device, data->indirect_block, indirect_block);
    } else {
      block_read (fs_device, data->indirect_block, indirect_block);
    }
    block_sector_t* direct_block_sector = &indirect_block->blocks[index];
    if (*direct_block_sector == 0) {
      if (!create) {
        goto release;
      }
      // Create new direct block, and initialize it.
      free_map_allocate (1, direct_block_sector);
      memset (bounce, 0, BLOCK_SECTOR_SIZE);
      block_write (fs_device, *direct_block_sector, bounce);

      // Update indirect block on disk.
      block_write (fs_device, data->indirect_block, indirect_block);
    }
    sector_idx = *direct_block_sector;
  } else if (index < DIRECT_BLOCKS + INDIRECT_BLOCKS + DOUBLE_INDIRECT_BLOCKS) {
    index -= (DIRECT_BLOCKS + INDIRECT_BLOCKS);
    double_indirect_block = malloc(BLOCK_SECTOR_SIZE);
    if (data->double_indirect_block == 0) {
      if (!create) {
        goto release;
      }
      free_map_allocate (1, &data->double_indirect_block);
      memset (double_indirect_block, 0, BLOCK_SECTOR_SIZE);
      block_write (fs_device, data->double_indirect_block, 
        double_indirect_block);
    } else {
      block_read (fs_device, data->double_indirect_block, 
        double_indirect_block);
    }
    size_t double_indirect_index = index / INDIRECT_BLOCKS;
    block_sector_t* indirect_block_sector = 
      &double_indirect_block->blocks[double_indirect_index];
    indirect_block = malloc(BLOCK_SECTOR_SIZE);
    if (*indirect_block_sector == 0) {
      if (!create) {
        goto release;
      }
      // Create new indirect block, and initialize it to SECTOR_NULL.
      free_map_allocate (1, indirect_block_sector);
      memset(indirect_block, 0, BLOCK_SECTOR_SIZE);
      block_write (fs_device, *indirect_block_sector, indirect_block);

      // Update double indirect block on disk.
      block_write (fs_device, data->double_indirect_block, 
        double_indirect_block);
    } else {
      block_read (fs_device, *indirect_block_sector, indirect_block);
    }
    size_t indirect_index = index % INDIRECT_BLOCKS;
    block_sector_t* direct_block_sector = 
      &indirect_block->blocks[indirect_index];
    if (*direct_block_sector == 0) {
      if (!create) {
        goto release;
      }
      // Create new direct block, and initialize it.
      free_map_allocate (1, direct_block_sector);
      memset (bounce, 0, BLOCK_SECTOR_SIZE);
      block_write (fs_device, *direct_block_sector, bounce);

      // Update the indirect block on disk.
      block_write (fs_device, *indirect_block_sector, indirect_block);
    }
    sector_idx = *direct_block_sector;
  }

release:
  free(indirect_block);
  free(double_indirect_block);
  return sector_idx; 
}

/* Returns the block device sector that contains byte offset POS
   within INODE.
   Returns 0 if INODE does not contain data for a byte at offset
   POS. */
static block_sector_t
byte_to_sector (struct inode *inode, off_t offset, bool create) {
  if (!inode_is_dir(inode)) {
      lock_acquire(&inode->lock);
  }
  block_sector_t sector = _byte_to_sector(&inode->data, offset, create);
  block_write (fs_device, inode->sector, &inode->data);
  if (!inode_is_dir(inode)) {
      lock_release(&inode->lock);
  }
  return sector;
}

/* Initializes an inode with LENGTH bytes of data and
   writes the new inode to sector SECTOR on the file system
   device.
   Returns true if successful.
   Returns false if memory or disk allocation fails. */
bool
inode_create (block_sector_t sector, off_t length, bool directory, 
              block_sector_t parent)
{
  struct inode_disk *disk_inode = NULL;
  bool success = false;

  ASSERT (length >= 0);

  /* If this assertion fails, the inode structure is not exactly
     one sector in size, and you should fix that. */
  ASSERT (sizeof *disk_inode == BLOCK_SECTOR_SIZE);

  disk_inode = calloc (1, BLOCK_SECTOR_SIZE);
  if (disk_inode != NULL)
    {
      disk_inode->magic = INODE_MAGIC;
      disk_inode->directory = directory;
      disk_inode->parent = parent;
      disk_inode->length = length;
      block_write (fs_device, sector, disk_inode);
      success = true;
      free (disk_inode);
    }
  return success;
}

/* Reads an inode from SECTOR
   and returns a `struct inode' that contains it.
   Returns a null pointer if memory allocation fails. */
struct inode *
inode_open (block_sector_t sector)
{
  struct list_elem *e;
  struct inode *inode;

  lock_acquire(&inodes_list_lock);

  /* Check whether this inode is already open. */
  for (e = list_begin (&open_inodes); e != list_end (&open_inodes);
       e = list_next (e)) 
    {
      inode = list_entry (e, struct inode, elem);
      if (inode->sector == sector) 
        {
          // If this inode is removed, this could return NULL.
          inode = _inode_reopen(inode, true); 
          goto release;
        }
    }

  /* Allocate memory. */
  inode = malloc (sizeof *inode);
  if (inode == NULL)
    goto release;

  /* Initialize. */
  list_push_front (&open_inodes, &inode->elem);
  inode->sector = sector;
  inode->open_cnt = 1;
  inode->deny_write_cnt = 0;
  inode->removed = false;
  lock_init (&inode->lock);
  block_read (fs_device, inode->sector, &inode->data);
release:

  lock_release(&inodes_list_lock);
  return inode;
}

/* Internal method which can be used if the caller owns the
   inode list lock.
   Reopens and returns INODE, or NULL if unsuccessful. */
static struct inode *
_inode_reopen (struct inode *inode, bool owns_lock)
{
  if (inode == NULL) {
    return NULL;
  }

  if (owns_lock) {
    ASSERT (lock_held_by_current_thread(&inodes_list_lock));
  } else {
    lock_acquire(&inodes_list_lock);
  }

  if (inode->removed) {
    inode = NULL;
    goto release;
  }
  inode->open_cnt++;

release:
  if (!owns_lock) {
    lock_release(&inodes_list_lock);
  }
  return inode;
}

/* Reopens and returns INODE. */
struct inode *
inode_reopen (struct inode *inode)
{
  return _inode_reopen(inode, false);
}

/* Returns INODE's inode number. */
block_sector_t
inode_get_inumber (const struct inode *inode)
{
  return inode->sector;
}

static void
_release_all_blocks(struct inode* inode) {
  // TODO Only clear up to the length of the file!
  // size_t length = inode->data.length;
  // size_t sectors = bytes_to_sectors(length);

  // Free all direct, indirect, and double direct blocks.
  for (size_t i = 0; i < DIRECT_BLOCKS; i++) {
    if (inode->data.direct_blocks[i] != 0) {
      free_map_release (inode->data.direct_blocks[i], 1);
    }
  }
  if (inode->data.indirect_block != 0) {
    struct indirect_block indirect_block;
    block_read (fs_device, inode->data.indirect_block, &indirect_block);
    for (size_t i = 0; i < INDIRECT_BLOCKS; i++) {
      if (indirect_block.blocks[i] != 0) {
        free_map_release (indirect_block.blocks[i], 1);
      }
    }
    free_map_release (inode->data.indirect_block, 1);
  }
  if (inode->data.double_indirect_block != 0) {
    struct indirect_block double_indirect_block;
    block_read (fs_device, inode->data.double_indirect_block, &double_indirect_block);
    for (size_t i = 0; i < INDIRECT_BLOCKS; i++) {
      if (double_indirect_block.blocks[i] != 0) {
        struct indirect_block indirect_block;
        block_read (fs_device, double_indirect_block.blocks[i], &indirect_block);
        for (size_t j = 0; j < INDIRECT_BLOCKS; j++) {
          if (indirect_block.blocks[j] != 0) {
            free_map_release (indirect_block.blocks[j], 1);
          }
        }
        free_map_release (double_indirect_block.blocks[i], 1);
      }
    }
    free_map_release (inode->data.double_indirect_block, 1);
  }
}

/* Closes INODE and writes it to disk.
   If this was the last reference to INODE, frees its memory.
   If INODE was also a removed inode, frees its blocks. */
void
inode_close (struct inode *inode) 
{
  /* Ignore null pointer. */
  if (inode == NULL)
    return;

  block_write (fs_device, inode->sector, &inode->data); // TODO Needed?

  lock_acquire(&inodes_list_lock);
  /* Release resources if this was the last opener. */
  if (--inode->open_cnt == 0)
    {
      /* Remove from inode list and release lock. */
      list_remove (&inode->elem);
      lock_release(&inodes_list_lock); 
 
      /* Deallocate blocks if removed. */
      if (inode->removed) 
        {
          free_map_release (inode->sector, 1);
          _release_all_blocks(inode);
        } else { 
          /* Write to disk. */
          // block_write (fs_device, inode->sector, &inode->data);
      }

      free (inode); 
    }
  else {
    lock_release(&inodes_list_lock);
  }
}

/* Marks INODE to be deleted when it is closed by the last caller who
   has it open. */
void
inode_remove (struct inode *inode) 
{
  ASSERT (inode != NULL);
  lock_acquire(&inodes_list_lock);
  inode->removed = true;
  lock_release(&inodes_list_lock);
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
      block_sector_t sector_idx = byte_to_sector(inode, offset, false);
      int sector_ofs = offset % BLOCK_SECTOR_SIZE; 

      /* Bytes left in inode, bytes left in sector, lesser of the two. */
      off_t inode_left = inode_length (inode) - offset;
      int sector_left = BLOCK_SECTOR_SIZE - sector_ofs;
      int min_left = inode_left < sector_left ? inode_left : sector_left;

      /* Number of bytes to actually copy out of this sector. */
      int chunk_size = size < min_left ? size : min_left;
      if (chunk_size <= 0)
        break;

      if (sector_idx == 0) 
        {
          /* This sector of the file is sparse, fill with zeros. */
          memset (buffer + bytes_read, 0, chunk_size);
        }
      else if (sector_ofs == 0 && chunk_size == BLOCK_SECTOR_SIZE)
        {
          /* Read full sector directly into caller's buffer. */
          block_read (fs_device, sector_idx, buffer + bytes_read);
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
   less than SIZE if end of file is reached or an error occurs.
   (Normally a write at end of file would extend the inode, but
   growth is not yet implemented.) */
off_t
inode_write_at (struct inode *inode, const void *buffer_, off_t size,
                off_t offset) 
{
  const uint8_t *buffer = buffer_;
  off_t bytes_written = 0;
  uint8_t *bounce = NULL;

  if (inode->deny_write_cnt)
    return 0;

  while (size > 0) 
    {
      /* Sector to write, starting byte offset within sector. */
      block_sector_t sector_idx = byte_to_sector(inode, offset, true);
      if (sector_idx == 0) {
        break;
      }
      int sector_ofs = offset % BLOCK_SECTOR_SIZE;

      /* Bytes in max file size, bytes left in sector, lesser of the two. */
      off_t inode_left = MAX_FILE_SIZE - offset;
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
          /* Read sector into bounce buffer, then partially copy
             into caller's buffer. */
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

  // The file has grown, update its length.
  if (offset > inode_length (inode)) {
    // if (!inode_is_dir(inode)) {
    //   lock_acquire(&inode->lock);
    // }
    inode->data.length = offset;
    block_write (fs_device, inode->sector, &inode->data);
    // if (!inode_is_dir(inode)) {
    //   lock_release(&inode->lock);
    // }
  }

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

/* Returns true if INODE is a directory. */
bool
inode_is_dir (const struct inode *inode)
{
  return inode->data.directory;
}

/* Returns INODE's parent inode. */
struct inode*
inode_get_parent (const struct inode *inode)
{
  return inode_open(inode->data.parent);
}

bool
inode_is_root (const struct inode *inode)
{
  return inode->sector == ROOT_DIR_SECTOR; 
}

void
inode_lock (struct inode* inode) {
  // lock_acquire(&inode->lock);
}

void
inode_unlock (struct inode* inode) {
  // lock_release(&inode->lock);
}