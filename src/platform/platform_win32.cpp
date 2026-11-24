// IPDE platform layer for Windows (Win32).

#include "platform/platform.h"

#include <imgui.h>

#include <windows.h>
#include <windowsx.h>
#include <dwmapi.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shobjidl.h>

#include <cfloat>
#include <string>

namespace ipde::platform {

static HWND g_hwnd = nullptr;
static bool g_quit = false;
static int g_width = 1280;
static int g_height = 800;
static float g_dpi_scale = 1.0f;
static LARGE_INTEGER g_perf_freq = {};
static LARGE_INTEGER g_perf_start = {};
static int g_cursor = ImGuiMouseCursor_Arrow;

static std::wstring to_wide(const std::string &p_utf8) {
	if (p_utf8.empty()) {
		return std::wstring();
	}
	const int len = MultiByteToWideChar(CP_UTF8, 0, p_utf8.c_str(), (int)p_utf8.size(), nullptr, 0);
	std::wstring out(len, L'\0');
	MultiByteToWideChar(CP_UTF8, 0, p_utf8.c_str(), (int)p_utf8.size(), out.data(), len);
	return out;
}

static std::string to_utf8(const std::wstring &p_wide) {
	if (p_wide.empty()) {
		return std::string();
	}
	const int len = WideCharToMultiByte(CP_UTF8, 0, p_wide.c_str(), (int)p_wide.size(), nullptr, 0, nullptr, nullptr);
	std::string out(len, '\0');
	WideCharToMultiByte(CP_UTF8, 0, p_wide.c_str(), (int)p_wide.size(), out.data(), len, nullptr, nullptr);
	return out;
}

static ImGuiKey vk_to_imgui_key(WPARAM p_vk) {
	switch (p_vk) {
		case VK_TAB: return ImGuiKey_Tab;
		case VK_LEFT: return ImGuiKey_LeftArrow;
		case VK_RIGHT: return ImGuiKey_RightArrow;
		case VK_UP: return ImGuiKey_UpArrow;
		case VK_DOWN: return ImGuiKey_DownArrow;
		case VK_PRIOR: return ImGuiKey_PageUp;
		case VK_NEXT: return ImGuiKey_PageDown;
		case VK_HOME: return ImGuiKey_Home;
		case VK_END: return ImGuiKey_End;
		case VK_INSERT: return ImGuiKey_Insert;
		case VK_DELETE: return ImGuiKey_Delete;
		case VK_BACK: return ImGuiKey_Backspace;
		case VK_SPACE: return ImGuiKey_Space;
		case VK_RETURN: return ImGuiKey_Enter;
		case VK_ESCAPE: return ImGuiKey_Escape;
		case VK_SHIFT: return ImGuiKey_LeftShift;
		case VK_CONTROL: return ImGuiKey_LeftCtrl;
		case VK_MENU: return ImGuiKey_LeftAlt;
		case VK_F1: return ImGuiKey_F1;
		case VK_F2: return ImGuiKey_F2;
		case VK_F3: return ImGuiKey_F3;
		case VK_F4: return ImGuiKey_F4;
		case VK_F5: return ImGuiKey_F5;
		case VK_F6: return ImGuiKey_F6;
		case VK_F7: return ImGuiKey_F7;
		case VK_F8: return ImGuiKey_F8;
		case VK_F9: return ImGuiKey_F9;
		case VK_F10: return ImGuiKey_F10;
		case VK_F11: return ImGuiKey_F11;
		case VK_F12: return ImGuiKey_F12;
		default: break;
	}
	if (p_vk >= '0' && p_vk <= '9') {
		return (ImGuiKey)(ImGuiKey_0 + (int)(p_vk - '0'));
	}
	if (p_vk >= 'A' && p_vk <= 'Z') {
		return (ImGuiKey)(ImGuiKey_A + (int)(p_vk - 'A'));
	}
	return ImGuiKey_None;
}

static void update_modifiers() {
	ImGuiIO &io = ImGui::GetIO();
	io.AddKeyEvent(ImGuiMod_Ctrl, (GetKeyState(VK_CONTROL) & 0x8000) != 0);
	io.AddKeyEvent(ImGuiMod_Shift, (GetKeyState(VK_SHIFT) & 0x8000) != 0);
	io.AddKeyEvent(ImGuiMod_Alt, (GetKeyState(VK_MENU) & 0x8000) != 0);
}

static LRESULT CALLBACK wnd_proc(HWND p_hwnd, UINT p_msg, WPARAM p_wparam, LPARAM p_lparam) {
	if (ImGui::GetCurrentContext() == nullptr) {
		return DefWindowProcW(p_hwnd, p_msg, p_wparam, p_lparam);
	}
	ImGuiIO &io = ImGui::GetIO();
	switch (p_msg) {
		case WM_CLOSE:
			g_quit = true;
			return 0;
		case WM_SIZE:
			g_width = LOWORD(p_lparam);
			g_height = HIWORD(p_lparam);
			return 0;
		case WM_ERASEBKGND:
			return 1;
		case WM_DPICHANGED: {
			g_dpi_scale = HIWORD(p_wparam) / 96.0f;
			const RECT *r = (const RECT *)p_lparam;
			SetWindowPos(p_hwnd, nullptr, r->left, r->top, r->right - r->left, r->bottom - r->top, SWP_NOZORDER | SWP_NOACTIVATE);
			return 0;
		}
		case WM_GETMINMAXINFO: {
			MINMAXINFO *mmi = (MINMAXINFO *)p_lparam;
			mmi->ptMinTrackSize.x = 800;
			mmi->ptMinTrackSize.y = 500;
			return 0;
		}
		case WM_MOUSEMOVE: {
			TRACKMOUSEEVENT tme = { sizeof(tme), TME_LEAVE, p_hwnd, 0 };
			TrackMouseEvent(&tme);
			io.AddMousePosEvent((float)GET_X_LPARAM(p_lparam), (float)GET_Y_LPARAM(p_lparam));
			return 0;
		}
		case WM_MOUSELEAVE:
			io.AddMousePosEvent(-FLT_MAX, -FLT_MAX);
			return 0;
		case WM_LBUTTONDOWN:
		case WM_RBUTTONDOWN:
		case WM_MBUTTONDOWN:
		case WM_LBUTTONDBLCLK:
		case WM_RBUTTONDBLCLK:
		case WM_MBUTTONDBLCLK: {
			int button = 0;
			if (p_msg == WM_RBUTTONDOWN || p_msg == WM_RBUTTONDBLCLK) {
				button = 1;
			} else if (p_msg == WM_MBUTTONDOWN || p_msg == WM_MBUTTONDBLCLK) {
				button = 2;
			}
			SetCapture(p_hwnd);
			io.AddMouseButtonEvent(button, true);
			return 0;
		}
		case WM_LBUTTONUP:
		case WM_RBUTTONUP:
		case WM_MBUTTONUP: {
			int button = 0;
			if (p_msg == WM_RBUTTONUP) {
				button = 1;
			} else if (p_msg == WM_MBUTTONUP) {
				button = 2;
			}
			ReleaseCapture();
			io.AddMouseButtonEvent(button, false);
			return 0;
		}
		case WM_MOUSEWHEEL:
			io.AddMouseWheelEvent(0.0f, (float)GET_WHEEL_DELTA_WPARAM(p_wparam) / (float)WHEEL_DELTA);
			return 0;
		case WM_MOUSEHWHEEL:
			io.AddMouseWheelEvent(-(float)GET_WHEEL_DELTA_WPARAM(p_wparam) / (float)WHEEL_DELTA, 0.0f);
			return 0;
		case WM_KEYDOWN:
		case WM_KEYUP:
		case WM_SYSKEYDOWN:
		case WM_SYSKEYUP: {
			const bool down = (p_msg == WM_KEYDOWN || p_msg == WM_SYSKEYDOWN);
			update_modifiers();
			const ImGuiKey key = vk_to_imgui_key(p_wparam);
			if (key != ImGuiKey_None) {
				io.AddKeyEvent(key, down);
			}
			if (p_msg == WM_SYSKEYDOWN || p_msg == WM_SYSKEYUP) {
				break; // Keep Alt+F4 etc.
			}
			return 0;
		}
		case WM_SETFOCUS:
		case WM_KILLFOCUS:
			io.AddFocusEvent(p_msg == WM_SETFOCUS);
			return 0;
		case WM_CHAR:
			if (p_wparam > 0 && p_wparam < 0x10000) {
				io.AddInputCharacterUTF16((unsigned short)p_wparam);
			}
			return 0;
		case WM_SETCURSOR:
			if (LOWORD(p_lparam) == HTCLIENT) {
				LPCWSTR cursor = IDC_ARROW;
				switch (g_cursor) {
					case ImGuiMouseCursor_TextInput: cursor = IDC_IBEAM; break;
					case ImGuiMouseCursor_ResizeAll: cursor = IDC_SIZEALL; break;
					case ImGuiMouseCursor_ResizeEW: cursor = IDC_SIZEWE; break;
					case ImGuiMouseCursor_ResizeNS: cursor = IDC_SIZENS; break;
					case ImGuiMouseCursor_ResizeNESW: cursor = IDC_SIZENESW; break;
					case ImGuiMouseCursor_ResizeNWSE: cursor = IDC_SIZENWSE; break;
					case ImGuiMouseCursor_Hand: cursor = IDC_HAND; break;
					case ImGuiMouseCursor_NotAllowed: cursor = IDC_NO; break;
					default: break;
				}
				SetCursor(LoadCursorW(nullptr, cursor));
				return TRUE;
			}
			break;
		default:
			break;
	}
	return DefWindowProcW(p_hwnd, p_msg, p_wparam, p_lparam);
}

bool create_window(const char *p_title, int p_width, int p_height) {
	QueryPerformanceFrequency(&g_perf_freq);
	QueryPerformanceCounter(&g_perf_start);

	// Per-monitor DPI awareness (loaded dynamically for older Windows versions).
	typedef BOOL(WINAPI * SetDpiCtxFn)(HANDLE);
	HMODULE user32 = GetModuleHandleW(L"user32.dll");
	SetDpiCtxFn set_dpi_ctx = user32 ? (SetDpiCtxFn)GetProcAddress(user32, "SetProcessDpiAwarenessContext") : nullptr;
	if (set_dpi_ctx) {
		set_dpi_ctx((HANDLE)-4); // DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2
	} else {
		SetProcessDPIAware();
	}

	HINSTANCE instance = GetModuleHandleW(nullptr);
	WNDCLASSEXW wc = {};
	wc.cbSize = sizeof(wc);
	wc.style = CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS;
	wc.lpfnWndProc = wnd_proc;
	wc.hInstance = instance;
	wc.hIcon = LoadIconW(instance, L"IDI_ICON1");
	wc.hIconSm = wc.hIcon;
	wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
	wc.lpszClassName = L"IPDEWindow";
	RegisterClassExW(&wc);

	HDC screen = GetDC(nullptr);
	g_dpi_scale = GetDeviceCaps(screen, LOGPIXELSX) / 96.0f;
	ReleaseDC(nullptr, screen);

	RECT rect = { 0, 0, (LONG)(p_width * g_dpi_scale), (LONG)(p_height * g_dpi_scale) };
	AdjustWindowRect(&rect, WS_OVERLAPPEDWINDOW, FALSE);
	g_hwnd = CreateWindowExW(0, wc.lpszClassName, to_wide(p_title).c_str(), WS_OVERLAPPEDWINDOW,
			CW_USEDEFAULT, CW_USEDEFAULT, rect.right - rect.left, rect.bottom - rect.top, nullptr, nullptr, instance, nullptr);
	if (!g_hwnd) {
		return false;
	}

	typedef UINT(WINAPI * GetDpiForWindowFn)(HWND);
	GetDpiForWindowFn get_dpi = user32 ? (GetDpiForWindowFn)GetProcAddress(user32, "GetDpiForWindow") : nullptr;
	if (get_dpi) {
		g_dpi_scale = get_dpi(g_hwnd) / 96.0f;
	}

	// Dark title bar (Windows 10 2004+ / 11).
	BOOL dark = TRUE;
	DwmSetWindowAttribute(g_hwnd, 20 /* DWMWA_USE_IMMERSIVE_DARK_MODE */, &dark, sizeof(dark));

	ShowWindow(g_hwnd, SW_SHOWDEFAULT);
	UpdateWindow(g_hwnd);
	RECT client;
	GetClientRect(g_hwnd, &client);
	g_width = client.right - client.left;
	g_height = client.bottom - client.top;
	return true;
}

void destroy_window() {
	if (g_hwnd) {
		DestroyWindow(g_hwnd);
		g_hwnd = nullptr;
	}
}

bool pump_events(double p_wait_seconds) {
	if (p_wait_seconds > 0.0) {
		MsgWaitForMultipleObjects(0, nullptr, FALSE, (DWORD)(p_wait_seconds * 1000.0), QS_ALLINPUT);
	}
	MSG msg;
	while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
		if (msg.message == WM_QUIT) {
			g_quit = true;
		}
		TranslateMessage(&msg);
		DispatchMessageW(&msg);
	}
	return !g_quit;
}

void present(const uint32_t *p_pixels, int p_width, int p_height) {
	if (!g_hwnd || p_width <= 0 || p_height <= 0) {
		return;
	}
	BITMAPINFO bmi = {};
	bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
	bmi.bmiHeader.biWidth = p_width;
	bmi.bmiHeader.biHeight = -p_height; // Top-down.
	bmi.bmiHeader.biPlanes = 1;
	bmi.bmiHeader.biBitCount = 32;
	bmi.bmiHeader.biCompression = BI_RGB;
	HDC dc = GetDC(g_hwnd);
	SetDIBitsToDevice(dc, 0, 0, p_width, p_height, 0, 0, 0, p_height, p_pixels, &bmi, DIB_RGB_COLORS);
	ReleaseDC(g_hwnd, dc);
}

void get_window_size(int &r_width, int &r_height) {
	r_width = g_width;
	r_height = g_height;
}

float get_dpi_scale() {
	return g_dpi_scale;
}

void set_window_title(const std::string &p_title) {
	if (g_hwnd) {
		SetWindowTextW(g_hwnd, to_wide(p_title).c_str());
	}
}

void set_mouse_cursor(int p_imgui_cursor) {
	g_cursor = p_imgui_cursor;
}

double get_time() {
	LARGE_INTEGER now;
	QueryPerformanceCounter(&now);
	return (double)(now.QuadPart - g_perf_start.QuadPart) / (double)g_perf_freq.QuadPart;
}

std::string get_executable_path() {
	wchar_t buffer[MAX_PATH * 4];
	const DWORD len = GetModuleFileNameW(nullptr, buffer, MAX_PATH * 4);
	return to_utf8(std::wstring(buffer, len));
}

static std::string known_folder(REFKNOWNFOLDERID p_id) {
	PWSTR path = nullptr;
	std::string out;
	if (SUCCEEDED(SHGetKnownFolderPath(p_id, 0, nullptr, &path))) {
		out = to_utf8(path);
	}
	CoTaskMemFree(path);
	return out;
}

std::string get_app_data_dir() {
	std::string dir = known_folder(FOLDERID_RoamingAppData) + "\\IPDE";
	CreateDirectoryW(to_wide(dir).c_str(), nullptr);
	return dir;
}

std::string get_documents_dir() {
	return known_folder(FOLDERID_Documents);
}

std::string get_temp_dir() {
	wchar_t buffer[MAX_PATH + 1];
	const DWORD len = GetTempPathW(MAX_PATH + 1, buffer);
	return to_utf8(std::wstring(buffer, len));
}

static std::wstring quote_arg(const std::wstring &p_arg) {
	std::wstring out = L"\"";
	for (wchar_t c : p_arg) {
		if (c == L'"') {
			out += L'\\';
		}
		out += c;
	}
	out += L"\"";
	return out;
}

bool create_process(const std::string &p_executable, const std::vector<std::string> &p_args) {
	std::wstring cmd = quote_arg(to_wide(p_executable));
	for (const std::string &a : p_args) {
		cmd += L" " + quote_arg(to_wide(a));
	}
	STARTUPINFOW si = {};
	si.cb = sizeof(si);
	PROCESS_INFORMATION pi = {};
	if (!CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi)) {
		return false;
	}
	CloseHandle(pi.hThread);
	CloseHandle(pi.hProcess);
	return true;
}

void open_url(const std::string &p_url) {
	ShellExecuteW(nullptr, L"open", to_wide(p_url).c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

void open_folder(const std::string &p_path) {
	ShellExecuteW(nullptr, L"explore", to_wide(p_path).c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

std::string pick_folder(const std::string &p_title) {
	std::string result;
	const HRESULT init = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
	IFileOpenDialog *dialog = nullptr;
	if (SUCCEEDED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_ALL, IID_PPV_ARGS(&dialog)))) {
		DWORD options = 0;
		dialog->GetOptions(&options);
		dialog->SetOptions(options | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM);
		dialog->SetTitle(to_wide(p_title).c_str());
		if (SUCCEEDED(dialog->Show(g_hwnd))) {
			IShellItem *item = nullptr;
			if (SUCCEEDED(dialog->GetResult(&item))) {
				PWSTR path = nullptr;
				if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path))) {
					result = to_utf8(path);
					CoTaskMemFree(path);
				}
				item->Release();
			}
		}
		dialog->Release();
	}
	if (SUCCEEDED(init)) {
		CoUninitialize();
	}
	return result;
}

std::string pick_file(const std::string &p_title, const std::string &p_filter_name, const std::string &p_pattern) {
	std::string result;
	const HRESULT init = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
	IFileOpenDialog *dialog = nullptr;
	if (SUCCEEDED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_ALL, IID_PPV_ARGS(&dialog)))) {
		DWORD options = 0;
		dialog->GetOptions(&options);
		dialog->SetOptions(options | FOS_FORCEFILESYSTEM | FOS_FILEMUSTEXIST);
		dialog->SetTitle(to_wide(p_title).c_str());
		const std::wstring name = to_wide(p_filter_name);
		const std::wstring pattern = to_wide(p_pattern);
		COMDLG_FILTERSPEC spec[2] = { { name.c_str(), pattern.c_str() }, { L"Todos los archivos", L"*.*" } };
		dialog->SetFileTypes(2, spec);
		if (SUCCEEDED(dialog->Show(g_hwnd))) {
			IShellItem *item = nullptr;
			if (SUCCEEDED(dialog->GetResult(&item))) {
				PWSTR path = nullptr;
				if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path))) {
					result = to_utf8(path);
					CoTaskMemFree(path);
				}
				item->Release();
			}
		}
		dialog->Release();
	}
	if (SUCCEEDED(init)) {
		CoUninitialize();
	}
	return result;
}

} // namespace ipde::platform
