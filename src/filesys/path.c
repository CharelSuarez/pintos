#include "filesys/path.h"
#include <string.h>
#include "threads/thread.h"
#include "filesys/directory.h"
#include "filesys/inode.h"

#define MAX_PATH_LENGTH 1024

void path_get_file(const char *_path, struct file **_file, struct dir **_dir) {
    size_t length = strnlen(_path, MAX_PATH_LENGTH);
    if (length == 0 || length >= MAX_PATH_LENGTH) {
        return;
    }
    char path[1024];
    strlcpy(path, _path, length + 1); 

    char* path_start = path;
    // The initial directory is the working directory.
    struct dir *dir = dir_reopen(thread_current()->current_dir);

    if (strcmp(path, ".") == 0) {
        if (_dir != NULL) {
            *_dir = dir_reopen(dir);
        } else {
            dir_close(dir);
        }
        return;
    }

    /* "/" represents the root directory. */
    if (path[0] == '/') {
        dir = dir_open_root();
        path_start++;
    /* "." or "./" represents the current directory. */
    } else if (length >= 2 && path[0] == '.' && path[1] == '/') {
        path_start += 2;
    }

    while (true) {
        struct inode *inode;
        char *next = strchr(path_start, '/');
        if (next == NULL) {
            // The end of the path is a file.
            dir_lookup(dir, path_start, &inode);
            dir_close(dir);
            if (_file != NULL) {
                *_file = file_open(inode);
            } else {
                inode_close(inode);
            }
            return;
        }
        // Ignore any double slash.
        if (next == path_start) {
            path_start++;
            continue;
        }
        *next = '\0';
        // Find the next directory.
        if (!dir_lookup(dir, path_start, &inode)) {
            return;
        }
        struct dir *next_dir = dir_open(inode);
        if (next_dir == NULL) {
            return;
        }
        dir_close(dir);
        dir = next_dir;
        path_start = next + 1;
    }

    if (_dir != NULL) {
        *_dir = dir;
    } else {
        dir_close(dir);
    }
}