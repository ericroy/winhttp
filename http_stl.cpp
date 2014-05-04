#include "http_stl.h"

namespace http
{
	namespace stl
	{

		std::string format_last_error(const std::string &msg)
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
				return msg + " (Failed to format error)";
			}
			return msg + ": " + buffer;
		}


		handle_manage_t::handle_manage_t() : handle_(nullptr) {}
		handle_manage_t::handle_manage_t(HINTERNET h) : handle_(h) {}
		handle_manage_t::~handle_manage_t() { if(handle_ != nullptr) WH_INTERNET(CloseHandle)(handle_); }



		session_t::session_t(const std::string &user_agent)
		{
			std::wstring wide_user_agent = std::wstring(std::begin(user_agent), std::end(user_agent));
			handle_ = WH_INTERNETW(Open)(wide_user_agent.c_str(), 0, nullptr, nullptr, 0);
			if(handle_ == nullptr) {
				THROW_LAST_ERROR("WinHttpOpen() failed");
				return;
			}
		}

		session_t::~session_t()
		{
		}






		connection_t::connection_t(const session_t &sess, const std::string &host)
			: flags_(0),
			timeout_(30)
		{
			host_ = std::wstring(std::begin(host), std::end(host));

			memset(&components_, 0, sizeof(components_));
			components_.dwStructSize = sizeof(components_);
			components_.dwSchemeLength = -1;
			components_.dwHostNameLength = -1;

			if(!WH_INTERNETW(CrackUrl)(host_.c_str(), 0, 0, &components_)) {
				THROW_LAST_ERROR("WinHttpCrackUrl() failed");
				return;
			}

			std::wstring host_only(components_.lpszHostName, components_.dwHostNameLength);
#ifdef WH_USE_WININET
			handle_ = InternetConnectW(sess.handle(), host_only.c_str(), components_.nPort, nullptr, nullptr, INTERNET_SERVICE_HTTP, 0, 0);
#else
			handle_ = WinHttpConnect(sess.handle(), host_only.c_str(), components_.nPort, 0);
#endif
			if(handle_ == nullptr) {
				THROW_LAST_ERROR("WinHttpConnect() failed");
				return;
			}
		}

		connection_t::~connection_t()
		{
		}

		void connection_t::set_option(option_t opt, bool on)
		{
			if(on) {
				flags_ |= (1u << opt);
			} else {
				flags_ &= ~(1u << opt);
			}
		}

		response_t connection_t::send(const request_t &req)
		{
			std::wstring path = req.url_;

			URL_COMPONENTSW url_comps;
			memset(&url_comps, 0, sizeof(url_comps));
			url_comps.dwStructSize = sizeof(url_comps);
			url_comps.dwSchemeLength = -1;
			url_comps.dwHostNameLength = -1;
			url_comps.dwUrlPathLength = -1;
			if(WH_INTERNETW(CrackUrl)(req.url_.c_str(), 0, 0, &url_comps)) {
				// If we managed to parse it, then it's an absolute url
				// Validate the scheme, domain, port
				if(url_comps.dwSchemeLength > 0 && url_comps.nScheme != components_.nScheme) {
					THROW_LAST_ERROR("request_t url used a different scheme than the connection_t was initialized with");
					return response_t(nullptr);
				}

				if(url_comps.dwHostNameLength > 0 && _wcsnicmp(url_comps.lpszHostName, components_.lpszHostName, url_comps.dwHostNameLength) != 0) {
					THROW_LAST_ERROR("request_t url used a different host name than the connection_t was initialized with");
					return response_t(nullptr);
				}

				if(url_comps.nPort != components_.nPort) {
					THROW_LAST_ERROR("request_t url used a different port than the connection_t was initialized with");
					return response_t(nullptr);
				}

				// Use only the path part for making the request_t
				path = url_comps.lpszUrlPath;
			}

			DWORD open_request_flags = 0;
			if(components_.nScheme == INTERNET_SCHEME_HTTPS) {
				open_request_flags |= WH_INTERNET_CONST(FLAG_SECURE);
			}

			const wchar_t *accept_types[] = { L"*/*", nullptr };
			handle_manage_t request_t(WH_HTTPW(OpenRequest)(handle_, req.method_.c_str(), path.c_str(), nullptr, nullptr, accept_types, open_request_flags WH_WININET_ARGS(0) ));
			if(request_t == nullptr) {
				THROW_LAST_ERROR("WinHttpOpenRequest() failed");
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
				THROW_LAST_ERROR("WinHttpSetOption(WINHTTP_OPTION_SECURITY_FLAGS) on request_t handle failed");
				return response_t(nullptr);
			}

			DWORD timeout = timeout_;
			if(!WH_INTERNET(SetOption)(request_t, WH_INTERNET_CONST(OPTION_SEND_TIMEOUT), (LPVOID)&timeout, sizeof(DWORD))) {
				THROW_LAST_ERROR("WinHttpSetOption(WINHTTP_OPTION_SEND_TIMEOUT) on request_t handle failed");
				return response_t(nullptr);
			}

			auto headers_end = std::end(req.additional_headers_);
			for(auto iter = std::begin(req.additional_headers_); iter != headers_end; ++iter) {
				if(!WH_HTTPW(AddRequestHeaders)(request_t, iter->c_str(), iter->length(), WH_HTTP_CONST(ADDREQ_FLAG_ADD) | WH_HTTP_CONST(ADDREQ_FLAG_REPLACE))) {
					THROW_LAST_ERROR("WinHttpAddRequestHeaders() failed");
					return response_t(nullptr);
				}
			}

			DWORD total_request_length = (DWORD)req.body_.length();

#ifdef WH_USE_WININET
			if(!HttpSendRequestW(request_t, nullptr, 0, (LPVOID)req.body_.c_str(), total_request_length)) {
				THROW_LAST_ERROR("HttpSendRequest() failed");
				return response_t(nullptr);
			}
#else
			if(!WinHttpSendRequest(request_t, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, total_request_length, 0)) {
				THROW_LAST_ERROR("WinHttpSendRequest() failed");
				return response_t(nullptr);
			}

			if(total_request_length > 0) {
				DWORD bytes_written = 0;
				if(!WinHttpWriteData(request_t, req.body_.data(), total_request_length, &bytes_written)) {
					THROW_LAST_ERROR("WinHttpWriteData() failed");
					return response_t(nullptr);
				}
				if(bytes_written != req.body_.length()) {
					THROW_ERROR("WinHttpWriteData did not send entire request_t body");
					return response_t(nullptr);
				}
			}
#endif

			HINTERNET h = request_t.handle();
			request_t.set_handle(nullptr);
			return response_t(h);
		}







		request_t::request_t(const std::string &method, const std::string &url)
			: method_(std::begin(method), std::end(method)),
			url_(std::begin(url), std::end(url)),
			flags_(0)
		{
		}

		request_t::~request_t()
		{
		}

		void request_t::add_header(const std::string &line)
		{
			additional_headers_.push_back(std::wstring(std::begin(line), std::end(line)));
		}

		void request_t::set_option(option_t opt, bool on)
		{
			if(on) {
				flags_ |= (1u << opt);
			}
			else {
				flags_ &= ~(1u << opt);
			}
		}




		response_t::response_t(HINTERNET request_t)
			: handle_manage_t(request_t),
			status_(-1),
			buffer_(nullptr)
		{
			if(handle_ != nullptr) {
#ifdef WH_USE_WININET
				char status_code[32];
				DWORD status_code_size = sizeof(status_code);
				DWORD header_index = 0;
				if(!HttpQueryInfoA(request_t, HTTP_QUERY_STATUS_CODE, &status_code, &status_code_size, &header_index)) {
					THROW_LAST_ERROR("HttpQueryInfo() failed");
					return;
				}
				status_code[status_code_size] = 0;
				status_ = atoi(status_code);
#else
				if(!WinHttpReceiveResponse(request_t, nullptr)) {
					THROW_LAST_ERROR("WinHttpReceiveResponse() failed");
					return;
				}

				DWORD status_code = 0;
				DWORD status_code_size = sizeof(status_code);
				if(!WinHttpQueryHeaders(request_t, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &status_code, &status_code_size, WINHTTP_NO_HEADER_INDEX)) {
					THROW_ERROR("WinHttpWriteData did not send entire request_t body");
					return;
				}
				status_ = (int)status_code;
#endif
			}
		}

		response_t::response_t(response_t &&other)
			: handle_manage_t(other.handle_),
			status_(other.status_),
			buffer_(other.buffer_)
		{
			other.handle_ = nullptr;
			other.buffer_ = nullptr;
		}

		response_t::~response_t()
		{
			if(buffer_ != nullptr) delete[] buffer_;
		}

		bool response_t::read(std::ostream &out)
		{
			if(handle_ == nullptr) {
				return false;
			}

			if(buffer_ == nullptr) {
				buffer_ = new char[read_buffer_size];
			}

			while(true) {

				DWORD data_available;
				if(!WH_INTERNET(QueryDataAvailable)(handle_, &data_available WH_WININET_ARGS(0, 0) )) {
					THROW_LAST_ERROR("WinHttpQueryDataAvailable() failed");
					return false;
				}

				if(data_available == 0) {
					break;
				}

				while(data_available > 0) {
					DWORD bytes_read;
					DWORD chunk_size = read_buffer_size < data_available ? read_buffer_size : data_available;

#ifdef WH_USE_WININET
					if(!InternetReadFile(handle_, buffer_, chunk_size, &bytes_read)) {
						THROW_LAST_ERROR("InternetReadFile() failed");
						return false;
					}
#else
					if(!WinHttpReadData(handle_, buffer_, chunk_size, &bytes_read)) {
						THROW_LAST_ERROR("WinHttpReadData() failed");
						return false;
					}
#endif

					out.write(buffer_, bytes_read);
					data_available -= bytes_read;
				}
			}
			return true;
		}


	} // namespace stl

} // namespace http
