#ifndef FILESYS_DIRECTORY_H
#define FILESYS_DIRECTORY_H

#include <stdbool.h>
#include <stddef.h>
#include "devices/block.h"
#include "filesys/file.h"

/* Maximum length of a file name component.
   This is the traditional UNIX maximum length.
   After directories are implemented, this maximum length may be
   retained, but much longer full path names must be allowed. */
#define NAME_MAX 14

struct inode;

/* Directory convenience methods. */
struct file *dir_open_root (void);
struct file *dir_open (struct file* dir, const char *name);
bool dir_is_empty (struct file *dir);

/* Reading and writing. */
bool dir_lookup (struct file *dir, const char *name, struct inode **);
bool dir_add (struct file *dir, const char *name, bool directory, size_t size);
bool dir_remove (struct file *file);
bool dir_readdir (struct file *dir, char name[NAME_MAX + 1]);

#endif /* filesys/directory.h */
