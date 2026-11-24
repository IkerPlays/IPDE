#pragma once

// File helpers. Paths are UTF-8 strings.

#include <cstdint>
#include <string>
#include <vector>

namespace ipde::fs {

bool read_file(const std::string &p_path, std::string &r_data);
bool write_file(const std::string &p_path, const std::string &p_data);
bool exists(const std::string &p_path);
bool is_directory(const std::string &p_path);
bool make_dirs(const std::string &p_path);
bool rename_path(const std::string &p_from, const std::string &p_to);
bool remove_path(const std::string &p_path);
bool copy_file(const std::string &p_from, const std::string &p_to);
std::vector<std::string> list_dir(const std::string &p_path);
std::string join(const std::string &p_a, const std::string &p_b);
std::string get_dir(const std::string &p_path);
std::string get_file_name(const std::string &p_path);
int64_t now_unix();

} // namespace ipde::fs
