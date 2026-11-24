// IPDE platform layer for Linux/X11. Only used to develop and test IPDE; releases target Windows.

#include "platform/platform.h"

#include <imgui.h>

#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>

#include <cfloat>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <sys/select.h>
#include <sys/stat.h>
#include <unistd.h>

namespace ipde::platform {

static Display *g_display = nullptr;
static Window g_window = 0;
static GC g_gc = nullptr;
static XImage *g_image = nullptr;
static Atom g_wm_delete = 0;
static bool g_quit = false;
static int g_width = 1280;
static int g_height = 800;
static std::chrono::steady_clock::time_point g_start;

static ImGuiKey keysym_to_imgui(KeySym p_sym) {
	switch (p_sym) {
		case XK_Tab: return ImGuiKey_Tab;
		case XK_Left: return ImGuiKey_LeftArrow;
		case XK_Right: return ImGuiKey_RightArrow;
		case XK_Up: return ImGuiKey_UpArrow;
		case XK_Down: return ImGuiKey_DownArrow;
		case XK_Page_Up: return ImGuiKey_PageUp;
		case XK_Page_Down: return ImGuiKey_PageDown;
		case XK_Home: return ImGuiKey_Home;
		case XK_End: return ImGuiKey_End;
		case XK_Delete: return ImGuiKey_Delete;
		case XK_BackSpace: return ImGuiKey_Backspace;
		case XK_Return: return ImGuiKey_Enter;
		case XK_Escape: return ImGuiKey_Escape;
		case XK_space: return ImGuiKey_Space;
		case XK_F5: return ImGuiKey_F5;
		default: break;
	}
	if (p_sym >= XK_a && p_sym <= XK_z) {
		return (ImGuiKey)(ImGuiKey_A + (int)(p_sym - XK_a));
	}
	if (p_sym >= XK_0 && p_sym <= XK_9) {
		return (ImGuiKey)(ImGuiKey_0 + (int)(p_sym - XK_0));
	}
	return ImGuiKey_None;
}

bool create_window(const char *p_title, int p_width, int p_height) {
	g_start = std::chrono::steady_clock::now();
	g_display = XOpenDisplay(nullptr);
	if (!g_display) {
		return false;
	}
	const int screen = DefaultScreen(g_display);
	g_window = XCreateSimpleWindow(g_display, RootWindow(g_display, screen), 0, 0, p_width, p_height, 0, 0, 0);
	XStoreName(g_display, g_window, p_title);
	XSelectInput(g_display, g_window, ExposureMask | KeyPressMask | KeyReleaseMask | ButtonPressMask | ButtonReleaseMask | PointerMotionMask | StructureNotifyMask | FocusChangeMask);
	g_wm_delete = XInternAtom(g_display, "WM_DELETE_WINDOW", False);
	XSetWMProtocols(g_display, g_window, &g_wm_delete, 1);
	XMapWindow(g_display, g_window);
	g_gc = XCreateGC(g_display, g_window, 0, nullptr);
	g_width = p_width;
	g_height = p_height;
	XFlush(g_display);
	return true;
}

void destroy_window() {
	if (g_image) {
		g_image->data = nullptr; // Owned by the renderer.
		XDestroyImage(g_image);
		g_image = nullptr;
	}
	if (g_display) {
		XFreeGC(g_display, g_gc);
		XDestroyWindow(g_display, g_window);
		XCloseDisplay(g_display);
		g_display = nullptr;
	}
}

bool pump_events(double p_wait_seconds) {
	if (!g_display) {
		return false;
	}
	if (p_wait_seconds > 0.0 && XPending(g_display) == 0) {
		const int fd = ConnectionNumber(g_display);
		fd_set set;
		FD_ZERO(&set);
		FD_SET(fd, &set);
		timeval tv;
		tv.tv_sec = (long)p_wait_seconds;
		tv.tv_usec = (long)((p_wait_seconds - (double)tv.tv_sec) * 1e6);
		select(fd + 1, &set, nullptr, nullptr, &tv);
	}
	ImGuiIO &io = ImGui::GetIO();
	while (XPending(g_display)) {
		XEvent ev;
		XNextEvent(g_display, &ev);
		switch (ev.type) {
			case ClientMessage:
				if ((Atom)ev.xclient.data.l[0] == g_wm_delete) {
					g_quit = true;
				}
				break;
			case ConfigureNotify:
				g_width = ev.xconfigure.width;
				g_height = ev.xconfigure.height;
				break;
			case MotionNotify:
				io.AddMousePosEvent((float)ev.xmotion.x, (float)ev.xmotion.y);
				break;
			case ButtonPress:
			case ButtonRelease: {
				const bool down = ev.type == ButtonPress;
				const unsigned b = ev.xbutton.button;
				if (b == Button1) {
					io.AddMouseButtonEvent(0, down);
				} else if (b == Button3) {
					io.AddMouseButtonEvent(1, down);
				} else if (b == Button2) {
					io.AddMouseButtonEvent(2, down);
				} else if (down && (b == 4 || b == 5)) {
					io.AddMouseWheelEvent(0.0f, b == 4 ? 1.0f : -1.0f);
				}
				break;
			}
			case KeyPress:
			case KeyRelease: {
				const bool down = ev.type == KeyPress;
				io.AddKeyEvent(ImGuiMod_Ctrl, (ev.xkey.state & ControlMask) != 0);
				io.AddKeyEvent(ImGuiMod_Shift, (ev.xkey.state & ShiftMask) != 0);
				char text[32] = {};
				KeySym sym = 0;
				const int len = XLookupString(&ev.xkey, text, sizeof(text) - 1, &sym, nullptr);
				const ImGuiKey key = keysym_to_imgui(sym);
				if (key != ImGuiKey_None) {
					io.AddKeyEvent(key, down);
				}
				if (down && len > 0 && (unsigned char)text[0] >= 32 && text[0] != 127) {
					io.AddInputCharactersUTF8(text);
				}
				break;
			}
			case FocusIn:
			case FocusOut:
				io.AddFocusEvent(ev.type == FocusIn);
				break;
			default:
				break;
		}
	}
	return !g_quit;
}

void present(const uint32_t *p_pixels, int p_width, int p_height) {
	if (!g_display) {
		return;
	}
	if (!g_image || g_image->width != p_width || g_image->height != p_height) {
		if (g_image) {
			g_image->data = nullptr;
			XDestroyImage(g_image);
		}
		const int screen = DefaultScreen(g_display);
		g_image = XCreateImage(g_display, DefaultVisual(g_display, screen), DefaultDepth(g_display, screen), ZPixmap, 0, nullptr, p_width, p_height, 32, p_width * 4);
	}
	g_image->data = (char *)p_pixels;
	XPutImage(g_display, g_window, g_gc, g_image, 0, 0, 0, 0, p_width, p_height);
	XFlush(g_display);
}

void get_window_size(int &r_width, int &r_height) {
	r_width = g_width;
	r_height = g_height;
}

float get_dpi_scale() {
	const char *env = getenv("IPDE_SCALE");
	return env ? (float)atof(env) : 1.0f;
}

void set_window_title(const std::string &p_title) {
	if (g_display) {
		XStoreName(g_display, g_window, p_title.c_str());
	}
}

void set_mouse_cursor(int p_imgui_cursor) {}

double get_time() {
	return std::chrono::duration<double>(std::chrono::steady_clock::now() - g_start).count();
}

std::string get_executable_path() {
	char buffer[4096];
	const ssize_t len = readlink("/proc/self/exe", buffer, sizeof(buffer) - 1);
	return len > 0 ? std::string(buffer, len) : std::string();
}

std::string get_app_data_dir() {
	const char *home = getenv("HOME");
	const std::string dir = std::string(home ? home : ".") + "/.local/share/IPDE";
	std::error_code ec;
	std::filesystem::create_directories(dir, ec);
	return dir;
}

std::string get_documents_dir() {
	const char *home = getenv("HOME");
	return std::string(home ? home : ".") + "/Documents";
}

std::string get_temp_dir() {
	return "/tmp";
}

bool create_process(const std::string &p_executable, const std::vector<std::string> &p_args) {
	const pid_t pid = fork();
	if (pid == 0) {
		std::vector<char *> argv;
		argv.push_back((char *)p_executable.c_str());
		for (const std::string &a : p_args) {
			argv.push_back((char *)a.c_str());
		}
		argv.push_back(nullptr);
		execv(p_executable.c_str(), argv.data());
		_exit(127);
	}
	return pid > 0;
}

void open_url(const std::string &p_url) {
	create_process("/usr/bin/xdg-open", { p_url });
}

void open_folder(const std::string &p_path) {
	create_process("/usr/bin/xdg-open", { p_path });
}

std::string pick_folder(const std::string &p_title) {
	return std::string();
}

} // namespace ipde::platform
