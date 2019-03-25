/*
 *  disk.cpp
 *
 *  Copyright (c) 2016 Will Bradley.
 *
 */

#include "disk.h"

#include <dirent.h>
#include <fstream>
#include <regex>
#include <sstream>
#include <stdio.h>
#include <string.h>
#include <string>
#include <sys/errno.h>
#include <sys/param.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "dbg.h"
#include "logger_decls.h"
#include "utils.h"
#include "zion.h"

bool file_exists(const std::string &file_path) {
    errno = 0;

    if (file_path.size() == 0)
        return false;

    return access(file_path.c_str(), R_OK) != -1;
}

bool folder_exists(const std::string &path) {
    struct stat stDirInfo;
    errno = 0;
    if (lstat(path.c_str(), &stDirInfo) < 0) {
        return false;
    }
    if (!S_ISDIR(stDirInfo.st_mode)) {
        return false;
    }

    return true;
}

off_t file_size(const char *filename) {
    struct stat st;

    if (stat(filename, &st) == 0)
        return st.st_size;

    return 0;
}

bool get_line_col(const std::string &file_path, size_t offset, size_t &line, size_t &col) {
    FILE *fp = fopen(file_path.c_str(), "rt");
    if (fp != NULL) {
        char ch;
        line = 1;
        col = 1;
        for (size_t i = 0; i < offset; i++) {
            if (fread(&ch, 1, 1, fp) == 0)
                return false;
            switch (ch) {
            case '\n':
                line++;
                col = 1;
                continue;
            default:
                col++;
                continue;
            }
        }
        fclose(fp);
        return true;
    }
    return false;
}

bool list_files(const std::string &folder,
                const std::string &regex_match,
                std::vector<std::string> &leaf_names) {
    struct stat stDirInfo;
    struct dirent *stFiles;
    DIR *stDirIn;

    if (lstat(folder.c_str(), &stDirInfo) < 0) {
        debug(log(log_info, "list_files : error : error in lstat of %s", folder.c_str()));
        return false;
    }
    if (!S_ISDIR(stDirInfo.st_mode))
        return false;
    if ((stDirIn = opendir(folder.c_str())) == NULL) {
        debug(log(log_info, "list_files : error : funky error #3 on %s", folder.c_str()));
        return false;
    }
    leaf_names.resize(0);
    const auto regex = std::regex(regex_match.size() ? regex_match.c_str() : "");
    while ((stFiles = readdir(stDirIn)) != NULL) {
        std::string leaf_name = stFiles->d_name;
        if (leaf_name == "." || leaf_name == "..") {
            continue;
        } else if (regex_match.size() != 0) {
            std::smatch match;
            if (!std::regex_search(leaf_name, match, regex)) {
                continue;
            }
        }
        leaf_names.push_back(leaf_name);
    }
    closedir(stDirIn);

    return true;
}

bool move_files(const std::string &source, const std::string &dest) {
    if (!ensure_directory_exists(dest)) {
        return false;
    }

    struct stat stDirInfo;
    struct dirent *stFiles;
    DIR *stDirIn;
    struct stat stFileInfo;

    if (lstat(source.c_str(), &stDirInfo) < 0) {
        debug(log(log_info, "move_files : error : error in lstat of %s", source.c_str()));
        return false;
    }
    if (!S_ISDIR(stDirInfo.st_mode)) {
        return false;
    }
    if ((stDirIn = opendir(source.c_str())) == NULL) {
        debug(log(log_info, "move_files : error : funky error #3 on %s", source.c_str()));
        return false;
    }
    while ((stFiles = readdir(stDirIn)) != NULL) {
        std::string leaf_name = stFiles->d_name;
        if (leaf_name == "." || leaf_name == "..") {
            continue;
        }

        std::string full_source_path = source + "/" + leaf_name;

        if (lstat(full_source_path.c_str(), &stFileInfo) < 0) {
            debug(log(log_info, "move_files : error : funky error #1 on %s",
                      full_source_path.c_str()));
            continue;
        }

        std::string full_target_path = dest + "/" + leaf_name;
        debug(log(log_info, "move_files : info : renaming %s to %s", full_source_path.c_str(),
                  full_target_path.c_str()));
        if (rename(full_source_path.c_str(), full_target_path.c_str()) != 0) {
            closedir(stDirIn);
            return false;
        }
        assert(!file_exists(full_source_path.c_str()));
        assert(file_exists(full_target_path.c_str()));
    }
    closedir(stDirIn);

    return true;
}

void print_dir(FILE *fp, const char *directory, const char *match) {
#ifdef ZION_DEBUG
    struct stat stDirInfo;
    struct dirent *stFiles;
    DIR *stDirIn;
    char szFullName[MAXPATHLEN];
    char szDirectory[MAXPATHLEN];
    struct stat stFileInfo;

    strncpy(szDirectory, directory, MAXPATHLEN - 1);

    if (lstat(szDirectory, &stDirInfo) < 0) {
        perror(szDirectory);
        return;
    }
    if (!S_ISDIR(stDirInfo.st_mode)) {
        return;
    }
    if ((stDirIn = opendir(szDirectory)) == NULL) {
        perror(szDirectory);
        return;
    }
    while ((stFiles = readdir(stDirIn)) != NULL) {
        sprintf(szFullName, "%s/%s", szDirectory, stFiles->d_name);

        if (lstat(szFullName, &stFileInfo) < 0)
            perror(szFullName);

        /* is the file a directory? */
        if (S_ISDIR(stFileInfo.st_mode)) {
            debug(write_fp(fp, "Directory: %s\n", szFullName));
        } else {
            if (match == NULL || strstr(szFullName, match) != NULL) {
                debug(write_fp(fp, "File: %s\n", szFullName));
            }
        }
    }
    closedir(stDirIn);
    return;
#endif
}

std::string ensure_ext(std::string name, std::string ext) {
    assert(ext[0] == '.');
    assert(ext.size() > 1);
    if (ends_with(name, ext)) {
        return name;
    } else {
        return name + ext;
    }
}

bool ensure_directory_exists(const std::string &name) {
    errno = 0;
    mode_t mode = S_IRWXU;
    return ((mkdir(name.c_str(), mode) == 0) || (errno == EEXIST));
}

void make_relative_to_same_dir(std::string filename,
                               const std::string &existing_file,
                               std::string &full_path) {
    size_t pos = existing_file.find_last_of('/');
    assert(pos != std::string::npos);
    assert(pos != existing_file.size() - 1);
    full_path.resize(0);
    full_path.append(existing_file, 0, pos + 1);
    full_path.append(filename);
}

std::string directory_from_file_path(const std::string &file_path) {
    size_t pos = file_path.find_last_of('/');
    if (pos == std::string::npos)
        return std::string();

    return file_path.substr(0, pos);
}

std::string leaf_from_file_path(const std::string &file_path) {
    size_t pos = file_path.find_last_of('/');
    if (pos == std::string::npos) {
        return file_path;
    }

    return file_path.substr(pos + 1);
}
