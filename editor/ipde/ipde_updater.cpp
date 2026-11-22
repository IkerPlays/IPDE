/**************************************************************************/
/*  ipde_updater.cpp                                                      */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             GODOT ENGINE                               */
/*                        https://godotengine.org                         */
/**************************************************************************/
/* Copyright (c) 2014-present Godot Engine contributors (see AUTHORS.md). */
/* Copyright (c) 2007-2014 Juan Linietsky, Ariel Manzur.                  */
/*                                                                        */
/* Permission is hereby granted, free of charge, to any person obtaining  */
/* a copy of this software and associated documentation files (the        */
/* "Software"), to deal in the Software without restriction, including    */
/* without limitation the rights to use, copy, modify, merge, publish,    */
/* distribute, sublicense, and/or sell copies of the Software, and to     */
/* permit persons to whom the Software is furnished to do so, subject to  */
/* the following conditions:                                              */
/*                                                                        */
/* The above copyright notice and this permission notice shall be         */
/* included in all copies or substantial portions of the Software.        */
/*                                                                        */
/* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,        */
/* EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF     */
/* MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. */
/* IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY   */
/* CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,   */
/* TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE      */
/* SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.                 */
/**************************************************************************/

#include "ipde_updater.h"

#include "core/config/engine.h"
#include "core/io/config_file.h"
#include "core/io/dir_access.h"
#include "core/io/file_access.h"
#include "core/io/json.h"
#include "core/io/zip_io.h"
#include "core/object/callable_mp.h"
#include "core/os/os.h"
#include "core/os/time.h"
#include "core/version.h"
#include "editor/editor_node.h"
#include "editor/editor_string_names.h"
#include "editor/file_system/editor_paths.h"
#include "editor/settings/editor_settings.h"
#include "editor/themes/editor_scale.h"
#include "scene/gui/box_container.h"
#include "scene/gui/button.h"
#include "scene/gui/check_box.h"
#include "scene/gui/dialogs.h"
#include "scene/gui/label.h"
#include "scene/gui/progress_bar.h"
#include "scene/gui/rich_text_label.h"
#include "scene/gui/split_container.h"
#include "scene/gui/tree.h"
#include "scene/main/http_request.h"
#include "scene/main/scene_tree.h"

IPDEUpdater *IPDEUpdater::singleton = nullptr;

static const char *OLD_SUFFIX = ".ipde_old";
static const char *STAGING_DIR = ".ipde_update_staging";

/* Release helpers */

int64_t IPDEUpdater::Release::get_sort_key() const {
	// Higher is newer. For the same number, a stable is considered newer than a devbuild.
	return ((int64_t(major) * 1000000 + int64_t(minor) * 1000 + int64_t(patch)) * 2) + (stable ? 1 : 0);
}

String IPDEUpdater::Release::get_display_name() const {
	return vformat("IPDE v.%d.%d.%d", major, minor, patch);
}

int64_t IPDEUpdater::_get_current_sort_key() {
	const bool stable = String(IPDE_VERSION_CHANNEL) == "stable";
	return ((int64_t(IPDE_VERSION_MAJOR) * 1000000 + int64_t(IPDE_VERSION_MINOR) * 1000 + int64_t(IPDE_VERSION_PATCH)) * 2) + (stable ? 1 : 0);
}

bool IPDEUpdater::_parse_version(const String &p_tag, int &r_major, int &r_minor, int &r_patch) {
	// Accepts "v1.2.3", "v.1.2.3", "1.2.3", "v1.2.3-stable", "IPDE v.1.2.3"...
	String s = p_tag.strip_edges();
	if (s.begins_with("IPDE")) {
		s = s.substr(4).strip_edges();
	}
	s = s.trim_prefix("v").trim_prefix("V").trim_prefix(".");
	s = s.get_slicec('-', 0).get_slicec(' ', 0);
	const PackedStringArray parts = s.split(".");
	if (parts.size() != 3) {
		return false;
	}
	for (const String &part : parts) {
		if (!part.is_valid_int()) {
			return false;
		}
	}
	r_major = parts[0].to_int();
	r_minor = parts[1].to_int();
	r_patch = parts[2].to_int();
	return true;
}

String IPDEUpdater::_get_platform_asset_suffix() {
	const String arch = Engine::get_singleton()->get_architecture_name();
#if defined(WINDOWS_ENABLED)
	return "_windows_" + arch + ".zip";
#elif defined(MACOS_ENABLED)
	return "_macos_universal.zip";
#elif defined(ANDROID_ENABLED)
	return "_android_editor.apk";
#elif defined(LINUXBSD_ENABLED)
	return "_linux_" + arch + ".zip";
#else
	return String();
#endif
}

const IPDEUpdater::Asset *IPDEUpdater::_get_platform_asset(const Release &p_release) const {
	const String suffix = _get_platform_asset_suffix();
	if (suffix.is_empty()) {
		return nullptr;
	}
	for (const Asset &asset : p_release.assets) {
		if (asset.name.ends_with(suffix)) {
			return &asset;
		}
	}
	return nullptr;
}

String IPDEUpdater::_get_repository() const {
	String repo;
	if (EditorSettings::get_singleton() && EditorSettings::get_singleton()->has_setting("ipde/updates/repository")) {
		repo = String(EDITOR_GET("ipde/updates/repository")).strip_edges();
	}
	if (repo.is_empty()) {
		repo = IPDE_GITHUB_REPOSITORY;
	}
	return repo;
}

String IPDEUpdater::_get_token() const {
	// Optional GitHub token (fine-grained, read-only) to access releases of a private repository.
	// It is stored in the local editor settings of each machine, never in the IPDE binary.
	if (EditorSettings::get_singleton() && EditorSettings::get_singleton()->has_setting("ipde/updates/github_token")) {
		return String(EDITOR_GET("ipde/updates/github_token")).strip_edges();
	}
	return String();
}

Vector<String> IPDEUpdater::_get_api_headers(const String &p_accept) const {
	Vector<String> headers;
	headers.push_back("Accept: " + p_accept);
	headers.push_back("User-Agent: IPDE-Updater/" IPDE_VERSION_NUMBER);
	headers.push_back("X-GitHub-Api-Version: 2022-11-28");
	const String token = _get_token();
	if (!token.is_empty()) {
		headers.push_back("Authorization: Bearer " + token);
	}
	return headers;
}

String IPDEUpdater::_get_config_path() const {
	return EditorPaths::get_singleton()->get_config_dir().path_join("ipde_updater.cfg");
}

Variant IPDEUpdater::_get_config_value(const String &p_key, const Variant &p_default) const {
	Ref<ConfigFile> cf;
	cf.instantiate();
	if (cf->load(_get_config_path()) != OK) {
		return p_default;
	}
	return cf->get_value("updater", p_key, p_default);
}

void IPDEUpdater::_set_config_value(const String &p_key, const Variant &p_value) {
	Ref<ConfigFile> cf;
	cf.instantiate();
	cf->load(_get_config_path()); // May not exist yet.
	cf->set_value("updater", p_key, p_value);
	cf->save(_get_config_path());
}

/* Fetching releases */

void IPDEUpdater::_fetch_releases(bool p_automatic) {
	if (fetching) {
		return;
	}
	fetching = true;
	fetch_is_automatic = p_automatic;
	_set_status(TTR("Looking for IPDE releases..."));
	if (refresh_button) {
		refresh_button->set_disabled(true);
	}

	const Vector<String> headers = _get_api_headers("application/vnd.github+json");
	String url = vformat("https://api.github.com/repos/%s/releases?per_page=100", _get_repository());
	// For testing: point the updater to another GitHub-compatible releases endpoint.
	if (OS::get_singleton()->has_environment("IPDE_UPDATER_API")) {
		url = OS::get_singleton()->get_environment("IPDE_UPDATER_API");
	}
	Error err = list_request->request(url, headers);
	if (err != OK) {
		fetching = false;
		refresh_button->set_disabled(false);
		_set_status(vformat(TTR("Couldn't connect to GitHub (error %d)."), err), true);
	}
}

void IPDEUpdater::_releases_request_completed(int p_result, int p_response_code, const PackedStringArray &p_headers, const PackedByteArray &p_body) {
	fetching = false;
	refresh_button->set_disabled(false);

	if (p_result != HTTPRequest::RESULT_SUCCESS) {
		_set_status(vformat(TTR("Couldn't connect to GitHub (error %d)."), p_result), true);
		return;
	}
	if (p_response_code == 404) {
		if (_get_token().is_empty()) {
			_set_status(vformat(TTR("Repository \"%s\" not found. If it is private, set a GitHub token in Editor Settings > IPDE > Updates."), _get_repository()), true);
		} else {
			_set_status(vformat(TTR("Repository \"%s\" not found, or the GitHub token can't access it."), _get_repository()), true);
		}
		return;
	}
	if (p_response_code == 401) {
		_set_status(TTR("The GitHub token is invalid or has expired."), true);
		return;
	}
	if (p_response_code == 403 || p_response_code == 429) {
		bool rate_limited = p_response_code == 429;
		for (const String &h : p_headers) {
			if (h.to_lower().begins_with("x-ratelimit-remaining:") && h.get_slicec(':', 1).strip_edges() == "0") {
				rate_limited = true;
			}
		}
		if (rate_limited) {
			_set_status(TTR("GitHub rate limit reached. Try again later."), true);
		} else {
			_set_status(vformat(TTR("Unexpected response from GitHub: %d."), p_response_code), true);
		}
		return;
	}
	if (p_response_code != 200) {
		_set_status(vformat(TTR("Unexpected response from GitHub: %d."), p_response_code), true);
		return;
	}

	const Variant parsed = JSON::parse_string(String::utf8((const char *)p_body.ptr(), p_body.size()));
	if (parsed.get_type() != Variant::ARRAY) {
		_set_status(TTR("Invalid response received from GitHub."), true);
		return;
	}

	releases.clear();
	const Array arr = parsed;
	for (const Variant &v : arr) {
		if (v.get_type() != Variant::DICTIONARY) {
			continue;
		}
		const Dictionary d = v;
		if (bool(d.get("draft", false))) {
			continue;
		}
		Release r;
		r.tag = d.get("tag_name", "");
		if (!_parse_version(r.tag, r.major, r.minor, r.patch)) {
			continue;
		}
		r.name = d.get("name", "");
		if (d.get("name", Variant()).get_type() != Variant::STRING || r.name.is_empty()) {
			r.name = r.get_display_name();
		}
		r.html_url = d.get("html_url", "");
		r.body = d.get("body", Variant()).get_type() == Variant::STRING ? String(d.get("body", "")) : String();
		r.published_at = d.get("published_at", Variant()).get_type() == Variant::STRING ? String(d.get("published_at", "")).get_slicec('T', 0) : String();
		const String tag_lower = r.tag.to_lower();
		if (tag_lower.ends_with("-stable")) {
			r.stable = true;
		} else if (tag_lower.contains("-dev")) {
			r.stable = false;
		} else {
			r.stable = !bool(d.get("prerelease", false));
		}
		const Array assets = d.get("assets", Array());
		for (const Variant &av : assets) {
			const Dictionary ad = av;
			Asset a;
			a.name = ad.get("name", "");
			a.url = ad.get("browser_download_url", "");
			a.api_url = ad.get("url", "");
			a.size = int64_t(ad.get("size", 0));
			r.assets.push_back(a);
		}
		releases.push_back(r);
	}

	// Newest first.
	struct ReleaseSort {
		bool operator()(const Release &p_a, const Release &p_b) const { return p_a.get_sort_key() > p_b.get_sort_key(); }
	};
	releases.sort_custom<ReleaseSort>();
	releases_loaded = true;

	// Find the newest version this platform can install.
	newest_available = String();
	const int64_t current = _get_current_sort_key();
	for (const Release &r : releases) {
		if (r.get_sort_key() > current && r.stable) {
			newest_available = r.get_display_name();
			break;
		}
	}
	emit_signal(SNAME("releases_updated"), newest_available);

	if (releases.is_empty()) {
		_set_status(TTR("No IPDE releases have been published yet."));
	} else if (newest_available.is_empty()) {
		_set_status(TTR("You are using the latest stable version of IPDE."));
	} else {
		_set_status(vformat(TTR("A new stable version is available: %s."), newest_available));
	}

	_update_release_tree();
	if (fetch_is_automatic) {
		_check_for_prompt();
	}
}

void IPDEUpdater::_check_for_prompt() {
	const bool notify_devbuilds = EditorSettings::get_singleton()->has_setting("ipde/updates/notify_devbuilds") && bool(EDITOR_GET("ipde/updates/notify_devbuilds"));
	const int64_t current = _get_current_sort_key();
	prompt_release = -1;
	for (int i = 0; i < releases.size(); i++) {
		const Release &r = releases[i];
		if (r.get_sort_key() <= current) {
			break;
		}
		if (!r.stable && !notify_devbuilds) {
			continue;
		}
		prompt_release = i;
		break;
	}
	if (prompt_release < 0) {
		return;
	}

	const Release &r = releases[prompt_release];
	if (String(_get_config_value("skipped_tag", "")) == r.tag) {
		return;
	}
	const double postponed_until = _get_config_value("postponed_until", 0.0);
	if (String(_get_config_value("postponed_tag", "")) == r.tag && Time::get_singleton()->get_unix_time_from_system() < postponed_until) {
		return;
	}

	prompt_text->clear();
	prompt_text->push_bold();
	prompt_text->add_text(vformat(TTR("%s (%s) is available!"), r.get_display_name(), r.stable ? TTR("Stable") : TTR("DevBuild")));
	prompt_text->pop();
	prompt_text->add_newline();
	prompt_text->add_text(vformat(TTR("You are using %s."), String(IPDE_VERSION_FULL_NAME)));
	prompt_text->add_newline();
	prompt_text->add_newline();
	if (!r.body.is_empty()) {
		prompt_text->add_text(r.body.left(1500));
		prompt_text->add_newline();
		prompt_text->add_newline();
	}
	prompt_text->push_bold();
	prompt_text->add_text(TTR("Do you want to update now?"));
	prompt_text->pop();
	prompt_dialog->popup_centered(Size2(560, 320) * EDSCALE);
}

void IPDEUpdater::_set_status(const String &p_text, bool p_error) {
	if (!status_label) {
		return;
	}
	status_label->set_text(p_text);
	status_label->set_tooltip_text(p_text);
	if (p_error) {
		status_label->add_theme_color_override(SceneStringName(font_color), status_label->get_theme_color(SNAME("error_color"), EditorStringName(Editor)));
	} else {
		status_label->remove_theme_color_override(SceneStringName(font_color));
	}
}

/* Startup prompt */

void IPDEUpdater::_prompt_confirmed() {
	if (prompt_release >= 0 && prompt_release < releases.size()) {
		show_version_manager();
		_start_install(prompt_release);
	}
}

void IPDEUpdater::_prompt_canceled() {
	if (prompt_release >= 0 && prompt_release < releases.size()) {
		// Ask again in 12 hours (avoids asking twice when going from the project manager to the editor).
		_set_config_value("postponed_tag", releases[prompt_release].tag);
		_set_config_value("postponed_until", Time::get_singleton()->get_unix_time_from_system() + 12 * 3600);
	}
}

void IPDEUpdater::_prompt_custom_action(const StringName &p_action) {
	if (p_action == SNAME("skip") && prompt_release >= 0 && prompt_release < releases.size()) {
		_set_config_value("skipped_tag", releases[prompt_release].tag);
		prompt_dialog->hide();
	}
}

/* Version manager */

void IPDEUpdater::show_version_manager() {
	manager_dialog->popup_centered_clamped(Size2(900, 560) * EDSCALE, 0.8);
	if (!releases_loaded && !fetching) {
		_fetch_releases(false);
	} else {
		_update_release_tree();
	}
}

void IPDEUpdater::_update_release_tree() {
	if (!release_tree) {
		return;
	}
	release_tree->clear();
	TreeItem *root = release_tree->create_item();
	const bool show_dev = show_devbuilds_check->is_pressed();
	const int64_t current = _get_current_sort_key();
	const Color success = release_tree->get_theme_color(SNAME("success_color"), EditorStringName(Editor));
	const Color accent = release_tree->get_theme_color(SNAME("accent_color"), EditorStringName(Editor));
	const Color warning = release_tree->get_theme_color(SNAME("warning_color"), EditorStringName(Editor));
	const Color disabled = release_tree->get_theme_color(SNAME("font_disabled_color"), EditorStringName(Editor));

	TreeItem *to_select = nullptr;
	for (int i = 0; i < releases.size(); i++) {
		const Release &r = releases[i];
		if (!r.stable && !show_dev && r.get_sort_key() != current) {
			continue;
		}
		TreeItem *item = release_tree->create_item(root);
		item->set_text(0, r.get_display_name());
		item->set_text(1, r.stable ? TTR("Stable") : TTR("DevBuild"));
		item->set_custom_color(1, r.stable ? success : warning);
		item->set_text(2, r.published_at);
		String state;
		if (r.get_sort_key() == current) {
			state = TTR("Installed");
			item->set_custom_color(3, accent);
		} else if (r.get_sort_key() > current) {
			state = TTR("Newer");
			item->set_custom_color(3, success);
		} else {
			state = TTR("Older");
			item->set_custom_color(3, disabled);
		}
		if (!_get_platform_asset(r) && !_get_platform_asset_suffix().is_empty()) {
			state += " - " + TTR("not available for this platform");
		}
		item->set_text(3, state);
		item->set_metadata(0, i);
		if (!to_select) {
			to_select = item;
		}
	}
	if (to_select) {
		to_select->select(0);
	} else {
		notes_label->clear();
		install_button->set_disabled(true);
		open_page_button->set_disabled(true);
	}
}

void IPDEUpdater::_release_selected() {
	TreeItem *item = release_tree->get_selected();
	if (!item) {
		return;
	}
	const int idx = item->get_metadata(0);
	ERR_FAIL_INDEX(idx, releases.size());
	const Release &r = releases[idx];

	notes_label->clear();
	notes_label->push_font_size(notes_label->get_theme_font_size(SNAME("bold_font_size")) * 1.3);
	notes_label->push_bold();
	notes_label->add_text(r.name);
	notes_label->pop();
	notes_label->pop();
	notes_label->add_newline();
	notes_label->add_text(vformat("%s  -  %s  -  %s", r.tag, r.stable ? TTR("Stable") : TTR("DevBuild"), r.published_at));
	notes_label->add_newline();
	notes_label->add_newline();
	notes_label->add_text(r.body.is_empty() ? TTR("No release notes.") : r.body);

	const bool busy = installing_release >= 0;
#if defined(WEB_ENABLED)
	install_button->set_text(TTR("Open Release Page"));
	install_button->set_disabled(busy);
#else
	const bool has_asset = _get_platform_asset(r) != nullptr;
	const int64_t current = _get_current_sort_key();
	if (r.get_sort_key() == current) {
		install_button->set_text(TTR("Reinstall"));
	} else if (r.get_sort_key() > current) {
		install_button->set_text(vformat(TTR("Update to %s"), r.get_display_name()));
	} else {
		install_button->set_text(vformat(TTR("Install %s"), r.get_display_name()));
	}
	install_button->set_disabled(busy || !has_asset);
	install_button->set_tooltip_text(has_asset ? String() : TTR("This release has no build for your platform."));
#endif
	open_page_button->set_disabled(r.html_url.is_empty());
}

void IPDEUpdater::_refresh_pressed() {
	_fetch_releases(false);
}

void IPDEUpdater::_open_page_pressed() {
	TreeItem *item = release_tree->get_selected();
	if (!item) {
		return;
	}
	const int idx = item->get_metadata(0);
	ERR_FAIL_INDEX(idx, releases.size());
	OS::get_singleton()->shell_open(releases[idx].html_url);
}

void IPDEUpdater::_install_pressed() {
	TreeItem *item = release_tree->get_selected();
	if (!item) {
		return;
	}
	const int idx = item->get_metadata(0);
	ERR_FAIL_INDEX(idx, releases.size());
	const Release &r = releases[idx];
#if defined(WEB_ENABLED)
	OS::get_singleton()->shell_open(r.html_url);
	return;
#else
	install_confirm_release = idx;
	String text;
	if (r.get_sort_key() < _get_current_sort_key()) {
		text = vformat(TTR("Install %s (%s)?\n\nThis is an older version than the one you are using. Projects saved with newer versions might not open correctly."), r.get_display_name(), r.stable ? TTR("Stable") : TTR("DevBuild"));
	} else if (!r.stable) {
		text = vformat(TTR("Install %s (DevBuild)?\n\nDevBuilds are development builds and may be unstable. Back up your projects."), r.get_display_name());
	} else {
		text = vformat(TTR("Install %s (Stable)?"), r.get_display_name());
	}
	text += "\n\n" + TTR("IPDE will download the update and restart automatically.");
	install_confirm_dialog->set_text(text);
	_popup_dialog(install_confirm_dialog);
#endif
}

void IPDEUpdater::_install_confirmed() {
	_start_install(install_confirm_release);
}

/* Installation */

void IPDEUpdater::_start_install(int p_release) {
	ERR_FAIL_INDEX(p_release, releases.size());
	if (installing_release >= 0) {
		return;
	}
	const Release &r = releases[p_release];

#if defined(WEB_ENABLED)
	OS::get_singleton()->shell_open(r.html_url);
	return;
#endif

	const Asset *asset = _get_platform_asset(r);
	if (!asset) {
		_show_error(vformat(TTR("%s has no build for this platform (%s)."), r.get_display_name(), _get_platform_asset_suffix().trim_prefix("_")));
		return;
	}

#if defined(ANDROID_ENABLED)
	// Android: the system package installer takes care of the update.
	OS::get_singleton()->shell_open(asset->url);
	_set_status(TTR("The APK is being downloaded. Open it to install the update."));
	return;
#endif

	installing_release = p_release;
	const String cache_dir = EditorPaths::get_singleton()->get_cache_dir().path_join("ipde_updates");
	DirAccess::make_dir_recursive_absolute(cache_dir);
	download_path = cache_dir.path_join(asset->name);
	if (FileAccess::exists(download_path)) {
		DirAccess::remove_absolute(download_path);
	}

	download_request->set_download_file(download_path);
	following_redirect = false;
	Error err;
	if (!_get_token().is_empty() && !asset->api_url.is_empty()) {
		// Private repository: download through the API with the token. GitHub answers with a
		// redirect to a signed URL, which must be requested without the token (see _download_completed).
		download_request->set_max_redirects(0);
		err = download_request->request(asset->api_url, _get_api_headers("application/octet-stream"));
	} else {
		download_request->set_max_redirects(8);
		Vector<String> headers;
		headers.push_back("Accept: application/octet-stream");
		headers.push_back("User-Agent: IPDE-Updater/" IPDE_VERSION_NUMBER);
		err = download_request->request(asset->url, headers);
	}
	if (err != OK) {
		installing_release = -1;
		_show_error(vformat(TTR("Couldn't start the download (error %d)."), err));
		return;
	}

	download_progress->set_value(0);
	download_progress->show();
	install_button->set_disabled(true);
	refresh_button->set_disabled(true);
	_set_status(vformat(TTR("Downloading %s..."), r.get_display_name()));
	set_process(true);
}

void IPDEUpdater::_download_completed(int p_result, int p_response_code, const PackedStringArray &p_headers, const PackedByteArray &p_body) {
	if (p_result == HTTPRequest::RESULT_REDIRECT_LIMIT_REACHED && !following_redirect) {
		String location;
		for (const String &h : p_headers) {
			if (h.to_lower().begins_with("location:")) {
				location = h.substr(h.find_char(':') + 1).strip_edges();
			}
		}
		if (!location.is_empty()) {
			following_redirect = true;
			download_request->set_max_redirects(8);
			Vector<String> headers;
			headers.push_back("User-Agent: IPDE-Updater/" IPDE_VERSION_NUMBER);
			if (download_request->request(location, headers) == OK) {
				return; // Keep downloading.
			}
		}
	}
	following_redirect = false;
	set_process(false);
	download_progress->hide();
	refresh_button->set_disabled(false);
	const int release_idx = installing_release;
	installing_release = -1;
	_release_selected();

	if (p_result != HTTPRequest::RESULT_SUCCESS || p_response_code != 200) {
		_show_error(vformat(TTR("Download failed (result %d, HTTP %d)."), p_result, p_response_code));
		return;
	}
	ERR_FAIL_INDEX(release_idx, releases.size());
	const Release &r = releases[release_idx];

	_set_status(vformat(TTR("Installing %s..."), r.get_display_name()));
	String error;
	if (_apply_update(download_path, restart_path, error) != OK) {
		_set_status(TTR("Update failed."), true);
		_show_error(error + "\n\n" + TTR("You can download it manually from the release page."));
		return;
	}
	DirAccess::remove_absolute(download_path);

	_set_status(vformat(TTR("%s installed. Restart IPDE to use it."), r.get_display_name()));
	restart_dialog->set_text(vformat(TTR("%s (%s) has been installed.\n\nRestart IPDE now?"), r.get_display_name(), r.stable ? TTR("Stable") : TTR("DevBuild")));
	_popup_dialog(restart_dialog);
}

Error IPDEUpdater::_extract_zip(const String &p_zip, const String &p_dest, String &r_error) {
	Ref<FileAccess> io_fa;
	zlib_filefunc_def io = zipio_create_io(&io_fa);
	unzFile pkg = unzOpen2(p_zip.utf8().get_data(), &io);
	if (!pkg) {
		r_error = TTR("Can't open the downloaded update package.");
		return ERR_CANT_OPEN;
	}

	int ret = unzGoToFirstFile(pkg);
	while (ret == UNZ_OK) {
		unz_file_info info;
		char fname[16384];
		ret = unzGetCurrentFileInfo(pkg, &info, fname, 16384, nullptr, 0, nullptr, 0);
		if (ret != UNZ_OK) {
			break;
		}
		const String raw_name = String::utf8(fname);
		const String name = raw_name.simplify_path();
		if (name.is_empty() || name.begins_with("__MACOSX") || name.begins_with("/") || name.contains("..")) {
			ret = unzGoToNextFile(pkg);
			continue;
		}
		const String out_path = p_dest.path_join(name);
		if (raw_name.ends_with("/")) {
			DirAccess::make_dir_recursive_absolute(out_path);
			ret = unzGoToNextFile(pkg);
			continue;
		}
		DirAccess::make_dir_recursive_absolute(out_path.get_base_dir());

		Vector<uint8_t> data;
		data.resize(info.uncompressed_size);
		unzOpenCurrentFile(pkg);
		int read = unzReadCurrentFile(pkg, data.ptrw(), data.size());
		unzCloseCurrentFile(pkg);
		if (read < 0 || uint64_t(read) != uint64_t(info.uncompressed_size)) {
			unzClose(pkg);
			r_error = vformat(TTR("Error reading \"%s\" from the update package."), name);
			return ERR_FILE_CORRUPT;
		}

		Ref<FileAccess> f = FileAccess::open(out_path, FileAccess::WRITE);
		if (f.is_null()) {
			unzClose(pkg);
			r_error = vformat(TTR("Can't write \"%s\". Check that you have write permissions to the IPDE installation folder."), out_path);
			return ERR_FILE_CANT_WRITE;
		}
		f->store_buffer(data.ptr(), data.size());
		f.unref();

#if defined(UNIX_ENABLED)
		// Restore Unix permissions (e.g. the executable bit) stored in the ZIP.
		const uint32_t mode = (uint32_t(info.external_fa) >> 16) & 0777;
		if (mode != 0) {
			FileAccess::set_unix_permissions(out_path, BitField<FileAccess::UnixPermissionFlags>(mode));
		}
#endif
		ret = unzGoToNextFile(pkg);
	}
	unzClose(pkg);
	return OK;
}

void IPDEUpdater::_remove_path(const String &p_path) {
	if (DirAccess::dir_exists_absolute(p_path)) {
		Ref<DirAccess> da = DirAccess::open(p_path);
		if (da.is_valid()) {
			da->erase_contents_recursive();
		}
		DirAccess::remove_absolute(p_path);
	} else if (FileAccess::exists(p_path)) {
		DirAccess::remove_absolute(p_path);
	}
}

Error IPDEUpdater::_replace_path(const String &p_new, const String &p_target, String &r_error) {
	Ref<DirAccess> da = DirAccess::create(DirAccess::ACCESS_FILESYSTEM);
	const bool target_exists = FileAccess::exists(p_target) || DirAccess::dir_exists_absolute(p_target);
	const String old_path = p_target + OLD_SUFFIX;
	if (target_exists) {
		_remove_path(old_path);
		// Running executables can't be overwritten on every OS, but they can be renamed.
		if (da->rename(p_target, old_path) != OK) {
			r_error = vformat(TTR("Can't replace \"%s\". Check that you have write permissions to the IPDE installation folder."), p_target);
			return ERR_FILE_CANT_WRITE;
		}
	}
	if (da->rename(p_new, p_target) != OK) {
		if (target_exists) {
			da->rename(old_path, p_target); // Roll back.
		}
		r_error = vformat(TTR("Can't move the new version to \"%s\"."), p_target);
		return ERR_FILE_CANT_WRITE;
	}
	return OK;
}

Error IPDEUpdater::_apply_update(const String &p_zip, String &r_restart_path, String &r_error) {
	const String exe_path = original_executable_path;
	r_restart_path = String();

#if defined(MACOS_ENABLED)
	// .../IPDE.app/Contents/MacOS/IPDE -> replace the whole .app bundle.
	const String bundle = exe_path.get_base_dir().get_base_dir().get_base_dir();
	if (!bundle.ends_with(".app")) {
		r_error = TTR("IPDE is not running from an .app bundle, it can't be updated automatically.");
		return ERR_UNAVAILABLE;
	}
	const String install_dir = bundle.get_base_dir();
#else
	const String install_dir = exe_path.get_base_dir();
#endif

	const String staging = install_dir.path_join(STAGING_DIR);
	_remove_path(staging);
	if (DirAccess::make_dir_recursive_absolute(staging) != OK) {
		r_error = vformat(TTR("Can't write to \"%s\". Check that you have write permissions to the IPDE installation folder."), install_dir);
		return ERR_FILE_CANT_WRITE;
	}

	Error err = _extract_zip(p_zip, staging, r_error);
	if (err != OK) {
		_remove_path(staging);
		return err;
	}

#if defined(MACOS_ENABLED)
	String new_bundle;
	for (const String &d : DirAccess::get_directories_at(staging)) {
		if (d.ends_with(".app")) {
			new_bundle = staging.path_join(d);
			break;
		}
	}
	if (new_bundle.is_empty()) {
		_remove_path(staging);
		r_error = TTR("The update package doesn't contain an .app bundle.");
		return ERR_FILE_CORRUPT;
	}
	err = _replace_path(new_bundle, bundle, r_error);
	_remove_path(staging);
	if (err != OK) {
		return err;
	}
	{
		List<String> args;
		args.push_back("-dr");
		args.push_back("com.apple.quarantine");
		args.push_back(bundle);
		OS::get_singleton()->execute("xattr", args);
	}
	const String macos_dir = bundle.path_join("Contents/MacOS");
	if (FileAccess::exists(macos_dir.path_join("IPDE"))) {
		r_restart_path = macos_dir.path_join("IPDE");
	} else {
		const PackedStringArray files = DirAccess::get_files_at(macos_dir);
		if (!files.is_empty()) {
			r_restart_path = macos_dir.path_join(files[0]);
		}
	}
#else
	// Find the main executable of the new version.
	const PackedStringArray files = DirAccess::get_files_at(staging);
	const PackedStringArray dirs = DirAccess::get_directories_at(staging);
	String main_exe;
	for (const String &f : files) {
#if defined(WINDOWS_ENABLED)
		if (f.to_lower().ends_with(".exe") && !f.to_lower().contains("console")) {
#else
		if (f.begins_with("IPDE") && !f.contains("console") && !f.ends_with(".txt") && !f.ends_with(".so")) {
#endif
			main_exe = f;
			break;
		}
	}
	if (main_exe.is_empty()) {
		_remove_path(staging);
		r_error = TTR("The update package doesn't contain the IPDE executable.");
		return ERR_FILE_CORRUPT;
	}

	// The main executable replaces the running one (keeping its file name, so shortcuts keep working).
	err = _replace_path(staging.path_join(main_exe), exe_path, r_error);
	if (err != OK) {
		_remove_path(staging);
		return err;
	}
#if defined(UNIX_ENABLED)
	FileAccess::set_unix_permissions(exe_path, BitField<FileAccess::UnixPermissionFlags>(0755));
#endif
	for (const String &f : files) {
		if (f == main_exe) {
			continue;
		}
		if (_replace_path(staging.path_join(f), install_dir.path_join(f), r_error) != OK) {
			WARN_PRINT(r_error);
		}
	}
	for (const String &d : dirs) {
		if (_replace_path(staging.path_join(d), install_dir.path_join(d), r_error) != OK) {
			WARN_PRINT(r_error);
		}
	}
	_remove_path(staging);
	r_restart_path = exe_path;
	r_error = String();
#endif
	return OK;
}

void IPDEUpdater::_cleanup_previous_update() {
#if !defined(ANDROID_ENABLED) && !defined(WEB_ENABLED)
	const String exe_path = original_executable_path;
	if (exe_path.is_empty()) {
		return;
	}
#if defined(MACOS_ENABLED)
	const String bundle = exe_path.get_base_dir().get_base_dir().get_base_dir();
	if (!bundle.ends_with(".app")) {
		return;
	}
	const String install_dir = bundle.get_base_dir();
#else
	const String install_dir = exe_path.get_base_dir();
#endif
	if (!DirAccess::dir_exists_absolute(install_dir)) {
		return;
	}
	for (const String &f : DirAccess::get_files_at(install_dir)) {
		if (f.ends_with(OLD_SUFFIX)) {
			_remove_path(install_dir.path_join(f));
		}
	}
	for (const String &d : DirAccess::get_directories_at(install_dir)) {
		if (d.ends_with(OLD_SUFFIX) || d == STAGING_DIR) {
			_remove_path(install_dir.path_join(d));
		}
	}
#endif
}

void IPDEUpdater::_popup_dialog(AcceptDialog *p_dialog) {
	// Exclusive dialogs must be children of the version manager while it's open.
	Node *target = manager_dialog->is_visible() ? static_cast<Node *>(manager_dialog) : static_cast<Node *>(this);
	if (p_dialog->get_parent() != target) {
		p_dialog->get_parent()->remove_child(p_dialog);
		target->add_child(p_dialog);
	}
	p_dialog->popup_centered();
}

void IPDEUpdater::_show_error(const String &p_text) {
	error_dialog->set_text(p_text);
	_popup_dialog(error_dialog);
}

void IPDEUpdater::_restart_confirmed() {
#if !defined(MACOS_ENABLED)
	// The running executable was renamed, make sure the new one is launched.
	OS::get_singleton()->set_restart_executable_path(restart_path);
#endif
	if (context == CONTEXT_EDITOR && EditorNode::get_singleton()) {
		EditorNode::get_singleton()->restart_editor();
	} else {
		List<String> args = OS::get_singleton()->get_cmdline_args();
		OS::get_singleton()->set_restart_on_exit(true, args);
		get_tree()->quit();
	}
}

/* Node */

void IPDEUpdater::_notification(int p_what) {
	switch (p_what) {
		case NOTIFICATION_READY: {
			_cleanup_previous_update();
			const bool check = !EditorSettings::get_singleton()->has_setting("ipde/updates/check_on_startup") || bool(EDITOR_GET("ipde/updates/check_on_startup"));
			if (check) {
				callable_mp(this, &IPDEUpdater::_fetch_releases).call_deferred(true);
			}
		} break;

		case NOTIFICATION_PROCESS: {
			const int total = download_request->get_body_size();
			const int downloaded = download_request->get_downloaded_bytes();
			if (total > 0) {
				download_progress->set_max(total);
				download_progress->set_value(downloaded);
				_set_status(vformat(TTR("Downloading... %s / %s"), String::humanize_size(downloaded), String::humanize_size(total)));
			} else if (downloaded > 0) {
				_set_status(vformat(TTR("Downloading... %s"), String::humanize_size(downloaded)));
			}
		} break;
	}
}

void IPDEUpdater::_bind_methods() {
	ADD_SIGNAL(MethodInfo("releases_updated", PropertyInfo(Variant::STRING, "newest_available")));
}

IPDEUpdater::IPDEUpdater(Context p_context) {
	singleton = this;
	context = p_context;
	original_executable_path = OS::get_singleton()->get_executable_path();
	set_process(false);

	list_request = memnew(HTTPRequest);
	list_request->set_use_threads(true);
	list_request->set_timeout(20);
	add_child(list_request);
	list_request->connect("request_completed", callable_mp(this, &IPDEUpdater::_releases_request_completed));

	download_request = memnew(HTTPRequest);
	download_request->set_use_threads(true);
	add_child(download_request);
	download_request->connect("request_completed", callable_mp(this, &IPDEUpdater::_download_completed));

	// Startup prompt.
	prompt_dialog = memnew(ConfirmationDialog);
	prompt_dialog->set_title(TTR("IPDE Update Available"));
	prompt_dialog->set_ok_button_text(TTR("Update"));
	prompt_dialog->set_cancel_button_text(TTR("Not Now"));
	prompt_dialog->add_button(TTR("Skip This Version"), true, "skip");
	prompt_text = memnew(RichTextLabel);
	prompt_text->set_fit_content(true);
	prompt_text->set_selection_enabled(true);
	prompt_text->set_custom_minimum_size(Size2(520, 160) * EDSCALE);
	prompt_text->set_v_size_flags(Control::SIZE_EXPAND_FILL);
	prompt_dialog->add_child(prompt_text);
	add_child(prompt_dialog);
	prompt_dialog->connect(SceneStringName(confirmed), callable_mp(this, &IPDEUpdater::_prompt_confirmed));
	prompt_dialog->connect("canceled", callable_mp(this, &IPDEUpdater::_prompt_canceled));
	prompt_dialog->connect("custom_action", callable_mp(this, &IPDEUpdater::_prompt_custom_action));

	// Version manager.
	manager_dialog = memnew(AcceptDialog);
	manager_dialog->set_title(TTR("IPDE Versions & Updates"));
	manager_dialog->set_ok_button_text(TTR("Close"));
	add_child(manager_dialog);

	VBoxContainer *vb = memnew(VBoxContainer);
	manager_dialog->add_child(vb);

	HBoxContainer *top = memnew(HBoxContainer);
	vb->add_child(top);
	installed_label = memnew(Label);
	installed_label->set_text(vformat(TTR("Installed: %s"), String(IPDE_VERSION_FULL_NAME)));
	installed_label->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	top->add_child(installed_label);
	show_devbuilds_check = memnew(CheckBox);
	show_devbuilds_check->set_text(TTR("Show DevBuilds"));
	show_devbuilds_check->set_pressed(true);
	show_devbuilds_check->connect(SceneStringName(toggled), callable_mp(this, &IPDEUpdater::_update_release_tree).unbind(1));
	top->add_child(show_devbuilds_check);
	refresh_button = memnew(Button);
	refresh_button->set_text(TTR("Refresh"));
	refresh_button->connect(SceneStringName(pressed), callable_mp(this, &IPDEUpdater::_refresh_pressed));
	top->add_child(refresh_button);

	HSplitContainer *split = memnew(HSplitContainer);
	split->set_v_size_flags(Control::SIZE_EXPAND_FILL);
	vb->add_child(split);

	release_tree = memnew(Tree);
	release_tree->set_columns(4);
	release_tree->set_column_titles_visible(true);
	release_tree->set_column_title(0, TTR("Version"));
	release_tree->set_column_title(1, TTR("Channel"));
	release_tree->set_column_title(2, TTR("Date"));
	release_tree->set_column_title(3, TTR("Status"));
	release_tree->set_column_expand(1, false);
	release_tree->set_column_custom_minimum_width(1, 90 * EDSCALE);
	release_tree->set_column_expand(2, false);
	release_tree->set_column_custom_minimum_width(2, 120 * EDSCALE);
	release_tree->set_hide_root(true);
	release_tree->set_select_mode(Tree::SELECT_ROW);
	release_tree->set_custom_minimum_size(Size2(480, 300) * EDSCALE);
	release_tree->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	release_tree->connect(SceneStringName(item_selected), callable_mp(this, &IPDEUpdater::_release_selected));
	split->add_child(release_tree);

	notes_label = memnew(RichTextLabel);
	notes_label->set_selection_enabled(true);
	notes_label->set_custom_minimum_size(Size2(300, 300) * EDSCALE);
	notes_label->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	split->add_child(notes_label);

	HBoxContainer *bottom = memnew(HBoxContainer);
	vb->add_child(bottom);
	status_label = memnew(Label);
	status_label->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	status_label->set_clip_text(true);
	status_label->set_text_overrun_behavior(TextServer::OVERRUN_TRIM_ELLIPSIS);
	status_label->set_mouse_filter(Control::MOUSE_FILTER_PASS);
	bottom->add_child(status_label);
	download_progress = memnew(ProgressBar);
	download_progress->set_custom_minimum_size(Size2(200, 0) * EDSCALE);
	download_progress->set_v_size_flags(Control::SIZE_SHRINK_CENTER);
	download_progress->hide();
	bottom->add_child(download_progress);
	open_page_button = memnew(Button);
	open_page_button->set_text(TTR("Open on GitHub"));
	open_page_button->connect(SceneStringName(pressed), callable_mp(this, &IPDEUpdater::_open_page_pressed));
	bottom->add_child(open_page_button);
	install_button = memnew(Button);
	install_button->set_text(TTR("Install"));
	install_button->set_disabled(true);
	install_button->connect(SceneStringName(pressed), callable_mp(this, &IPDEUpdater::_install_pressed));
	bottom->add_child(install_button);

	install_confirm_dialog = memnew(ConfirmationDialog);
	install_confirm_dialog->set_title(TTR("Install IPDE Version"));
	install_confirm_dialog->set_ok_button_text(TTR("Install"));
	add_child(install_confirm_dialog);
	install_confirm_dialog->connect(SceneStringName(confirmed), callable_mp(this, &IPDEUpdater::_install_confirmed));

	restart_dialog = memnew(ConfirmationDialog);
	restart_dialog->set_title(TTR("IPDE Updated"));
	restart_dialog->set_ok_button_text(TTR("Restart Now"));
	restart_dialog->set_cancel_button_text(TTR("Later"));
	add_child(restart_dialog);
	restart_dialog->connect(SceneStringName(confirmed), callable_mp(this, &IPDEUpdater::_restart_confirmed));

	error_dialog = memnew(AcceptDialog);
	error_dialog->set_title(TTR("IPDE Updater"));
	add_child(error_dialog);
}

IPDEUpdater::~IPDEUpdater() {
	if (singleton == this) {
		singleton = nullptr;
	}
}
