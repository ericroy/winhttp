#pragma once
#pragma comment(lib, "winhttp.lib")

#include <Windows.h>
#include <winhttp.h>
#include <string>
#include <iostream>
#include <vector>

#if _HAS_EXCEPTIONS

#include <stdexcept>
#define THROW_LAST_ERROR(x) { last_error e(x); error_ = e.what(); ok_ = false; throw e; }
#define THROW_ERROR(x) { std::runtime_error e(x); error_ = e.what(); ok_ = false; throw e; }

#else

#define THROW_LAST_ERROR(x) { error_ = format_last_error(x); ok_ = false; }
#define THROW_ERROR(x) { error_ = x; ok_ = false; }

#endif

namespace http
{
	std::string format_last_error(const std::string &msg)
	{
		LPSTR buffer = nullptr;
		DWORD error_code = GetLastError();
		if(!FormatMessageA(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_FROM_HMODULE | FORMAT_MESSAGE_IGNORE_INSERTS,
			GetModuleHandleA("winhttp.dll"), error_code, 0, (LPSTR)&buffer, 0, nullptr)) {
			return msg + " (Failed to format error)";
		}
		return msg + ": " + buffer;
	}


#if _HAS_EXCEPTIONS
	class last_error : public std::runtime_error
	{
	public:
		last_error(const char *msg) : std::runtime_error(format_last_error(msg)) {}
	};
#endif


	class session
	{
	public:
		session() : handle_(nullptr), ok_(true)
		{
			handle_ = WinHttpOpen(L"", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
			if(handle_ == nullptr) {
				THROW_LAST_ERROR("WinHttpOpen() failed");
				return;
			}
		}

		~session()
		{
			if(handle_ != nullptr) WinHttpCloseHandle(handle_);
		}

		inline HINTERNET handle() const { return handle_; };
		inline bool ok() const { return ok_; }
		inline const std::string &error() const { return error_; }

	private:
		HINTERNET handle_;
		bool ok_;
		std::string error_;
	};


	class handle_manager
	{
	public:
		handle_manager(HINTERNET h) : handle_(h) {}
		~handle_manager() { if(handle_ != nullptr) WinHttpCloseHandle(handle_); }
		inline operator HINTERNET() const { return handle_; }

	public:
		HINTERNET handle_;
	};


	class connection
	{
	public:
		connection(const session &sess, const std::string &host) : handle_(nullptr), ok_(true)
		{
			host_ = std::wstring(std::begin(host), std::end(host));

			memset(&components_, 0, sizeof(components_));
			components_.dwStructSize = sizeof(components_);
			components_.dwSchemeLength = -1;
			components_.dwHostNameLength = -1;

			if(!WinHttpCrackUrl(host_.c_str(), 0, 0, &components_)) {
				THROW_LAST_ERROR("WinHttpCrackUrl() failed");
				return;
			}

			std::wstring host_only(components_.lpszHostName, components_.dwHostNameLength);
			handle_ = WinHttpConnect(sess.handle(), host_only.c_str(), components_.nPort, 0);
			if(handle_ == nullptr) {
				THROW_LAST_ERROR("WinHttpConnect() failed");
				return;
			}
		}

		~connection()
		{
			if(handle_ != nullptr) WinHttpCloseHandle(handle_);
		}

		int request(const std::string &method,
			const std::string &url,
			const std::vector<std::string> *additional_headers,
			const std::string *body,
			std::ostream *response)
		{
			std::wstring wmethod(std::begin(method), std::end(method));
			std::wstring wurl(std::begin(url), std::end(url));
			std::wstring path = wurl;

			URL_COMPONENTS url_comps;
			memset(&url_comps, 0, sizeof(url_comps));
			url_comps.dwStructSize = sizeof(url_comps);
			url_comps.dwSchemeLength = -1;
			url_comps.dwHostNameLength = -1;
			url_comps.dwUrlPathLength = -1;
			if(WinHttpCrackUrl(wurl.c_str(), 0, 0, &url_comps)) {
				// If we managed to parse it, then it's an absolute url
				// Validate the scheme, domain, port
				if(url_comps.dwSchemeLength > 0 && url_comps.nScheme != components_.nScheme) {
					THROW_LAST_ERROR("Request url used a different scheme than the connection was initialized with");
					return -1;
				}

				if(url_comps.dwHostNameLength > 0 && _wcsnicmp(url_comps.lpszHostName, components_.lpszHostName, url_comps.dwHostNameLength) != 0) {
					THROW_LAST_ERROR("Request url used a different host name than the connection was initialized with");
					return -1;
				}

				if(url_comps.nPort != components_.nPort) {
					THROW_LAST_ERROR("Request url used a different port than the connection was initialized with");
					return -1;
				}

				// Use only the path part for making the request
				path = url_comps.lpszUrlPath;
			}
			
			DWORD flags = 0;
			if(components_.nScheme == INTERNET_SCHEME_HTTPS) {
				flags |= WINHTTP_FLAG_SECURE;
			}

			handle_manager request(WinHttpOpenRequest(handle_, wmethod.c_str(), path.c_str(), nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, flags));
			if(request == nullptr) {
				THROW_LAST_ERROR("WinHttpOpenRequest() failed");
				return -1;
			}

			if(additional_headers != nullptr) {
				for(std::vector<std::string>::const_iterator iter = std::begin(*additional_headers); iter != std::end(*additional_headers); ++iter) {
					std::wstring header_line(std::begin(*iter), std::end(*iter));
					if(!WinHttpAddRequestHeaders(request, header_line.c_str(), header_line.length(), WINHTTP_ADDREQ_FLAG_REPLACE)) {
						THROW_LAST_ERROR("WinHttpAddRequestHeaders() failed");
						return -1;
					}
				}
			}

			DWORD total_request_length = body != nullptr ? body->length() : 0;
			if(!WinHttpSendRequest(request, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, total_request_length, 0)) {
				THROW_LAST_ERROR("WinHttpSendRequest() failed");
				return -1;
			}

			if(body != nullptr) {
				DWORD bytes_written = 0;
				if(!WinHttpWriteData(request, body->data(), body->length(), &bytes_written)) {
					THROW_LAST_ERROR("WinHttpWriteData() failed");
					return -1;
				}
				if(bytes_written != body->length()) {
					THROW_ERROR("WinHttpWriteData did not send entire request body");
					return -1;
				}
			}

			if(!WinHttpReceiveResponse(request, nullptr)) {
				THROW_LAST_ERROR("WinHttpReceiveResponse() failed");
				return -1;
			}

			
			DWORD status_code = 0;
			DWORD status_code_size = sizeof(status_code);
			if(!WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &status_code, &status_code_size, WINHTTP_NO_HEADER_INDEX)) {
				THROW_ERROR("WinHttpWriteData did not send entire request body");
				return -1;
			}

			if(response != nullptr) {
				const int buffer_size = 1024 * 1024;
				char *buffer = new char[buffer_size];
				while(true) {
					DWORD data_available;
					if(!WinHttpQueryDataAvailable(request, &data_available)) {
						THROW_LAST_ERROR("WinHttpQueryDataAvailable() failed");
						delete[] buffer;
						return -1;
					}

					if(data_available == 0) {
						break;
					}

					while(data_available > 0) {
						DWORD bytes_read;
						DWORD chunk_size = buffer_size < data_available ? buffer_size : data_available;

						if(!WinHttpReadData(request, buffer, chunk_size, &bytes_read)) {
							THROW_LAST_ERROR("WinHttpReadData() failed");
							delete[] buffer;
							return -1;
						}

						response->write(buffer, bytes_read);
						data_available -= bytes_read;
					}
				}
			}

			return status_code;
		}

		inline int get(const std::string &url, std::ostream *response = nullptr) { return request("GET", url, nullptr, nullptr, response); }
		inline int post(const std::string &url, const std::string *body = nullptr, std::ostream *response = nullptr) { return request("POST", url, nullptr, body, response); }

		inline HINTERNET handle() const { return handle_; };
		inline bool ok() const { return ok_; }
		inline const std::string &error() const { return error_; }

	private:
		std::wstring host_;
		URL_COMPONENTS components_;
		HINTERNET handle_;
		bool ok_;
		std::string error_;
	};


}