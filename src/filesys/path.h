#ifndef FILESYS_PATH_H
#define FILESYS_PATH_H

#include <stdbool.h>
#include "filesys/file.h"
#include "filesys/directory.h"

struct file* path_get_file(const char *_path, bool as_dir);
struct file* path_create_file(const char *_path, bool as_dir, size_t size);

#endif /* filesys/path.h */
