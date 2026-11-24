#pragma once

// HTTP(S) GET requests, run in a background thread. Windows uses WinHTTP.

#include <atomic>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace ipde::http {

struct Request {
	std::string url;
	std::vector<std::string> headers; // "Name: value"
	std::string output_file; // If set, the body is written to this file instead of memory.

	// Filled by the request.
	std::atomic<bool> done{ false };
	std::atomic<int64_t> downloaded{ 0 };
	std::atomic<int64_t> total{ 0 };
	int status = 0; // HTTP status code, 0 on connection error.
	std::string body;
	std::string error;
};

// Starts the request in a background thread. Poll r_request->done.
void start(const std::shared_ptr<Request> &p_request);

} // namespace ipde::http
