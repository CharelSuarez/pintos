#include "filesys/filesys.h"
#include <debug.h>
#include <stdio.h>
#include <string.h>
#include "filesys/file.h"
#include "filesys/free-map.h"
#include "filesys/inode.h"
#include "filesys/directory.h"
#include "filesys/path.h"

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

/* Creates a directory at the given PATH with the given INITIAL_SIZE.
   Returns true if successful, false otherwise.
   Fails if a file named NAME already exists,
   or if internal memory allocation fails. */
bool
filesys_create (const char *path, off_t initial_size) 
{
  struct file *file = path_create_file(path, false, initial_size);
  if (file == NULL) {
    return false;
  }
  file_close(file);
  return true;
}

/* Creates a directory at the given PATH with the given INITIAL_SIZE.
   The PATH is interpreted as a directory, regardless if
   the path ends with '/' or not.
   Returns true if successful, false otherwise.
   Fails if a file named NAME already exists,
   or if internal memory allocation fails. */
bool
filesys_create_dir (const char *path, off_t initial_size) 
{
  struct file *file = path_create_file(path, true, initial_size);
  if (file == NULL) {
    return false;
  }
  file_close(file);
  return true;
}

/* Opens the file or directory with the given NAME.
   Returns the new file if successful or a null pointer
   otherwise.
   Fails if no file named NAME exists,
   or if an internal memory allocation fails. */
struct file *
filesys_open (const char *path)
{
  return path_get_file(path, false);
}

/* Opens the directory with the given PATH.
   The PATH is interpreted as a directory, regardless if
   the path ends with '/' or not.
   Returns the new directory if successful or a null pointer
   otherwise.
   Fails if no directory named NAME exists,
   or if an internal memory allocation fails. */
struct file *
filesys_open_dir (const char *path)
{
  return path_get_file(path, true);
}

/* Deletes the file named NAME.
   Returns true if successful, false on failure.
   Fails if no file named NAME exists,
   or if an internal memory allocation fails. */
bool
filesys_remove (const char *name) 
{ 
  struct file *file = path_get_file(name, false);
  if (file == NULL) {
    return false;
  }
  return dir_remove(file);
}

/* Formats the file system. */
static void
do_format (void)
{
  printf ("Formatting file system...");
  free_map_create ();
  if (!inode_create (ROOT_DIR_SECTOR, 0, true, ROOT_DIR_SECTOR))
    PANIC ("root directory creation failed");
  free_map_close ();
  printf ("done.\n");
}
