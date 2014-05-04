#include "http_nostl.h"
#include <cstdlib>

namespace http
{
	namespace nostl
	{

		inline size_t tri_min(size_t a, size_t b, size_t c)
		{
			size_t temp = a < b ? a : b;
			return (temp < c ? temp : c);
		}

		inline void safe_free(void *s)
		{
			if(s != nullptr) {
				free(s);
			}
		}

		inline void safe_delete(void *s)
		{
			if(s != nullptr) {
				delete s;
			}
		}

		inline void safe_array_delete(void *s)
		{
			if(s != nullptr) {
				delete[] s;
			}
		}

		wchar_t *alloc_wide_string(const char *s)
		{
			int length = lstrlenA(s);
			wchar_t *ws = new wchar_t[length + 1];
			MultiByteToWideChar(CP_UTF8, 0, s, length + 1, ws, length + 1);
			return ws;
		}



		char *format_last_error(const char *msg)
		{
			LPSTR buffer = nullptr;
			DWORD error_code = GetLastError();
			if(!FormatMessageA(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_FROM_HMODULE | FORMAT_MESSAGE_IGNORE_INSERTS,
#ifdef WH_USE_WININET
				GetModuleHandleA("wininet.dll"),
#else
				GetModuleHandleA("winhttp.dll"),
#endif		
				error_code, 0, (LPSTR)&buffer, 0, nullptr)) {

				const char *tail = " (Failed to format error)";
				char *ret = new char[lstrlenA(msg) + lstrlenA(tail) + 1];
				lstrcpyA(ret, msg);
				lstrcatA(ret, tail);
				return ret;
			}

			const char *sep = ": ";
			char *ret = new char[lstrlenA(msg) + lstrlenA(sep) + lstrlenA(buffer) + 1];
			lstrcpyA(ret, msg);
			lstrcatA(ret, sep);
			lstrcatA(ret, buffer);
			return ret;
		}


		handle_manager_t::handle_manager_t() : handle_(nullptr) {}
		handle_manager_t::handle_manager_t(HINTERNET h) : handle_(h) {}
		handle_manager_t::~handle_manager_t() { if(handle_ != nullptr) WH_INTERNET(CloseHandle)(handle_); }



		session_t::session_t(const char *user_agent)
		{
			wchar_t *wide_user_agent = alloc_wide_string(user_agent);
			handle_ = WH_INTERNETW(Open)(wide_user_agent, 0, nullptr, nullptr, 0);
			safe_array_delete(wide_user_agent);
			if(handle_ == nullptr) {
				set_error("WinHttpOpen() failed");
				return;
			}
		}

		session_t::~session_t()
		{
		}





		connection_t::connection_t(const session_t &sess, const char *host)
			: flags_(0),
			timeout_(30),
			host_(alloc_wide_string(host))
		{
			memset(&components_, 0, sizeof(components_));
			components_.dwStructSize = sizeof(components_);
			components_.dwSchemeLength = -1;
			components_.dwHostNameLength = -1;

			if(!WH_INTERNETW(CrackUrl)(host_, 0, 0, &components_)) {
				set_error("WinHttpCrackUrl() failed");
				return;
			}

			size_t buffer_bytes = (components_.dwHostNameLength + 1) * sizeof(wchar_t);
			wchar_t *host_only = new wchar_t[buffer_bytes];
			wcsncpy_s(host_only, buffer_bytes, components_.lpszHostName, components_.dwHostNameLength);
#ifdef WH_USE_WININET
			handle_ = InternetConnectW(sess.handle(), host_only, components_.nPort, nullptr, nullptr, INTERNET_SERVICE_HTTP, 0, 0);
#else
			handle_ = WinHttpConnect(sess.handle(), host_only, components_.nPort, 0);
#endif
			safe_array_delete(host_only);

			if(handle_ == nullptr) {
				set_error("WinHttpConnect() failed");
				return;
			}
		}

		connection_t::~connection_t()
		{
			safe_array_delete(host_);
		}

		void connection_t::set_option(option_t opt, bool on)
		{
			if(on) {
				flags_ |= (1u << opt);
			}
			else {
				flags_ &= ~(1u << opt);
			}
		}

		response_t connection_t::send(const request_t &req)
		{
			const wchar_t *path = req.url_;

			URL_COMPONENTSW url_comps;
			memset(&url_comps, 0, sizeof(url_comps));
			url_comps.dwStructSize = sizeof(url_comps);
			url_comps.dwSchemeLength = -1;
			url_comps.dwHostNameLength = -1;
			url_comps.dwUrlPathLength = -1;
			if(WH_INTERNETW(CrackUrl)(req.url_, 0, 0, &url_comps)) {
				// If we managed to parse it, then it's an absolute url
				// Validate the scheme, domain, port
				if(url_comps.dwSchemeLength > 0 && url_comps.nScheme != components_.nScheme) {
					set_error("request_t url used a different scheme than the connection_t was initialized with");
					return response_t(nullptr);
				}

				if(url_comps.dwHostNameLength > 0 && _wcsnicmp(url_comps.lpszHostName, components_.lpszHostName, url_comps.dwHostNameLength) != 0) {
					set_error("request_t url used a different host name than the connection_t was initialized with");
					return response_t(nullptr);
				}

				if(url_comps.nPort != components_.nPort) {
					set_error("request_t url used a different port than the connection_t was initialized with");
					return response_t(nullptr);
				}

				// Use only the path part for making the request_t
				path = url_comps.lpszUrlPath;
			}

			DWORD open_request_flags = 0;
			if(components_.nScheme == INTERNET_SCHEME_HTTPS) {
				open_request_flags |= WH_INTERNET_CONST(FLAG_SECURE);
			}

			const wchar_t *accept_types[] = {L"*/*", nullptr};
			handle_manager_t request_t(WH_HTTPW(OpenRequest)(handle_, req.method_, path, nullptr, nullptr, accept_types, open_request_flags WH_WININET_ARGS(0) ));
			if(request_t == nullptr) {
				set_error("WinHttpOpenRequest() failed");
				return response_t(nullptr);
			}

			unsigned int option_flags = flags_ | req.flags_;
			DWORD security_flags = 0;
			if((option_flags & option_allow_unknown_cert_authority) != 0) {
				security_flags |= SECURITY_FLAG_IGNORE_UNKNOWN_CA;
			}
			if((option_flags & option_allow_invalid_cert_name) != 0) {
				security_flags |= SECURITY_FLAG_IGNORE_CERT_CN_INVALID;
			}
			if((option_flags & option_allow_invalid_cert_date) != 0) {
				security_flags |= SECURITY_FLAG_IGNORE_CERT_DATE_INVALID;
			}

			if(!WH_INTERNET(SetOption)(request_t, WH_INTERNET_CONST(OPTION_SECURITY_FLAGS), (LPVOID)&security_flags, sizeof(DWORD))) {
				set_error("WinHttpSetOption(WINHTTP_OPTION_SECURITY_FLAGS) on request_t handle failed");
				return response_t(nullptr);
			}

			DWORD timeout = timeout_;
			if(!WH_INTERNET(SetOption)(request_t, WH_INTERNET_CONST(OPTION_SEND_TIMEOUT), (LPVOID)&timeout, sizeof(DWORD))) {
				set_error("WinHttpSetOption(WINHTTP_OPTION_SEND_TIMEOUT) on request_t handle failed");
				return response_t(nullptr);
			}

			const request_t::header_line *entry = req.headers_head_;
			while(entry != nullptr) {
				if(!WH_HTTPW(AddRequestHeaders)(request_t, entry->line_, lstrlenW(entry->line_), WH_HTTP_CONST(ADDREQ_FLAG_ADD) | WH_HTTP_CONST(ADDREQ_FLAG_REPLACE))) {
					set_error("WinHttpAddRequestHeaders() failed");
					return response_t(nullptr);
				}
				entry = entry->next_;
			}

			DWORD total_request_length = (DWORD)req.body_length_;

#ifdef WH_USE_WININET
			if(!HttpSendRequestW(request_t, nullptr, 0, req.body_, total_request_length)) {
				set_error("HttpSendRequest() failed");
				return response_t(nullptr);
			}
#else
			if(!WinHttpSendRequest(request_t, nullptr, 0, nullptr, 0, total_request_length, 0)) {
				set_error("WinHttpSendRequest() failed");
				return response_t(nullptr);
			}

			if(total_request_length > 0) {
				DWORD bytes_written = 0;
				if(!WinHttpWriteData(request_t, req.body_, total_request_length, &bytes_written)) {
					set_error("WinHttpWriteData() failed");
					return response_t(nullptr);
				}
				if(bytes_written != req.body_length_) {
					set_error("WinHttpWriteData did not send entire request_t body");
					return response_t(nullptr);
				}
			}
#endif

			HINTERNET h = request_t.handle();
			request_t.set_handle(nullptr);
			return response_t(h);
		}







		request_t::header_line::header_line(wchar_t *line)
			: line_(line),
			next_(nullptr)
		{
		}

		request_t::header_line::~header_line()
		{
			safe_delete(line_);
			safe_delete(next_);
		}



		request_t::request_t(const char *method, const char *url)
			: method_(alloc_wide_string(method)),
			url_(alloc_wide_string(url)),
			body_(nullptr),
			body_length_(0),
			flags_(0),
			headers_head_(nullptr),
			headers_tail_(nullptr)
		{
		}

		request_t::~request_t()
		{
			safe_array_delete(method_);
			safe_array_delete(url_);
			safe_array_delete(body_);
			safe_delete(headers_head_);
		}

		void request_t::add_header(const char *line)
		{
			header_line *entry = new header_line(alloc_wide_string(line));
			if(headers_head_ == nullptr) {
				headers_head_ = headers_tail_ = entry;
			}
			else {
				headers_tail_->next_ = entry;
				headers_tail_ = entry;
			}
		}

		void request_t::set_body(const char *data, size_t length)
		{
			safe_array_delete(body_);
			body_length_ = length;
			body_ = new char[length];
			memcpy(body_, data, length);
		}

		void request_t::set_option(option_t opt, bool on)
		{
			if(on) {
				flags_ |= (1u << opt);
			} else {
				flags_ &= ~(1u << opt);
			}
		}




		response_t::response_t(HINTERNET request_t)
			: handle_manager_t(request_t),
			status_(-1)
		{
			if(handle_ != nullptr) {
#ifdef WH_USE_WININET
				char status_code[32];
				DWORD status_code_size = sizeof(status_code);
				DWORD header_index = 0;
				if(!HttpQueryInfo(request_t, HTTP_QUERY_STATUS_CODE, &status_code, &status_code_size, &header_index)) {
					set_error("HttpQueryInfo() failed");
					return;
				}
				status_code[status_code_size] = 0;
				status_ = atoi(status_code);
#else
				if(!WinHttpReceiveResponse(request_t, nullptr)) {
					set_error("WinHttpReceiveResponse() failed");
					return;
				}
				
				DWORD status_code = 0;
				DWORD status_code_size = sizeof(status_code);
				if(!WinHttpQueryHeaders(request_t, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &status_code, &status_code_size, WINHTTP_NO_HEADER_INDEX)) {
					set_error("WinHttpWriteData did not send entire request_t body");
					return;
				}
				status_ = (int)status_code;
#endif
			}
		}

		response_t::response_t(response_t &&other)
			: handle_manager_t(other.handle_),
			status_(other.status_)
		{
			other.handle_ = nullptr;
		}

		response_t::~response_t()
		{
		}

		bool response_t::read(char *buffer, size_t count, size_t *bytes_read)
		{
			if(handle_ == nullptr) {
				return false;
			}

			size_t remaining = count;
			char *p = buffer;

			while(remaining > 0) {

				DWORD data_available;
				if(!WH_INTERNET(QueryDataAvailable)(handle_, &data_available WH_WININET_ARGS(0, 0) )) {
					set_error("WinHttpQueryDataAvailable() failed");
					return false;
				}

				if(data_available == 0) {
					break;
				}

				while(data_available > 0 && remaining > 0) {
					DWORD copied;
					DWORD chunk_size = data_available < remaining ? data_available : remaining;

#ifdef WH_USE_WININET
					if(!InternetReadFile(handle_, p, chunk_size, &copied)) {
						set_error("InternetReadFile() failed");
						return false;
					}
#else
					if(!WinHttpReadData(handle_, p, chunk_size, &copied)) {
						set_error("WinHttpReadData() failed");
						return false;
					}
#endif

					p += copied;
					data_available -= copied;
					remaining -= copied;
				}
			}

			if(bytes_read != nullptr) {
				*bytes_read = count - remaining;
			}

			return true;
		}


	} // namespace nostl

} // namespace http
