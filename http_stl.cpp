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
				GetModuleHandleA("winhttp.dll"), error_code, 0, (LPSTR)&buffer, 0, nullptr)) {
				return msg + " (Failed to format error)";
			}
			return msg + ": " + buffer;
		}


		handle_manager::handle_manager() : handle_(nullptr) {}
		handle_manager::handle_manager(HINTERNET h) : handle_(h) {}
		handle_manager::~handle_manager() { if(handle_ != nullptr) WinHttpCloseHandle(handle_); }



		session::session()
		{
			handle_ = WinHttpOpen(L"", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
			if(handle_ == nullptr) {
				THROW_LAST_ERROR("WinHttpOpen() failed");
				return;
			}
		}

		session::~session()
		{
		}






		connection::connection(const session &sess, const std::string &host)
			: flags_(0),
			timeout_(30)
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

		connection::~connection()
		{
		}

		void connection::set_option(option opt, bool on)
		{
			if(on) {
				flags_ |= (1u << opt);
			} else {
				flags_ &= ~(1u << opt);
			}
		}

		response connection::send(const request &req)
		{
			std::wstring path = req.url_;

			URL_COMPONENTS url_comps;
			memset(&url_comps, 0, sizeof(url_comps));
			url_comps.dwStructSize = sizeof(url_comps);
			url_comps.dwSchemeLength = -1;
			url_comps.dwHostNameLength = -1;
			url_comps.dwUrlPathLength = -1;
			if(WinHttpCrackUrl(req.url_.c_str(), 0, 0, &url_comps)) {
				// If we managed to parse it, then it's an absolute url
				// Validate the scheme, domain, port
				if(url_comps.dwSchemeLength > 0 && url_comps.nScheme != components_.nScheme) {
					THROW_LAST_ERROR("Request url used a different scheme than the connection was initialized with");
					return response(nullptr);
				}

				if(url_comps.dwHostNameLength > 0 && _wcsnicmp(url_comps.lpszHostName, components_.lpszHostName, url_comps.dwHostNameLength) != 0) {
					THROW_LAST_ERROR("Request url used a different host name than the connection was initialized with");
					return response(nullptr);
				}

				if(url_comps.nPort != components_.nPort) {
					THROW_LAST_ERROR("Request url used a different port than the connection was initialized with");
					return response(nullptr);
				}

				// Use only the path part for making the request
				path = url_comps.lpszUrlPath;
			}

			DWORD open_request_flags = 0;
			if(components_.nScheme == INTERNET_SCHEME_HTTPS) {
				open_request_flags |= WINHTTP_FLAG_SECURE;
			}

			handle_manager request(WinHttpOpenRequest(handle_, req.method_.c_str(), path.c_str(), nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, open_request_flags));
			if(request == nullptr) {
				THROW_LAST_ERROR("WinHttpOpenRequest() failed");
				return response(nullptr);
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

			if(!WinHttpSetOption(request, WINHTTP_OPTION_SECURITY_FLAGS, (LPVOID)&security_flags, sizeof(DWORD))) {
				THROW_LAST_ERROR("WinHttpSetOption(WINHTTP_OPTION_SECURITY_FLAGS) on request handle failed");
				return response(nullptr);
			}

			DWORD timeout = timeout_;
			if(!WinHttpSetOption(request, WINHTTP_OPTION_SEND_TIMEOUT, (LPVOID)&timeout, sizeof(DWORD))) {
				THROW_LAST_ERROR("WinHttpSetOption(WINHTTP_OPTION_SEND_TIMEOUT) on request handle failed");
				return response(nullptr);
			}

			auto headersEnd = std::end(req.additional_headers_);
			for(auto iter = std::begin(req.additional_headers_); iter != headersEnd; ++iter) {
				if(!WinHttpAddRequestHeaders(request, iter->c_str(), iter->length(), WINHTTP_ADDREQ_FLAG_ADD|WINHTTP_ADDREQ_FLAG_REPLACE)) {
					THROW_LAST_ERROR("WinHttpAddRequestHeaders() failed");
					return response(nullptr);
				}
			}

			DWORD total_request_length = req.body_.length();
			if(!WinHttpSendRequest(request, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, total_request_length, 0)) {
				THROW_LAST_ERROR("WinHttpSendRequest() failed");
				return response(nullptr);
			}

			if(total_request_length > 0) {
				DWORD bytes_written = 0;
				if(!WinHttpWriteData(request, req.body_.data(), req.body_.length(), &bytes_written)) {
					THROW_LAST_ERROR("WinHttpWriteData() failed");
					return response(nullptr);
				}
				if(bytes_written != req.body_.length()) {
					THROW_ERROR("WinHttpWriteData did not send entire request body");
					return response(nullptr);
				}
			}

			HINTERNET h = request.handle();
			request.set_handle(nullptr);
			return response(h);
		}







		request::request(const std::string &method, const std::string &url)
			: method_(std::begin(method), std::end(method)),
			url_(std::begin(url), std::end(url)),
			flags_(0)
		{
		}

		request::~request()
		{
		}

		void request::add_header(const std::string &line)
		{
			additional_headers_.push_back(std::wstring(std::begin(line), std::end(line)));
		}

		void request::set_option(option opt, bool on)
		{
			if(on) {
				flags_ |= (1u << opt);
			}
			else {
				flags_ &= ~(1u << opt);
			}
		}




		response::response(HINTERNET request)
			: handle_manager(request),
			status_(-1),
			buffer_(nullptr)
		{
			if(handle_ != nullptr) {
				if(!WinHttpReceiveResponse(request, nullptr)) {
					THROW_LAST_ERROR("WinHttpReceiveResponse() failed");
					return;
				}

				DWORD status_code = 0;
				DWORD status_code_size = sizeof(status_code);
				if(!WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &status_code, &status_code_size, WINHTTP_NO_HEADER_INDEX)) {
					THROW_ERROR("WinHttpWriteData did not send entire request body");
					return;
				}
				status_ = (int)status_code;
			}
		}

		response::response(response &&other)
			: handle_manager(other.handle_),
			status_(other.status_),
			buffer_(other.buffer_)
		{
			other.handle_ = nullptr;
			other.buffer_ = nullptr;
		}

		response::~response()
		{
			if(buffer_ != nullptr) delete[] buffer_;
		}

		bool response::read(std::ostream &out)
		{
			if(handle_ == nullptr) {
				return false;
			}

			if(buffer_ == nullptr) {
				buffer_ = new char[read_buffer_size];
			}

			while(true) {

				DWORD data_available;
				if(!WinHttpQueryDataAvailable(handle_, &data_available)) {
					THROW_LAST_ERROR("WinHttpQueryDataAvailable() failed");
					return false;
				}

				if(data_available == 0) {
					break;
				}

				while(data_available > 0) {
					DWORD bytes_read;
					DWORD chunk_size = read_buffer_size < data_available ? read_buffer_size : data_available;

					if(!WinHttpReadData(handle_, buffer_, chunk_size, &bytes_read)) {
						THROW_LAST_ERROR("WinHttpReadData() failed");
						return false;
					}

					out.write(buffer_, bytes_read);
					data_available -= bytes_read;
				}
			}
			return true;
		}


	} // namespace stl

} // namespace http
