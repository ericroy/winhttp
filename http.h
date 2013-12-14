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
	std::string format_last_error(const std::string &msg);


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
		session();
		~session();
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
		handle_manager(HINTERNET h);
		~handle_manager();
		inline operator HINTERNET() const { return handle_; }

	public:
		HINTERNET handle_;
	};


	class request
	{
	public:
		enum option
		{
			AllowUnknownCertificateAuthorities = 0,
		};

		request(const std::string &method, const std::string &url);
		~request();
		inline const std::wstring &method() const { return method_; }
		inline const std::wstring &url() const { return url_; }
		inline const std::string &body() const { return body_; }
		inline const std::vector<std::wstring> &additional_headers() const { return additional_headers_; }
		inline void set_body(const std::string &body) { body_ = body; }
		inline void set_body(char *data, size_t length) { body_.clear(); body_.append(data, length); }
		void add_header(const std::string &name, const std::string &value);
		void set_option(option opt, bool on);

	private:
		std::wstring method_;
		std::wstring url_;
		std::string body_;
		std::vector<std::wstring> additional_headers_;
		unsigned int flags_;
	};


	class response
	{
		friend class connection;

	private:
		static const int read_buffer_size = 1024 * 1024;

	private:
		response(HANDLE request);

	public:
		response(const response &other) = delete;
		response(response &&other);
		~response();
		inline const response &operator=(const response &other) = delete;
		inline const response &operator=(const response &&other) = delete;
		inline int status() const { return status_; }
		inline bool succeeded() const { return status_ >= 200 && status_ < 300; }
		inline bool failed() const { return !succeeded(); }
		bool read(std::ostream &out);

	private:
		HINTERNET handle_;
		char *buffer_;
		int status_;
		bool ok_;
		std::string error_;
	};


	class connection
	{
	public:
		connection(const session &sess, const std::string &host);
		~connection();
		response send(const request &req);
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