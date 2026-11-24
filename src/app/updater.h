#pragma once

// IPDE updater: reads GitHub Releases, installs any version (silently for automatic updates)
// by swapping the running executable, and restarts.

#include "core/http.h"

#include <memory>
#include <string>
#include <vector>

namespace ipde {

struct Release {
	std::string tag;
	std::string name;
	std::string body;
	std::string date;
	std::string html_url;
	std::string asset_url; // Executable for this platform, empty if not available.
	int64_t asset_size = 0;
	int major = 0;
	int minor = 0;
	int patch = 0;
	bool stable = false;

	int64_t sort_key() const { return ((int64_t)major * 1000000 + (int64_t)minor * 1000 + patch) * 2 + (stable ? 1 : 0); }
	std::string display_name() const;
};

class Updater {
public:
	enum class State {
		IDLE,
		CHECKING,
		DOWNLOADING,
		INSTALLED, // Waiting for a restart.
		FAILED,
	};

	void init();
	void update(); // Call every frame.

	void check(bool p_automatic);
	void install(const Release &p_release, bool p_automatic);
	void restart_into_new_version();

	State get_state() const { return state; }
	const std::string &get_status() const { return status; }
	float get_progress() const;
	const std::vector<Release> &get_releases() const { return releases; }
	bool has_checked() const { return checked; }
	const Release *get_newest_available() const; // Newest release the user should update to, or null.
	const Release *get_installed_release() const; // Release that is installed (after an update, before restarting).
	bool is_busy() const { return state == State::CHECKING || state == State::DOWNLOADING; }
	bool was_automatic() const { return automatic_install; }

	static int64_t current_sort_key();
	static bool parse_version(const std::string &p_tag, int &r_major, int &r_minor, int &r_patch);

private:
	State state = State::IDLE;
	std::string status;
	std::vector<Release> releases;
	bool checked = false;
	bool automatic_check = false;
	bool automatic_install = false;
	Release installing;
	std::string exe_path;
	std::string download_path;
	std::shared_ptr<http::Request> request;

	void _on_releases(const std::shared_ptr<http::Request> &p_req);
	void _on_download(const std::shared_ptr<http::Request> &p_req);
	void _cleanup_old_files();
};

} // namespace ipde
