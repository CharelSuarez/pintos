#include "filesys/path.h"
#include <string.h>
#include "threads/thread.h"
#include "filesys/directory.h"
#include "filesys/inode.h"
#include "filesys/filesys.h"
#include <stdio.h>

#define MAX_PATH_LENGTH 1024

static struct file* 
path_get_create_file(const char *_path, bool as_dir, bool create, size_t size);

/* Gets the file at the given path, or NULL if it does not exist.
   If AS_DIR is specified, then it looks for a directory,
   regardless if the path ends with '/' or not.
*/
struct file* 
path_get_file(const char *_path, bool as_dir) {
    return path_get_create_file(_path, as_dir, false, 0);
}

/* Creates a file at the given path, with size SIZE.
   If AS_DIR is specified, then the file is always created as a directory,
   regardless of if the path ends with '/' or not.
*/
struct file* 
path_create_file(const char *_path, bool as_dir, size_t size) {
    return path_get_create_file(_path, as_dir, true, size);
}

/* Adds a file (or directory, if DIRECTORY) to directory DIR 
   with NAME and SIZE, and returns a new handle.
   Takes ownership of DIR and closes it. */
static struct file*
add_file(struct file* dir, const char* name, bool directory, size_t size) {
    if (!dir_add(dir, name, directory, size)) {
        file_close(dir);
        return NULL;
    }
    struct file* file = dir_open(dir, name);
    file_close(dir);
    return file;
}

static struct file* 
path_get_create_file(const char *_path, bool as_dir, 
                     bool create, size_t size) {
    size_t length = strnlen(_path, MAX_PATH_LENGTH);
    if (length == 0 || length >= MAX_PATH_LENGTH) {
        // PANIC("CHECKPOINT -1");
        return NULL;
    }
    char path[1024];
    strlcpy(path, _path, length + 1); 

    char* currPath = path;
    // The initial directory is the working directory.
    struct file *dir = file_reopen(thread_current()->working_dir);
    if (dir == NULL) {
        return NULL;
    } 

    /* "/" represents the root directory. */
    if (path[0] == '/') {
        file_close(dir); 
        dir = dir_open_root();
        currPath++;
    /* "." or "./" represents the current directory. */
    } else if (length >= 2 && path[0] == '.' && path[1] == '/') {
        currPath += 2;
    }

    // If the rest of the path is empty, stop here.
    if (currPath == path + length) {
        return dir;
    }

    while (true) {
        char *nextSlash = strchr(currPath, '/');
        // This is the end of the path.
        if (nextSlash == NULL && create && !as_dir) {
            // Create a new file.
            return add_file(dir, currPath, false, size);
        }
        if (nextSlash != NULL){
            *nextSlash = '\0';
        }
        // Ignore any double slash.
        if (nextSlash == currPath) {
            currPath++;
            continue;
        }
        // If this is the last directory, we may have to create it.
        if (create && (nextSlash == path + length - 1 || 
                      (nextSlash == NULL && as_dir))) {
            return add_file(dir, currPath, true, size);
        }
        // Find the next file or directory.
        struct inode *inode;
        bool foundNext = dir_lookup(dir, currPath, &inode);
        file_close(dir);
        if (!foundNext) {
            return NULL;
        }
        struct file *nextFile = file_open(inode);
        if (nextFile == NULL) {
            return NULL;
        }
        // If this is the end of the path, return the file or directory.
        if (nextSlash == NULL) {
            return nextFile;
        }
        // If this isn't the end of the path, this can't be a file.
        if (!file_is_dir(nextFile)) {
            file_close(nextFile);
            return NULL;
        }
        // Otherwise, keep going.
        dir = nextFile;
        currPath = nextSlash + 1;
    }

    return NULL;
}