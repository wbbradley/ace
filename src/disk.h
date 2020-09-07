/*
 *  disk.h
 *
 *  Copyright (c) 2016 Will Bradley.
 *
 */
#pragma once
#include <dirent.h>
#include <limits.h>
#include <stdint.h>
#include <string.h>
#include <string>
#include <sys/stat.h>
#include <sys/types.h>
#include <vector>

bool folder_exists(const std::string &path);
bool file_exists(const std::string &file_path);
bool get_line_col(const std::string &file_path,
                  size_t offset,
                  size_t &line,
                  size_t &col);
off_t file_size(const char *filename);
void make_relative_to_same_dir(std::string filename,
                               const std::string &existing_file,
                               std::string &full_path);
std::string directory_from_file_path(const std::string &file_path);
std::string leaf_from_file_path(const std::string &file_path);
bool ensure_directory_exists(const std::string &name);
bool move_files(const std::string &source, const std::string &dest);
bool list_files(const std::string &folder,
                const std::string &regex_match,
                std::vector<std::string> &leaf_names);
std::string ensure_ext(std::string name, std::string ext);

struct for_each_file_stat_t {
  bool regular_file() const {
    return !is_dir && is_file;
  }
  bool is_file;
  bool is_dir;
};

struct for_each_control_t {
  bool halt;
  bool recurse;
};

template <typename T>
bool for_each_file(const std::string &dir, const T &callback) {
  struct dirent *stFiles;
  DIR *stDirIn;
  char szFullName[PATH_MAX];
  char szDirectory[PATH_MAX];
  struct stat stFileInfo;

  strncpy(szDirectory, dir.c_str(), PATH_MAX - 1);

  if ((stDirIn = opendir(szDirectory)) != NULL) {
    while ((stFiles = readdir(stDirIn)) != NULL) {
      if ((strcmp(".", stFiles->d_name) == 0) ||
          (strcmp("..", stFiles->d_name) == 0)) {
        continue;
      }

      sprintf(szFullName, "%s/%s", szDirectory, stFiles->d_name);

      if (lstat(szFullName, &stFileInfo) < 0) {
        continue;
      }

      if (S_ISDIR(stFileInfo.st_mode)) {
        std::string subdir_name(szFullName);
        for_each_file_stat_t file_stat;
        file_stat.is_file = false;
        file_stat.is_dir = true;
        for_each_control_t control;
        control.halt = false;
        control.recurse = false;
        callback(subdir_name, file_stat, control);
        if (control.halt) {
          return false;
        }
        if (control.recurse && !for_each_file(subdir_name, callback)) {
          return false;
        }
      } else {
        std::string file_name(szFullName);
        for_each_file_stat_t file_stat;
        file_stat.is_file = true;
        file_stat.is_dir = false;
        for_each_control_t control;
        control.halt = false;
        control.recurse = false;
        callback(file_name, file_stat, control);
        if (control.halt) {
          return false;
        }
      }
    }
    closedir(stDirIn);
  }
  return true;
}
