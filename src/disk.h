/*
 *  disk.h
 *
 *  Copyright (c) 2016 Will Bradley.
 *
 */
#pragma once
#include <string>
#include <stdint.h>
#include <vector>

void print_dir(FILE *fp, const char *directory, const char *match);
bool folder_exists(const std::string &path);
bool file_exists(const std::string &file_path);
bool get_line_col(const std::string &file_path, size_t offset, size_t &line, size_t &col);
off_t file_size(const char *filename);
void make_relative_to_same_dir(std::string filename, const std::string &existing_file, std::string &full_path);
std::string directory_from_file_path(const std::string &file_path);
std::string leaf_from_file_path(const std::string &file_path);
bool ensure_directory_exists(const std::string &name);
bool move_files(const std::string &source, const std::string &dest);
bool list_files(const std::string &folder, const std::string &regex_match, std::vector<std::string> &leaf_names);
std::string ensure_ext(std::string name, std::string ext);
