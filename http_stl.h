#pragma once
#include <Windows.h>

#ifndef WH_USE_WININET
#define WH_INTERNET(X) WinHttp##X
#define WH_INTERNETW(X) WinHttp##X
#define WH_HTTP(X) WinHttp##X
#define WH_HTTPW(X) WinHttp##X
#define WH_INTERNET_CONST(X) WINHTTP_##X
#define WH_HTTP_CONST(X) WINHTTP_##X
#define WH_WININET_ARGS(...)
#define WH_WINHTTP_ARGS(...) ,__VA_ARGS__
#pragma comment(lib, "winhttp.lib")
#include <winhttp.h>
#else
#define WH_INTERNET(X) Internet##X
#define WH_INTERNETW(X) Internet##X##W
#define WH_HTTP(X) Http##X
#define WH_HTTPW(X) Http##X##W
#define WH_INTERNET_CONST(X) INTERNET_##X
#define WH_HTTP_CONST(X) HTTP_##X
#define WH_WININET_ARGS(...) ,__VA_ARGS__
#define WH_WINHTTP_ARGS(...)
#pragma comment(lib, "wininet.lib")
#include <WinInet.h>
#endif

#include <string>
#include <iostream>
#include <vector>

#if _HAS_EXCEPTIONS

#include <stdexcept>
#define THROW_LAST_ERROR(x) { last_error_t e(x); error_ = e.what(); ok_ = false; throw e; }
#define THROW_ERROR(x) { std::runtime_error e(x); error_ = e.what(); ok_ = false; throw e; }

#else

#define THROW_LAST_ERROR(x) { error_ = format_last_error(x); ok_ = false; }
#define THROW_ERROR(x) { error_ = x; ok_ = false; }

#endif

namespace http
{
	namespace stl
	{

		class request_t;
		class response_t;


		std::string format_last_error(const std::string &msg);


#if _HAS_EXCEPTIONS
		class last_error_t : public std::runtime_error
		{
		public:
			last_error_t(const char *msg) : std::runtime_error(format_last_error(msg)) {}
		};
#endif


		enum option_t
		{
			option_allow_unknown_cert_authority = 0,
			option_allow_invalid_cert_name,
			option_allow_invalid_cert_date
		};


		class handle_manage_t
		{
		public:
			handle_manage_t();
			handle_manage_t(HINTERNET h);
			handle_manage_t(const handle_manage_t &other) = delete;
			virtual ~handle_manage_t();
			inline HINTERNET handle() const { return handle_; };
			inline void set_handle(HINTERNET h) { handle_ = h; };
			inline operator HINTERNET() const { return handle_; };

		protected:
			HINTERNET handle_;
		};


		class error_handler_t
		{
		public:
			error_handler_t() : ok_(true) {}
			virtual ~error_handler_t() {}
			inline bool ok() const { return ok_; }
			inline const std::string &error() const { return error_; }

		protected:
			bool ok_;
			std::string error_;
		};


		class session_t : public handle_manage_t, public error_handler_t
		{
		public:
			session_t(const std::string &user_agent);
			~session_t();
		};


		class connection_t : public handle_manage_t, public error_handler_t
		{
		public:
			connection_t(const session_t &sess, const std::string &host);
			virtual ~connection_t();
			response_t send(const request_t &req);
			unsigned int flags() const { return flags_; }
			inline unsigned int timeout() const { return timeout_; }
			void set_option(option_t opt, bool on);
			inline void set_timeout(unsigned int seconds) { timeout_ = seconds; }

		private:
			std::wstring host_;
			URL_COMPONENTSW components_;
			unsigned int flags_;
			unsigned int timeout_;
		};


		class request_t
		{
			friend class connection_t;

		public:
			request_t(const std::string &method, const std::string &url);
			virtual ~request_t();
			inline void set_body(const std::string &body) { body_ = body; }
			inline void set_body(const char *data, size_t length) { body_.clear(); body_.append(data, length); }
			void add_header(const std::string &line);
			void set_option(option_t opt, bool on);

		private:
			std::wstring method_;
			std::wstring url_;
			std::string body_;
			std::vector<std::wstring> additional_headers_;
			unsigned int flags_;
		};


		class response_t : public handle_manage_t, public error_handler_t
		{
			friend class connection_t;

		private:
			static const int read_buffer_size = 1024 * 1024;

		private:
			response_t(HINTERNET request_t);

		public:
			response_t(const response_t &other) = delete;
			response_t(response_t &&other);
			virtual ~response_t();
			inline const response_t &operator=(const response_t &other) = delete;
			inline const response_t &operator=(const response_t &&other) = delete;
			inline int status() const { return status_; }
			inline bool succeeded() const { return status_ >= 200 && status_ < 300; }
			inline bool failed() const { return !succeeded(); }
			bool read(std::ostream &out);

		private:
			char *buffer_;
			int status_;
		};

	} // namespace stl

} // namespace http
