#include <windows.h>
#include <string.h>
#include <stdio.h>
#include <inttypes.h>

struct FileEntry {
    uint64_t size;
    char * data;

    int name_length;
    char name[MAX_PATH];
};

void do_packing(char * path) {
    FileEntry files[500];
    int num_files = 0;

    //
    // Step One: Get all the files
    //

    {

        WIN32_FIND_DATA find_data;

        char * search_path = (char *) malloc(strlen(path) + 5);
        sprintf(search_path, "%s/*.*", path);

        HANDLE find_handle = FindFirstFile(search_path, &find_data);
        free(search_path);

        if(find_handle != INVALID_HANDLE_VALUE) {
            do {
                if(!(find_data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ) { // Ignore directories.

                    strcpy((char*)files[num_files].name, find_data.cFileName);

                    files[num_files].name_length = strlen(find_data.cFileName);

                    char * file_path = (char *) malloc(strlen(path) + strlen(find_data.cFileName) + 1);
                    sprintf(file_path, "%s/%s", path , find_data.cFileName);

                    HANDLE file_handle = CreateFile(file_path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);

                    free(file_path);

                    int high_file_size;
                    uint64_t file_size = GetFileSize(file_handle, (LPDWORD) &high_file_size);
                    file_size += high_file_size * (MAXDWORD + 1);

                    files[num_files].data = (char *) malloc(file_size);

                    ReadFile(file_handle, files[num_files].data, file_size, (LPDWORD) &(files[num_files].size), NULL);

                    num_files++;
                }

            } while(FindNextFile(find_handle, &find_data));

            FindClose(find_handle);
        } else {
            printf("ERROR: Directory to pack does not exist\n");

            for(int i = 0; i < num_files; i++) {
                free(files[i].data);
            }

            return;
        }
    }

    //
    // Step Two: Pack 'em
    //

    {
        uint64_t total_file_names_size = 0;
        uint64_t total_files_size      = 0;

        for(int i = 0; i < num_files; i++) {
            total_file_names_size += files[i].name_length;
            total_files_size      += files[i].size;
        }

        uint64_t header_size = num_files * 12 + total_file_names_size;
        uint64_t packed_size = 8 + header_size + total_files_size;

        char * header     = (char *) malloc(header_size);
        char * file_datas = (char *) malloc(total_files_size);

        uint64_t header_offset = 0;
        uint64_t file_datas_offset = 0;

        for(int i = 0; i < num_files; i++) {
            memcpy(header + header_offset, &files[i].name_length, 4);
            header_offset += 4;

            memcpy(header + header_offset, (char *) files[i].name, files[i].name_length);
            header_offset += files[i].name_length;

            memcpy(header + header_offset, &files[i].size, 8);
            header_offset += 8;

            memcpy(file_datas + file_datas_offset, files[i].data, files[i].size);
            file_datas_offset += files[i].size;
        }

        char * packed_file_data = (char *) malloc(packed_size);

        memcpy(packed_file_data,                   &header_size, 8);
        memcpy(packed_file_data + 8,               header,       header_size);
        memcpy(packed_file_data + 8 + header_size, file_datas,   total_files_size);

        char * packed_file_name = (char *) malloc(strlen(path) + 5);

        sprintf(packed_file_name, "%s.zop", path);

        HANDLE packed_file_handle = CreateFile(packed_file_name, GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);

        free(packed_file_name);

        WriteFile(packed_file_handle, packed_file_data, packed_size, NULL, NULL);

        free(header);
        free(file_datas);
        free(packed_file_data);

        for(int i = 0; i < num_files; i++) {
            free(files[i].data);
        }


        // Print packed file info
        printf("------------------------------------\n  %s:\n    Header Size: %lld bytes\n    Total Files Size: %lld bytes\n    Packed Size: %lld bytes\n    Files:", path, header_size, total_files_size, packed_size);

        int longest_file_name = 0;
        for(int i = 0; i < num_files; i++) {
            if(files[i].name_length > longest_file_name) longest_file_name = files[i].name_length;
        }

        for(int i = 0; i < num_files; i++) {
            printf("\n        * %s", files[i].name);
            for(int j = files[i].name_length; j < longest_file_name + 1; j++) {
                printf(" ");
            }
            printf("(%lld bytes)", files[i].size);
        }

        printf("\n------------------------------------");
    }

    return;
}

void do_unpacking(char * packed_file, char * file_to_recover) {

    //
    // Step One: Read Packed File
    //

    HANDLE file_handle = CreateFile(packed_file, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);

    int high_file_size;
    uint64_t file_size = GetFileSize(file_handle, (LPDWORD) &high_file_size);
    file_size += high_file_size * (MAXDWORD + 1);
    char * packed_data = (char *) malloc(file_size);

    if(!ReadFile(file_handle, packed_data, file_size, NULL, NULL)) {
        printf("ERROR: Packed file does not exist\n");
        free(packed_data);

        return;
    }

    uint64_t header_size;
    memcpy(&header_size, packed_data, 8);

    char * cursor = packed_data + 8;

    int file_to_recover_length = strlen(file_to_recover);
    uint64_t file_offset = 8 + header_size;

    while(cursor < packed_data + 8 + header_size) {
        int file_name_length;

        memcpy(&file_name_length, cursor, 4);
        cursor +=4;

        if(file_name_length == file_to_recover_length) {
            if(memcmp(cursor, file_to_recover, file_name_length) == 0) {
                // Found the file
                cursor += file_name_length;

                uint64_t file_size;
                memcpy(&file_size, cursor, 8);

                cursor = packed_data + file_offset;

                HANDLE recoverd_file_handle = CreateFile(file_to_recover, GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);

                WriteFile(recoverd_file_handle, cursor, file_size, NULL, NULL);

                free(packed_data);

                printf("------------------------------------\n  Recovered %s (%lld bytes)\n------------------------------------", file_to_recover, file_size);

                return;
            }
        }

        cursor += file_name_length;

        int file_size;
        memcpy(&file_size, cursor, 8);
        file_offset += file_size;

        cursor += 8;
    }

    printf("ERROR: File to recover does not exist\n");
    free(packed_data);

    return;
}

void main(int argc, char * argv[]) {

    if(argc == 3 && strcmp(argv[1], "-pack") == 0) {
        do_packing(argv[2]);
    } else if(argc == 4 && strcmp(argv[1], "-recover") == 0) {
        do_unpacking(argv[2], argv[3]);
    }

    return;
}
