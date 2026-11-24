#include "app/updater.h"

#include "app/app.h"
#include "core/fs.h"
#include "core/json.h"
#include "platform/platform.h"
#include "version.h"

#include <algorithm>
#include <cstdlib>

#ifndef _WIN32
#include <sys/stat.h>
#endif

namespace ipde {

#ifdef _WIN32
static const char *ASSET_SUFFIX = "_windows_x86_64.exe";
#else
static const char *ASSET_SUFFIX = "_linux_x86_64"; // Development builds only.
#endif

std::string Release::display_name() const {
	return "IPDE v." + std::to_string(major) + "." + std::to_string(minor) + "." + std::to_string(patch);
}

int64_t Updater::current_sort_key() {
	const bool stable = std::string(IPDE_VERSION_CHANNEL) == "stable";
	return ((int64_t)IPDE_VERSION_MAJOR * 1000000 + (int64_t)IPDE_VERSION_MINOR * 1000 + IPDE_VERSION_PATCH) * 2 + (stable ? 1 : 0);
}

bool Updater::parse_version(const std::string &p_tag, int &r_major, int &r_minor, int &r_patch) {
	// Accepts "v1.2.3", "v.1.2.3", "1.2.3", "v1.2.3-stable"...
	size_t i = 0;
	while (i < p_tag.size() && (p_tag[i] == 'v' || p_tag[i] == 'V' || p_tag[i] == '.')) {
		i++;
	}
	int parts[3] = { 0, 0, 0 };
	for (int k = 0; k < 3; k++) {
		if (i >= p_tag.size() || !isdigit((unsigned char)p_tag[i])) {
			return false;
		}
		parts[k] = atoi(p_tag.c_str() + i);
		while (i < p_tag.size() && isdigit((unsigned char)p_tag[i])) {
			i++;
		}
		if (k < 2) {
			if (i >= p_tag.size() || p_tag[i] != '.') {
				return false;
			}
			i++;
		}
	}
	r_major = parts[0];
	r_minor = parts[1];
	r_patch = parts[2];
	return true;
}

void Updater::_cleanup_old_files() {
	// Leftovers from a previous update (the old executable can't be deleted while it runs).
	const std::string dir = fs::get_dir(exe_path);
	for (const std::string &f : fs::list_dir(dir)) {
		if (f.size() > 9 && (f.compare(f.size() - 9, 9, ".ipde_old") == 0)) {
			fs::remove_path(fs::join(dir, f));
		}
		if (f == "IPDE.update.tmp") {
			fs::remove_path(fs::join(dir, f));
		}
	}
}

void Updater::init() {
	exe_path = platform::get_executable_path();
	_cleanup_old_files();
}

void Updater::check(bool p_automatic) {
	if (is_busy()) {
		return;
	}
	automatic_check = p_automatic;
	state = State::CHECKING;
	status = "Buscando versiones...";
	request = std::make_shared<http::Request>();
	const char *api = getenv("IPDE_UPDATER_API");
	request->url = api ? api : "https://api.github.com/repos/" IPDE_REPOSITORY "/releases?per_page=100";
	request->headers = { "Accept: application/vnd.github+json", "User-Agent: IPDE-Updater/" IPDE_VERSION_NUMBER };
	http::start(request);
}

void Updater::update() {
	if (!request || !request->done) {
		return;
	}
	std::shared_ptr<http::Request> req = request;
	request.reset();
	if (state == State::CHECKING) {
		_on_releases(req);
	} else if (state == State::DOWNLOADING) {
		_on_download(req);
	}
}

float Updater::get_progress() const {
	if (state != State::DOWNLOADING || !request) {
		return 0.0f;
	}
	const int64_t total = request->total > 0 ? (int64_t)request->total : installing.asset_size;
	return total > 0 ? std::min(1.0f, (float)request->downloaded / (float)total) : 0.0f;
}

void Updater::_on_releases(const std::shared_ptr<http::Request> &p_req) {
	state = State::IDLE;
	if (p_req->status != 200) {
		state = State::FAILED;
		status = p_req->status == 0 ? "No se pudo conectar con GitHub." : "GitHub respondió con el código " + std::to_string(p_req->status) + ".";
		return;
	}
	const Json list = Json::parse(p_req->body);
	if (!list.is_array()) {
		state = State::FAILED;
		status = "Respuesta de GitHub no válida.";
		return;
	}
	releases.clear();
	for (const Json &r : list.get_items()) {
		if (r["draft"].as_bool(false)) {
			continue;
		}
		Release rel;
		rel.tag = r["tag_name"].as_string();
		if (!parse_version(rel.tag, rel.major, rel.minor, rel.patch)) {
			continue;
		}
		rel.name = rel.display_name();
		rel.body = r["body"].as_string();
		rel.date = r["published_at"].as_string().substr(0, 10);
		rel.html_url = r["html_url"].as_string();
		std::string lower = rel.tag;
		std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
		if (lower.size() >= 7 && lower.compare(lower.size() - 7, 7, "-stable") == 0) {
			rel.stable = true;
		} else if (lower.find("-dev") != std::string::npos) {
			rel.stable = false;
		} else {
			rel.stable = !r["prerelease"].as_bool(false);
		}
		const std::string suffix = ASSET_SUFFIX;
		for (const Json &a : r["assets"].get_items()) {
			const std::string name = a["name"].as_string();
			if (name.size() >= suffix.size() && name.compare(name.size() - suffix.size(), suffix.size(), suffix) == 0) {
				rel.asset_url = a["browser_download_url"].as_string();
				rel.asset_size = (int64_t)a["size"].as_number(0);
			}
		}
		releases.push_back(rel);
	}
	std::sort(releases.begin(), releases.end(), [](const Release &a, const Release &b) { return a.sort_key() > b.sort_key(); });
	checked = true;

	const Release *newest = get_newest_available();
	status = newest ? "Hay una versión nueva: " + newest->display_name() + "." : "Tienes la última versión.";

	App &app = App::get();
	// Loop guard: if this release was installed automatically moments ago and we're still on an
	// older version, the package is wrong. Don't install it again.
	const bool recently_auto_installed = newest && app.settings.last_auto_tag == newest->tag && fs::now_unix() - app.settings.last_auto_time < 600;
	if (automatic_check && newest && app.settings.auto_update && newest->tag != app.settings.skipped_tag && !recently_auto_installed) {
		app.settings.last_auto_tag = newest->tag;
		app.settings.last_auto_time = fs::now_unix();
		app.settings.save();
		install(*newest, true);
	} else if (automatic_check && newest) {
		app.toast(newest->display_name() + " está disponible. Instálala desde Versiones.");
	}
}

const Release *Updater::get_newest_available() const {
	const bool on_devbuild = std::string(IPDE_VERSION_CHANNEL) != "stable";
	const bool include_dev = on_devbuild || App::get().settings.include_devbuilds;
	const int64_t current = current_sort_key();
	for (const Release &r : releases) {
		if (r.sort_key() <= current) {
			break;
		}
		if (!r.stable && !include_dev) {
			continue;
		}
		if (r.asset_url.empty()) {
			continue;
		}
		return &r;
	}
	return nullptr;
}

const Release *Updater::get_installed_release() const {
	return state == State::INSTALLED ? &installing : nullptr;
}

void Updater::install(const Release &p_release, bool p_automatic) {
	if (is_busy() || p_release.asset_url.empty()) {
		return;
	}
	automatic_install = p_automatic;
	installing = p_release;
	download_path = fs::join(fs::get_dir(exe_path), "IPDE.update.tmp");
	state = State::DOWNLOADING;
	status = "Descargando " + p_release.display_name() + "...";
	request = std::make_shared<http::Request>();
	request->url = p_release.asset_url;
	request->output_file = download_path;
	request->headers = { "Accept: application/octet-stream", "User-Agent: IPDE-Updater/" IPDE_VERSION_NUMBER };
	http::start(request);
	if (p_automatic) {
		App::get().toast("Actualizando IPDE a " + p_release.display_name() + "...");
	}
}

void Updater::_on_download(const std::shared_ptr<http::Request> &p_req) {
	App &app = App::get();
	auto fail = [&](const std::string &p_msg) {
		state = State::FAILED;
		status = p_msg;
		fs::remove_path(download_path);
		app.toast(p_msg, true);
	};
	if (p_req->status != 200 || !p_req->error.empty()) {
		fail("La descarga ha fallado (" + (p_req->error.empty() ? "HTTP " + std::to_string(p_req->status) : p_req->error) + ").");
		return;
	}
	std::string data;
	if (!fs::read_file(download_path, data) || data.size() < 1024) {
		fail("El archivo descargado no es válido.");
		return;
	}
	// Running executables can't be overwritten on Windows, but they can be renamed.
	const std::string old_path = exe_path + ".ipde_old";
	fs::remove_path(old_path);
	if (!fs::rename_path(exe_path, old_path)) {
		fail("No se pudo reemplazar el ejecutable (¿permisos de la carpeta?).");
		return;
	}
	if (!fs::rename_path(download_path, exe_path)) {
		fs::rename_path(old_path, exe_path);
		fail("No se pudo instalar la nueva versión.");
		return;
	}
#ifndef _WIN32
	chmod(exe_path.c_str(), 0755);
#endif
	state = State::INSTALLED;
	status = installing.display_name() + " instalada. Reinicia IPDE para usarla.";
	if (automatic_install && app.screen == App::Screen::DASHBOARD) {
		// Nothing to lose in the dashboard: restart right away.
		restart_into_new_version();
	} else {
		app.toast(installing.display_name() + " instalada. Se usará al reiniciar IPDE.");
	}
}

void Updater::restart_into_new_version() {
	if (platform::create_process(exe_path, {})) {
		App::get().quit_requested = true;
	} else {
		App::get().toast("No se pudo reiniciar IPDE. Ábrelo de nuevo manualmente.", true);
	}
}

} // namespace ipde
