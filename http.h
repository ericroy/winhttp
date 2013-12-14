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
	class request;
	class response;
	

	std::string format_last_error(const std::string &msg);
	

#if _HAS_EXCEPTIONS
	class last_error : public std::runtime_error
	{
	public:
		last_error(const char *msg) : std::runtime_error(format_last_error(msg)) {}
	};
#endif


	enum option
	{
		option_allow_unknown_cert_authority = 0,
		option_allow_invalid_cert_name,
		option_allow_invalid_cert_date
	};


	class handle_manager
	{
	public:
		handle_manager();
		handle_manager(HINTERNET h);
		virtual ~handle_manager();
		inline HINTERNET handle() const { return handle_; };
		inline void set_handle(HINTERNET h) { handle_ = h; };
		inline operator HINTERNET() const { return handle_; };

	protected:
		HINTERNET handle_;
	};


	class error_handler
	{
	public:
		error_handler() : ok_(true) {}
		virtual ~error_handler() {}
		inline bool ok() const { return ok_; }
		inline const std::string &error() const { return error_; }

	protected:
		bool ok_;
		std::string error_;
	};


	class session : public handle_manager, public error_handler
	{
	public:
		session();
		~session();
	};


	class connection : public handle_manager, public error_handler
	{
	public:
		connection(const session &sess, const std::string &host);
		virtual ~connection();
		response send(const request &req);
		unsigned int flags() const { return flags_; }
		inline unsigned int timeout() const { return timeout_; }
		void set_option(option opt, bool on);
		inline void set_timeout(unsigned int seconds) { timeout_ = seconds; }

	private:
		std::wstring host_;
		URL_COMPONENTS components_;
		unsigned int flags_;
		unsigned int timeout_;
	};


	class request
	{
	public:
		request(const std::string &method, const std::string &url);
		virtual ~request();
		inline const std::wstring &method() const { return method_; }
		inline const std::wstring &url() const { return url_; }
		inline const std::string &body() const { return body_; }
		inline const std::vector<std::wstring> &additional_headers() const { return additional_headers_; }
		unsigned int flags() const { return flags_; }
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


	class response : public handle_manager, public error_handler
	{
		friend class connection;

	private:
		static const int read_buffer_size = 1024 * 1024;

	private:
		response(HINTERNET request);

	public:
		response(const response &other) = delete;
		response(response &&other);
		virtual ~response();
		inline const response &operator=(const response &other) = delete;
		inline const response &operator=(const response &&other) = delete;
		inline int status() const { return status_; }
		inline bool succeeded() const { return status_ >= 200 && status_ < 300; }
		inline bool failed() const { return !succeeded(); }
		bool read(std::ostream &out);

	private:
		char *buffer_;
		int status_;
	};

}