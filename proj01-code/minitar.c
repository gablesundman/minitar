#include <fcntl.h>
#include <grp.h>
#include <math.h>
#include <pwd.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <sys/types.h>
#include <unistd.h>
#include <stdlib.h>

#include "minitar.h"

#define NUM_TRAILING_BLOCKS 2
#define MAX_MSG_LEN 512

/*
 * Helper function to compute the checksum of a tar header block
 * Performs a simple sum over all bytes in the header in accordance with POSIX
 * standard for tar file structure.
 */
void compute_checksum(tar_header *header) {
    // Have to initially set header's checksum to "all blanks"
    memset(header->chksum, ' ', 8);
    unsigned sum = 0;
    char *bytes = (char *)header;
    for (int i = 0; i < sizeof(tar_header); i++) {
        sum += bytes[i];
    }
    snprintf(header->chksum, 8, "%07o", sum);
}

/*
 * Populates a tar header block pointed to by 'header' with metadata about
 * the file identified by 'file_name'.
 * Returns 0 on success or -1 if an error occurs
 */
int fill_tar_header(tar_header *header, const char *file_name) {
    memset(header, 0, sizeof(tar_header));
    char err_msg[MAX_MSG_LEN];
    struct stat stat_buf;
    // stat is a system call to inspect file metadata
    if (stat(file_name, &stat_buf) != 0) {
        snprintf(err_msg, MAX_MSG_LEN, "Failed to stat file %s", file_name);
        perror(err_msg);
        return -1;
    }

    strncpy(header->name, file_name, 100); // Name of the file, null-terminated string
    snprintf(header->mode, 8, "%07o", stat_buf.st_mode & 07777); // Permissions for file, 0-padded octal

    snprintf(header->uid, 8, "%07o", stat_buf.st_uid); // Owner ID of the file, 0-padded octal
    struct passwd *pwd = getpwuid(stat_buf.st_uid); // Look up name corresponding to owner ID
    if (pwd == NULL) {
        snprintf(err_msg, MAX_MSG_LEN, "Failed to look up owner name of file %s", file_name);
        perror(err_msg);
        return -1;
    }
    strncpy(header->uname, pwd->pw_name, 32); // Owner  name of the file, null-terminated string

    snprintf(header->gid, 8, "%07o", stat_buf.st_gid); // Group ID of the file, 0-padded octal
    struct group *grp = getgrgid(stat_buf.st_gid); // Look up name corresponding to group ID
    if (grp == NULL) {
        snprintf(err_msg, MAX_MSG_LEN, "Failed to look up group name of file %s", file_name);
        perror(err_msg);
        return -1;
    }
    strncpy(header->gname, grp->gr_name, 32); // Group name of the file, null-terminated string

    snprintf(header->size, 12, "%011o", (unsigned)stat_buf.st_size); // File size, 0-padded octal
    snprintf(header->mtime, 12, "%011o", (unsigned)stat_buf.st_mtime); // Modification time, 0-padded octal
    header->typeflag = REGTYPE; // File type, always regular file in this project
    strncpy(header->magic, MAGIC, 6); // Special, standardized sequence of bytes
    memcpy(header->version, "00", 2); // A bit weird, sidesteps null termination
    snprintf(header->devmajor, 8, "%07o", major(stat_buf.st_dev)); // Major device number, 0-padded octal
    snprintf(header->devminor, 8, "%07o", minor(stat_buf.st_dev)); // Minor device number, 0-padded octal

    compute_checksum(header);
    return 0;
}

/*
 * Removes 'nbytes' bytes from the file identified by 'file_name'
 * Returns 0 upon success, -1 upon error
 * Note: This function uses lower-level I/O syscalls (not stdio), which we'll learn about later
 */
int remove_trailing_bytes(const char *file_name, size_t nbytes) {
    char err_msg[MAX_MSG_LEN];
    // Note: ftruncate does not work with O_APPEND
    int fd = open(file_name, O_WRONLY);
    if (fd == -1) {
        snprintf(err_msg, MAX_MSG_LEN, "Failed to open file %s", file_name);
        perror(err_msg);
        return -1;
    }
    //  Seek to end of file - nbytes
    off_t current_pos = lseek(fd, -1 * nbytes, SEEK_END);
    if (current_pos == -1) {
        snprintf(err_msg, MAX_MSG_LEN, "Failed to seek in file %s", file_name);
        perror(err_msg);
        close(fd);
        return -1;
    }
    // Remove all contents of file past current position
    if (ftruncate(fd, current_pos) == -1) {
        snprintf(err_msg, MAX_MSG_LEN, "Failed to truncate file %s", file_name);
        perror(err_msg);
        close(fd);
        return -1;
    }
    if (close(fd) == -1) {
        snprintf(err_msg, MAX_MSG_LEN, "Failed to close file %s", file_name);
        perror(err_msg);
        return -1;
    }
    return 0;
}

/* 
 * iterate through all specified files
 * make a header for each -> fill_tar_header
 * write each file's header & contents in a sequence of 512-byte blocks
 * add two 512 byte blocks of footer on the end, or 1024 blank bytes
*/
int create_archive(const char *archive_name, const file_list_t *files) {
    //initialize current file, tar header, & error msg
    node_t *curr_file = files->head;
    tar_header curr_h;
    char err_msg[MAX_MSG_LEN];

    // open archive file
    FILE *f = fopen(archive_name, "w+");

    // iteratate through each file in list
    for(int i = 0; i < files->size; i++){
        // open file to read from & check for error
        FILE *read_file  = fopen(curr_file->name, "r");
        if(read_file == NULL){
            snprintf(err_msg, MAX_MSG_LEN, "One of the files to be added does not exist: %s", curr_file->name);
            perror(err_msg);
            fclose(f);
            return -1;
        }

        // create and write tar header into file
        fill_tar_header(&curr_h, curr_file->name);
        fwrite(&curr_h, sizeof(tar_header), 1, f);

        // intialize nbytes & buffer to read and write from file to archive file
        char buf[MAX_MSG_LEN];
        memset(buf,0,MAX_MSG_LEN);
        int nbytes;

        // while loop to read 512 bytes at a time & stop after a read returns less than that
        while((nbytes = fread(&buf, 1, MAX_MSG_LEN, read_file)) > 0){
            
            // sets string terminator & writes to archive file
            buf[nbytes] = '\0';
            fwrite(&buf, MAX_MSG_LEN, 1, f);

            // reset buf to all 0s
            memset(&buf,0, MAX_MSG_LEN);
        }
        fclose(read_file);

        // advances to next file in list for next run of loop
        curr_file = curr_file->next;
    }
    // initialize footer & write to file
    char footer[1024];
    memset(footer, 0, 1024);
    fwrite(footer, 1024, 1, f);

    fclose(f);
    return 0;
}

/*
 * first check that the archive file actually exists
 * add new representations of specified files to the archive file -- don't need to delete old versions
 * need to remove footer before writing -> remove_trailing_bytes
 * then need to add a footer after appending is over
*/
int append_files_to_archive(const char *archive_name, const file_list_t *files) {
    //initialize current file, tar header, & error msg
    char err_msg[MAX_MSG_LEN];
    tar_header curr_h;
    node_t *curr_file = files->head;

    // open archive file & check for error
    FILE *f_test = fopen(archive_name, "r");
    if(f_test == NULL){
        snprintf(err_msg, MAX_MSG_LEN, "Archive File does not exist: %s", archive_name);
        perror(err_msg);
        return -1;
    }
    fclose(f_test);

    // open file for appending now
    FILE *f = fopen(archive_name, "a");

    // remove footer
    remove_trailing_bytes(archive_name, 1024);

    // iteratate through each file in list
    for(int i = 0; i < files->size; i++){
        // open file to read from & check for error
        FILE *read_file  = fopen(curr_file->name, "r");
        if(read_file == NULL){
            snprintf(err_msg, MAX_MSG_LEN, "One of the files to be added does not exist: %s", curr_file->name);
            perror(err_msg);
            return -1;
        }

        // create and write tar header into file
        fill_tar_header(&curr_h, curr_file->name);
        fwrite(&curr_h, sizeof(tar_header), 1, f);

        // intialize nbytes & buffer to read and write from file to archive file
        char buf[MAX_MSG_LEN];
        memset(buf,0,MAX_MSG_LEN);
        int nbytes;

        // while loop to read 512 bytes at a time & stop after a read returns less than that
        while((nbytes = fread(&buf, 1, MAX_MSG_LEN, read_file)) > 0){
            
            // sets string terminator & writes to archive file
            buf[nbytes] = '\0';
            fwrite(&buf, MAX_MSG_LEN, 1, f);

            // reset buf to all 0s
            memset(&buf,0, MAX_MSG_LEN);
        }
        fclose(read_file);

        // advances to next file in list for next run of loop
        curr_file = curr_file->next;
    }

    // initialize footer & write to file
    char footer[1024];
    memset(footer, 0, 1024);
    fwrite(footer, 1024, 1, f);

    fclose(f);
    return 0;
}

/* 
 * make sure archive file exists
 * iterate through archive file's members files, printing the name of each
 * skip (length of file / 512) blocks of 512 bytes to get to the next header
 * know you're finished when you find an empty file name in the "tar_header" extracted from the archive file
*/
int get_archive_file_list(const char *archive_name, file_list_t *files) {
    // initalize error message
    char err_msg[MAX_MSG_LEN];

    // open archive file and check for error
    FILE *f = fopen(archive_name, "r");
    if(f == NULL){
        snprintf(err_msg, MAX_MSG_LEN, "Archive File does not exist: %s", archive_name);
        perror(err_msg);
        return -1;
    }

    // intialize variables for the file size and number of blocks in a file
    int size, blocks;
    
    // initalize the tar_header
    tar_header curr_h;

    // read in the tar header at the very top of the archive file
    fread(&curr_h, 1, sizeof(tar_header), f);

    // while loop to run until a name in the tar_header is empty - indicating we've hit the footer
    while(strcmp(curr_h.name,"")!=0){
        
        // converts size from octal to decimal and stores in size
        sscanf(curr_h.size, "%o", &size);

        // find number of 512 byte blocks for a given file
        if(size % 512 == 0){
            blocks = size / 512;
        }
        else{
            blocks = (size / 512)+1;
        }

        // adds file to linked list
        if(file_list_contains(files, curr_h.name) == 0){
            file_list_add(files, curr_h.name);
        }
        
        // skip all the blocks for a given file to get to the next header (or footer)
        fseek(f, blocks*512, SEEK_CUR);

        // read in the first 512 bytes at the new position: either another header, or the footer
        fread(&curr_h, 1, sizeof(tar_header), f);
    }
    fclose(f);
    return 0;
}

/*
 * iterate through the archive, create a new file in the cwd for each member file in the archive
 * only use most recent version of a file if multiple uploads
 * all the contents of each file will be represented as 512-byte blocks, 
 * don't want to write out all of last block which likely is not 512 bytes
*/
int extract_files_from_archive(const char *archive_name) {
    // initialize tar header, & error msg
    char err_msg[MAX_MSG_LEN];
    tar_header curr_h;

    // open archive file & check for error
    FILE *f = fopen(archive_name, "r");
    if(f == NULL){
        snprintf(err_msg, MAX_MSG_LEN, "Archive File does not exist: %s", archive_name);
        perror(err_msg);
        return -1;
    }

    // read in the tar header at the very top of the archive file
    fread(&curr_h, 1, sizeof(tar_header), f);

    // while loop to run through the archive file, stopping when the file name doesn't exist
    while(strcmp(curr_h.name,"")!=0){

        // intialize variables for the file size and number of blocks in a file
        int size, blocks;
        
        // converts from octal to decimal and stores in size
        sscanf(curr_h.size, "%o", &size);

        // create variable for the size of the last block of a given file
        int last_block = size % 512;

        // intializes buffer to read and write from file to archive file
        char buf[MAX_MSG_LEN];
        memset(buf,0,MAX_MSG_LEN);

        // find number of 512 byte blocks for a given file
        if(last_block == 0){
            blocks = size / 512;
        }
        else{
            blocks = (size / 512)+1;
        }

        // opens the file found in the archive
        FILE *new_file = fopen(curr_h.name, "w");

        // writes each block one at a time up to the last block
        for(int i = 0; i < (blocks-1); i++){
            fread(buf, 512, 1, f);
            fwrite(buf, 512, 1, new_file);
        }

        // writes out last block separately, since all 512 bytes are probably not valuable
        fread(buf, last_block, 1, f);
        fwrite(buf, last_block, 1, new_file);
        fclose(new_file);

        // skips the rest of the last block up to the next header (or footer)
        fseek(f, (512-last_block), SEEK_CUR);

        // read in the first 512 bytes at the new position: either another header, or the footer
        fread(&curr_h, 1, sizeof(tar_header), f);
    }
    fclose(f);
    return 0;
}
