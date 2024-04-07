#include "filesys/directory.h"
#include <stdio.h>
#include <string.h>
#include <list.h>
#include "filesys/filesys.h"
#include "filesys/inode.h"
#include "threads/malloc.h"
#include "filesys/file.h"
#include "filesys/free-map.h"

/* A single directory entry. */
struct dir_entry 
  {
    block_sector_t inode_sector;        /* Sector number of this entry */
    char name[NAME_MAX + 1];            /* Null terminated file name. */
    bool in_use;                        /* In use or free? */
  };

/* Opens the root directory and returns a directory for it.
   Return true if successful, false on failure. */
struct file *
dir_open_root (void)
{
  return file_open (inode_open (ROOT_DIR_SECTOR));
}

struct file *
dir_open (struct file* dir, const char *name)
{
  ASSERT (dir != NULL);
  ASSERT (file_is_dir (dir));

  struct inode *inode = NULL;
  dir_lookup(dir, name, &inode);
  if (inode == NULL) {
    return NULL;
  }
  return file_open(inode);
}

/* Searches DIR for a file with the given NAME.
   If successful, returns true, sets *EP to the directory entry
   if EP is non-null, and sets *OFSP to the byte offset of the
   directory entry if OFSP is non-null.
   otherwise, returns false and ignores EP and OFSP. */
static bool
lookup (struct file *dir, const char *name, struct dir_entry *ep, off_t *ofsp) 
{
  struct dir_entry e;
  size_t ofs;
  
  ASSERT (dir != NULL);
  ASSERT (name != NULL);

  ASSERT (file_is_dir (dir));

  file_seek (dir, 0);
  for (ofs = 0; file_read_at (dir, &e, sizeof e, ofs) == sizeof e;
       ofs += sizeof e) 
    if (e.in_use && !strcmp (name, e.name)) 
      {
        if (ep != NULL)
          *ep = e;
        if (ofsp != NULL)
          *ofsp = ofs;
        return true;
      }
  return false;
}

bool
dir_is_empty (struct file *dir) 
{
  struct dir_entry e; 
  size_t ofs;
  
  ASSERT (dir != NULL);
  ASSERT (file_is_dir (dir));

  for (ofs = 0; file_read_at (dir, &e, sizeof e, ofs) == sizeof e;
       ofs += sizeof e) 
    if (e.in_use) 
      {
        return false;
      }
  return true;
}

/* Finds the dir entry given file handle FILE.
   If successful, returns true, and sets *EP to the directory entry.
   Otherwise, returns false and ignores EP. */
static bool
lookup_file (struct file *parent, struct file *file, struct dir_entry *ep, 
             off_t *ofsp) 
{
  struct dir_entry e;
  size_t ofs;
  
  ASSERT (file != NULL);
  ASSERT (parent != NULL);

  if (!file_is_dir(parent)) {
    return false;
  }

  block_sector_t file_sector = file_get_inumber(file);
  for (ofs = 0; file_read_at (parent, &e, sizeof e, ofs) == sizeof e;
       ofs += sizeof e) 
    if (e.in_use && file_sector == e.inode_sector) 
      {
        if (ep != NULL)
          *ep = e;
        if (ofsp != NULL)
          *ofsp = ofs;
        return true;
      }
  return false;
}

/* Searches DIR for a file with the given NAME
   and returns true if one exists, false otherwise.
   On success, sets *INODE to an inode for the file, otherwise to
   a null pointer.  The caller must close *INODE. */
bool
dir_lookup (struct file *dir, const char *name,
            struct inode **inode) 
{
  struct dir_entry e;

  ASSERT (dir != NULL);
  ASSERT (name != NULL);

  if (strcmp(name, ".") == 0) {
    *inode = inode_reopen(file_get_inode(dir));
    return true;
  } else if (strcmp(name, "..") == 0) {
    *inode = inode_get_parent(file_get_inode(dir));
    return true;
  }

  if (lookup (dir, name, &e, NULL))
    *inode = inode_open (e.inode_sector);
  else
    *inode = NULL;

  return *inode != NULL;
}

/* Adds a file named NAME to DIR, which must not already contain a
   file by that name.  The file's inode is in sector
   INODE_SECTOR.
   Returns true if successful, false on failure.
   Fails if NAME is invalid (i.e. too long) or a disk or memory
   error occurs. */
bool
dir_add (struct file *dir, const char *name, bool directory, size_t size)
{
  struct dir_entry e;
  off_t ofs;
  bool success = false;

  ASSERT (dir != NULL);
  ASSERT (name != NULL);

  ASSERT (file_is_dir (dir));

  /* Check NAME for validity. */
  if (*name == '\0' || strcmp(name, ".") == 0 
      || strcmp(name, "..") == 0 || strlen (name) > NAME_MAX)
    return false;

  /* Check that NAME is not in use. */
  if (lookup (dir, name, NULL, NULL))
    goto done;

  /* Set OFS to offset of free slot.
     If there are no free slots, then it will be set to the
     current end-of-file.
     
     inode_read_at() will only return a short read at end of file.
     Otherwise, we'd need to verify that we didn't get a short
     read due to something intermittent such as low memory. */
  for (ofs = 0; file_read_at (dir, &e, sizeof e, ofs) == sizeof e;
       ofs += sizeof e) 
    if (!e.in_use)
      break;

  /* Write slot. */
  e.in_use = true;
  strlcpy (e.name, name, sizeof e.name);
  if (!free_map_allocate(1, &e.inode_sector)) {
    goto done;
  }
  if (!inode_create (e.inode_sector, size, directory, file_get_inumber(dir))) {
    free_map_release (e.inode_sector, 1);
    goto done;
  }
  success = file_write_at (dir, &e, sizeof e, ofs) == sizeof e;

 done:
  return success;
}

/* Conviencence method to remove any entry for FILE in its parent 
   directory. Takes ownership of file and closes it.
   Returns true if successful, false on failure,
   which occurs only if there is no file with the given NAME. */
bool
dir_remove (struct file *file) 
{
  struct dir_entry e;
  struct inode *inode = NULL;
  bool success = false;
  off_t ofs;
  struct file* parent = NULL;

  ASSERT (file != NULL);

  if (file_is_dir(file) && (!dir_is_empty(file) || file_is_root(file))) {
    goto release;
  }

  parent = file_get_parent(file);

  /* Find directory entry. */
  if (!lookup_file (parent, file, &e, &ofs))
    goto release;

  /* Open inode. */
  inode = inode_open (e.inode_sector);
  if (inode == NULL)
    goto release;

  /* Erase directory entry. */
  e.in_use = false;
  if (file_write_at (parent, &e, sizeof e, ofs) != sizeof e) 
    goto release;

  /* Remove inode. */
  inode_remove (inode);
  success = true;

 release:
  file_close(parent);
  file_close(file);
  inode_close (inode);
  return success;
}


/* Reads the next directory entry in DIR and stores the name in
   NAME.  Returns true if successful, false if the directory
   contains no more entries. */
bool
dir_readdir (struct file *dir, char name[NAME_MAX + 1])
{
  struct dir_entry e;
  while (file_read (dir, &e, sizeof e) == sizeof e) 
    {
      if (e.in_use)
        {
          strlcpy (name, e.name, NAME_MAX + 1);
          return true;
        } 
    }
  return false;
}
