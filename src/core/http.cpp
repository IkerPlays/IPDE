#include "core/http.h"

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <thread>

#ifdef _WIN32
#include <windows.h>
#include <winhttp.h>
#endif

namespace ipde::http {

#ifdef _WIN32

static std::wstring widen(const std::string &p_s) {
	if (p_s.empty()) {
		return std::wstring();
	}
	const int len = MultiByteToWideChar(CP_UTF8, 0, p_s.c_str(), (int)p_s.size(), nullptr, 0);
	std::wstring out(len, L'\0');
	MultiByteToWideChar(CP_UTF8, 0, p_s.c_str(), (int)p_s.size(), out.data(), len);
	return out;
}

static void run(std::shared_ptr<Request> p_req) {
	Request &req = *p_req;
	std::wstring url = widen(req.url);

	URL_COMPONENTS parts = {};
	parts.dwStructSize = sizeof(parts);
	wchar_t host[256] = {};
	wchar_t path[4096] = {};
	parts.lpszHostName = host;
	parts.dwHostNameLength = 256;
	parts.lpszUrlPath = path;
	parts.dwUrlPathLength = 4096;
	wchar_t extra[4096] = {};
	parts.lpszExtraInfo = extra;
	parts.dwExtraInfoLength = 4096;
	if (!WinHttpCrackUrl(url.c_str(), 0, 0, &parts)) {
		req.error = "Invalid URL";
		req.done = true;
		return;
	}

	HINTERNET session = WinHttpOpen(L"IPDE", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
	if (!session) {
		session = WinHttpOpen(L"IPDE", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
	}
	HINTERNET connect = session ? WinHttpConnect(session, host, parts.nPort, 0) : nullptr;
	const std::wstring object = std::wstring(path) + extra;
	HINTERNET request = connect ? WinHttpOpenRequest(connect, L"GET", object.c_str(), nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, parts.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0) : nullptr;

	bool ok = request != nullptr;
	if (ok) {
		std::wstring headers;
		for (const std::string &h : req.headers) {
			headers += widen(h) + L"\r\n";
		}
		ok = WinHttpSendRequest(request, headers.empty() ? WINHTTP_NO_ADDITIONAL_HEADERS : headers.c_str(), (DWORD)-1L, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) && WinHttpReceiveResponse(request, nullptr);
	}
	if (ok) {
		DWORD status = 0;
		DWORD size = sizeof(status);
		WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &status, &size, WINHTTP_NO_HEADER_INDEX);
		req.status = (int)status;
		wchar_t length_buf[64] = {};
		DWORD length_size = sizeof(length_buf);
		if (WinHttpQueryHeaders(request, WINHTTP_QUERY_CONTENT_LENGTH, WINHTTP_HEADER_NAME_BY_INDEX, length_buf, &length_size, WINHTTP_NO_HEADER_INDEX)) {
			req.total = _wtoi64(length_buf);
		}

		std::ofstream file;
		if (!req.output_file.empty() && req.status == 200) {
			file.open(std::filesystem::u8path(req.output_file), std::ios::binary | std::ios::trunc);
		}
		std::vector<char> buffer(64 * 1024);
		while (true) {
			DWORD available = 0;
			if (!WinHttpQueryDataAvailable(request, &available)) {
				ok = false;
				break;
			}
			if (available == 0) {
				break;
			}
			if (available > buffer.size()) {
				buffer.resize(available);
			}
			DWORD read = 0;
			if (!WinHttpReadData(request, buffer.data(), available, &read)) {
				ok = false;
				break;
			}
			if (file.is_open()) {
				file.write(buffer.data(), read);
			} else {
				req.body.append(buffer.data(), read);
			}
			req.downloaded += read;
		}
		if (file.is_open()) {
			file.close();
			if (!file) {
				req.error = "Couldn't write the downloaded file";
			}
		}
	}
	if (!ok && req.error.empty()) {
		req.error = "Connection error (" + std::to_string(GetLastError()) + ")";
	}
	if (request) {
		WinHttpCloseHandle(request);
	}
	if (connect) {
		WinHttpCloseHandle(connect);
	}
	if (session) {
		WinHttpCloseHandle(session);
	}
	req.done = true;
}

#else

// Development backend: uses the curl command line tool.
static void run(std::shared_ptr<Request> p_req) {
	Request &req = *p_req;
	std::string cmd = "curl -sL --max-time 600";
	for (const std::string &h : req.headers) {
		cmd += " -H '" + h + "'";
	}
	const std::string body_file = req.output_file.empty() ? "/tmp/ipde_http_" + std::to_string((uintptr_t)&req) : req.output_file;
	cmd += " -o '" + body_file + "' -w '%{http_code}' '" + req.url + "'";
	FILE *pipe = popen(cmd.c_str(), "r");
	char buf[64] = {};
	if (pipe) {
		fgets(buf, sizeof(buf), pipe);
		pclose(pipe);
	}
	req.status = atoi(buf);
	if (req.output_file.empty()) {
		std::ifstream f(body_file, std::ios::binary);
		req.body.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
		std::remove(body_file.c_str());
	}
	if (req.status == 0) {
		req.error = "Connection error";
	}
	req.downloaded = (int64_t)req.body.size();
	req.done = true;
}

#endif

void start(const std::shared_ptr<Request> &p_request) {
	std::thread(run, p_request).detach();
}

} // namespace ipde::http
