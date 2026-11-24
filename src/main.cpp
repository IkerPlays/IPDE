// IPDE - game engine by IkersStudio.

#include "app/app.h"
#include "app/game.h"
#include "core/audio.h"
#include "core/fs.h"
#include "platform/platform.h"
#include "version.h"

#include <imgui.h>
#include <imgui_internal.h>

#include <algorithm>
#include <cstdio>

#include <string>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#include <shellapi.h>
#endif

using namespace ipde;

static std::vector<std::string> get_args(int argc, char **argv) {
	std::vector<std::string> args;
#ifdef _WIN32
	int count = 0;
	LPWSTR *wargs = CommandLineToArgvW(GetCommandLineW(), &count);
	for (int i = 1; i < count; i++) {
		const int len = WideCharToMultiByte(CP_UTF8, 0, wargs[i], -1, nullptr, 0, nullptr, nullptr);
		std::string s(len > 0 ? len - 1 : 0, '\0');
		WideCharToMultiByte(CP_UTF8, 0, wargs[i], -1, s.data(), len, nullptr, nullptr);
		args.push_back(s);
	}
	LocalFree(wargs);
#else
	for (int i = 1; i < argc; i++) {
		args.push_back(argv[i]);
	}
#endif
	return args;
}

static int run_editor(const std::string &p_open_project) {
	if (!platform::create_window("IPDE", 1280, 800)) {
		return 1;
	}
	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	ImGuiIO &io = ImGui::GetIO();
	io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures | ImGuiBackendFlags_HasMouseCursors;
	io.BackendRendererName = "ipde_soft";
	io.BackendPlatformName = "ipde_platform";

	App &app = App::get();
	app.init();
	if (!p_open_project.empty()) {
		app.open_project(p_open_project);
	}

	int idle_frames = 0;
	while (!app.quit_requested) {
		// Redraw continuously while something changes; otherwise sleep until there's input.
		const bool busy = app.needs_redraw() || idle_frames < 3;
		if (!platform::pump_events(busy ? 1.0 / 60.0 : 0.25)) {
			break;
		}
		const bool had_input = io.InputQueueCharacters.Size > 0 || ImGui::GetCurrentContext()->InputEventsQueue.Size > 0;
		idle_frames = had_input ? 0 : idle_frames + 1;

		int w, h;
		platform::get_window_size(w, h);
		if (w <= 0 || h <= 0) {
			continue;
		}
		io.DisplaySize = ImVec2((float)w, (float)h);
		static double last = platform::get_time();
		const double now = platform::get_time();
		io.DeltaTime = (float)std::max(1e-4, now - last);
		last = now;

		ImGui::NewFrame();
		app.frame();
		ImGui::Render();
		platform::set_mouse_cursor(ImGui::GetMouseCursor());

		app.renderer.resize(w, h);
		app.renderer.clear(theme::BG);
		app.renderer.render(ImGui::GetDrawData());
		platform::present(app.renderer.get_pixels(), w, h);
	}

	app.shutdown();
	audio::shutdown();
	app.renderer.shutdown();
	ImGui::DestroyContext();
	platform::destroy_window();
	return 0;
}

#ifdef _WIN32
int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int) {
	const std::vector<std::string> args = get_args(0, nullptr);
#else
int main(int argc, char **argv) {
	const std::vector<std::string> args = get_args(argc, argv);
#endif
	// "--new <folder> [2d|3d]": create a project with the example scene and exit.
	for (size_t i = 0; i + 1 < args.size(); i++) {
		if (args[i] == "--new") {
			std::string err;
			const bool is3d = !(i + 2 < args.size() && args[i + 2] == "2d");
			if (!Project::create(args[i + 1], fs::get_file_name(args[i + 1]), is3d, err)) {
				fprintf(stderr, "%s\n", err.c_str());
				return 1;
			}
			return 0;
		}
	}
	// "--run <project folder>": play a project (used by the editor's Run button).
	for (size_t i = 0; i + 1 < args.size(); i++) {
		if (args[i] == "--run") {
			return run_game(args[i + 1]);
		}
	}
	// Exported game: the project data lives in a "data" folder next to the executable.
	const std::string data_dir = fs::join(fs::get_dir(platform::get_executable_path()), "data");
	if (fs::exists(fs::join(data_dir, "project.ipde"))) {
		return run_game(data_dir);
	}
	// "--open <project folder>": open a project directly in the editor.
	std::string open_project;
	for (size_t i = 0; i + 1 < args.size(); i++) {
		if (args[i] == "--open") {
			open_project = args[i + 1];
		}
	}
	return run_editor(open_project);
}
