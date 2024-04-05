#ifndef FILESYS_PATH_H
#define FILESYS_PATH_H

#include <stdbool.h>
#include "filesys/file.h"
#include "filesys/directory.h"

void path_get_file(const char *path, struct file **file, struct dir **dir);

#endif /* filesys/path.h */
