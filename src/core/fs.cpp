#include "core/fs.h"

#include <chrono>
#include <filesystem>
#include <fstream>

namespace stdfs = std::filesystem;

namespace ipde::fs {

static stdfs::path to_path(const std::string &p_path) {
#ifdef _WIN32
	return stdfs::u8path(p_path);
#else
	return stdfs::path(p_path);
#endif
}

static std::string from_path(const stdfs::path &p_path) {
#ifdef _WIN32
	const auto s = p_path.u8string();
	return std::string(s.begin(), s.end());
#else
	return p_path.string();
#endif
}

bool read_file(const std::string &p_path, std::string &r_data) {
	std::ifstream f(to_path(p_path), std::ios::binary);
	if (!f) {
		return false;
	}
	r_data.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
	return true;
}

bool write_file(const std::string &p_path, const std::string &p_data) {
	std::ofstream f(to_path(p_path), std::ios::binary | std::ios::trunc);
	if (!f) {
		return false;
	}
	f.write(p_data.data(), (std::streamsize)p_data.size());
	return (bool)f;
}

bool exists(const std::string &p_path) {
	std::error_code ec;
	return stdfs::exists(to_path(p_path), ec);
}

bool is_directory(const std::string &p_path) {
	std::error_code ec;
	return stdfs::is_directory(to_path(p_path), ec);
}

bool make_dirs(const std::string &p_path) {
	std::error_code ec;
	stdfs::create_directories(to_path(p_path), ec);
	return is_directory(p_path);
}

bool rename_path(const std::string &p_from, const std::string &p_to) {
	std::error_code ec;
	stdfs::rename(to_path(p_from), to_path(p_to), ec);
	return !ec;
}

bool remove_path(const std::string &p_path) {
	std::error_code ec;
	stdfs::remove_all(to_path(p_path), ec);
	return !ec;
}

bool copy_file(const std::string &p_from, const std::string &p_to) {
	std::error_code ec;
	stdfs::copy_file(to_path(p_from), to_path(p_to), stdfs::copy_options::overwrite_existing, ec);
	return !ec;
}

std::vector<std::string> list_dir(const std::string &p_path) {
	std::vector<std::string> out;
	std::error_code ec;
	for (const auto &entry : stdfs::directory_iterator(to_path(p_path), ec)) {
		out.push_back(from_path(entry.path().filename()));
	}
	return out;
}

std::string join(const std::string &p_a, const std::string &p_b) {
	return from_path(to_path(p_a) / to_path(p_b));
}

std::string get_dir(const std::string &p_path) {
	return from_path(to_path(p_path).parent_path());
}

std::string get_file_name(const std::string &p_path) {
	return from_path(to_path(p_path).filename());
}

int64_t now_unix() {
	return std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
}

} // namespace ipde::fs
