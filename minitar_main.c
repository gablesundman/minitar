#include <stdio.h>
#include <string.h>

#include "file_list.h"
#include "minitar.h"

int main(int argc, char **argv) {
    if (argc < 4) {
        printf("Usage: %s -c|a|t|u|x -f ARCHIVE [FILE...]\n", argv[0]);
        return 0;
    }

    // create file list
    file_list_t files;
    file_list_init(&files);

    // get archive name and minitar argument from cmd line
    char *archive = argv[3];
    char *arg = argv[1];
    
    // fill file list with files from cmd line
    for(int i = 4; i < argc; i++){
        file_list_add(&files, argv[i]);
    }

    // execute functions based on minitar arg, or give error if invalid argument
    if(strcmp(arg,"-c") == 0){
        if(create_archive(archive, &files) == -1){
            file_list_clear(&files);
            return 1;
        }
    }
    else if(strcmp(arg,"-a") == 0){
        if(append_files_to_archive(archive, &files) == -1){
            file_list_clear(&files);
            return 1;
        }
    }
    else if(strcmp(arg,"-t") == 0){
        if(get_archive_file_list(archive, &files) == -1){
            file_list_clear(&files);
            return 1;
        }
        else{
            node_t *curr_file = files.head;
            while(curr_file != NULL){
                printf("%s\n", curr_file->name);
                curr_file = curr_file->next;
            }
        }
    }
    else if(strcmp(arg,"-u") == 0){
        // initialize a file list to list what files are in the archive
        file_list_t archive_list; 
        file_list_init(&archive_list);

        // retrieve archive list using get_archive_file_list function
        get_archive_file_list(archive, &archive_list);

        // check if the files requested to be updated are present in the archive
        if(file_list_is_subset(&files, &archive_list) == 0){
            printf("Error: One or more of the specified files is not already present in archive\n");
            file_list_clear(&archive_list);
            file_list_clear(&files);
            return 1;
        }
        file_list_clear(&archive_list);

        // if all files were present, then we just need to append all of these files to the archive
        if(append_files_to_archive(archive, &files) == -1){
            file_list_clear(&files);
            return 1;
        }
    }
    else if(strcmp(arg,"-x") == 0){
        if(extract_files_from_archive(archive) == -1){
            file_list_clear(&files);
            return 1;
        }
    }
    else{
        printf("Invalid Argument: %s\n", arg);
        printf("Usage: %s -c|a|t|u|x -f ARCHIVE [FILE...]\n", argv[0]);
        file_list_clear(&files);
        return 1;
    }

    // clear files list
    file_list_clear(&files);
    return 0;
}
