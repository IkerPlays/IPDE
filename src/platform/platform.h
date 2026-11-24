#pragma once

// IPDE platform layer: window, input, paths and processes.
// Windows (Win32) is the target platform; the X11 backend is only used to develop and test on Linux.

#include <cstdint>
#include <string>
#include <vector>

namespace ipde::platform {

bool create_window(const char *p_title, int p_width, int p_height);
void destroy_window();

// Processes pending events and forwards input to Dear ImGui.
// If p_wait_seconds > 0, waits up to that time for an event first.
// Returns false when the user closed the window.
bool pump_events(double p_wait_seconds);

// Shows a frame. p_pixels is p_width * p_height 0xAARRGGBB pixels, top row first.
void present(const uint32_t *p_pixels, int p_width, int p_height);

void get_window_size(int &r_width, int &r_height);
float get_dpi_scale();
void set_window_title(const std::string &p_title);
// Borderless fullscreen on the current monitor (false restores the window).
void set_fullscreen(bool p_fullscreen);
bool is_fullscreen();
void set_mouse_cursor(int p_imgui_cursor);

double get_time();

std::string get_executable_path();
std::string get_app_data_dir(); // Per-user writable data folder (created if missing).
std::string get_documents_dir();
std::string get_temp_dir();

bool create_process(const std::string &p_executable, const std::vector<std::string> &p_args);
void open_url(const std::string &p_url);
void open_folder(const std::string &p_path);

// Native folder picker. Returns an empty string if canceled or unavailable.
std::string pick_folder(const std::string &p_title);
// Native file picker. p_filter_name e.g. "Modelos OBJ", p_pattern e.g. "*.obj". Empty if canceled.
std::string pick_file(const std::string &p_title, const std::string &p_filter_name, const std::string &p_pattern);

} // namespace ipde::platform
