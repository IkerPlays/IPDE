/**************************************************************************/
/*  ipde_updater.h                                                        */
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

#pragma once

#include "scene/main/node.h"

class AcceptDialog;
class Button;
class CheckBox;
class ConfirmationDialog;
class HTTPRequest;
class Label;
class ProgressBar;
class RichTextLabel;
class Tree;

// IPDE in-app updater.
//
// Releases are published on GitHub Releases as "IPDE v.X.Y.Z" with tags
// "vX.Y.Z-stable" or "vX.Y.Z-devbuild" (devbuilds are marked as pre-releases).
// On startup, newer stable releases are offered to the user (who can accept,
// postpone or skip them). The version manager dialog allows installing any
// release manually: newer or older stables and devbuilds.
class IPDEUpdater : public Node {
	GDCLASS(IPDEUpdater, Node);

public:
	enum Context {
		CONTEXT_PROJECT_MANAGER,
		CONTEXT_EDITOR,
	};

	struct Asset {
		String name;
		String url;
		String api_url;
		int64_t size = 0;
	};

	struct Release {
		String tag;
		String name;
		String html_url;
		String body;
		String published_at;
		int major = 0;
		int minor = 0;
		int patch = 0;
		bool stable = false;
		Vector<Asset> assets;

		int64_t get_sort_key() const;
		String get_display_name() const;
	};

private:
	static IPDEUpdater *singleton;

	Context context = CONTEXT_PROJECT_MANAGER;
	String original_executable_path;

	HTTPRequest *list_request = nullptr;
	HTTPRequest *download_request = nullptr;

	Vector<Release> releases;
	bool fetching = false;
	bool fetch_is_automatic = false;
	bool releases_loaded = false;
	int installing_release = -1;
	String download_path;
	String newest_available;

	// Startup prompt.
	ConfirmationDialog *prompt_dialog = nullptr;
	RichTextLabel *prompt_text = nullptr;
	int prompt_release = -1;

	// Version manager.
	AcceptDialog *manager_dialog = nullptr;
	Label *installed_label = nullptr;
	CheckBox *show_devbuilds_check = nullptr;
	Button *refresh_button = nullptr;
	Button *install_button = nullptr;
	Button *open_page_button = nullptr;
	Tree *release_tree = nullptr;
	RichTextLabel *notes_label = nullptr;
	Label *status_label = nullptr;
	ProgressBar *download_progress = nullptr;

	ConfirmationDialog *install_confirm_dialog = nullptr;
	int install_confirm_release = -1;
	ConfirmationDialog *restart_dialog = nullptr;
	AcceptDialog *whats_new_dialog = nullptr;
	RichTextLabel *whats_new_text = nullptr;
	AcceptDialog *error_dialog = nullptr;

	static int64_t _get_current_sort_key();
	static String _get_platform_asset_suffix();
	static bool _parse_version(const String &p_tag, int &r_major, int &r_minor, int &r_patch);
	String _get_repository() const;
	String _get_token() const;
	Vector<String> _get_api_headers(const String &p_accept) const;
	bool following_redirect = false;
	String _get_config_path() const;
	Variant _get_config_value(const String &p_key, const Variant &p_default) const;
	void _set_config_value(const String &p_key, const Variant &p_value);

	void _fetch_releases(bool p_automatic);
	void _releases_request_completed(int p_result, int p_response_code, const PackedStringArray &p_headers, const PackedByteArray &p_body);
	void _check_for_prompt();
	void _check_whats_new();
	void _notify_available_update();
	void _set_status(const String &p_text, bool p_error = false);

	void _prompt_confirmed();
	void _prompt_canceled();
	void _prompt_custom_action(const StringName &p_action);

	void _update_release_tree();
	void _release_selected();
	void _refresh_pressed();
	void _install_pressed();
	void _open_page_pressed();
	void _install_confirmed();

	const Asset *_get_platform_asset(const Release &p_release) const;
	void _start_install(int p_release);
	void _download_completed(int p_result, int p_response_code, const PackedStringArray &p_headers, const PackedByteArray &p_body);
	Error _extract_zip(const String &p_zip, const String &p_dest, String &r_error);
	Error _replace_path(const String &p_new, const String &p_target, String &r_error);
	Error _apply_update(const String &p_zip, String &r_restart_path, String &r_error);
	void _remove_path(const String &p_path);
	void _cleanup_previous_update();
	void _popup_dialog(AcceptDialog *p_dialog);
	void _show_error(const String &p_text);
	void _restart_confirmed();

	String restart_path;

protected:
	void _notification(int p_what);
	static void _bind_methods();

public:
	static IPDEUpdater *get_singleton() { return singleton; }

	void show_version_manager();
	String get_newest_available() const { return newest_available; }

	IPDEUpdater(Context p_context);
	~IPDEUpdater();
};
