#pragma once
#pragma comment(lib, "winhttp.lib")

#include <Windows.h>
#include <winhttp.h>

namespace http
{
	namespace nostl
	{

		class request;
		class response;

		void safe_free(void *s);
		void safe_delete(void *s);

		char *format_last_error(const char *msg);


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
			handle_manager(const handle_manager &other) = delete;
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
			error_handler() : ok_(true), error_(nullptr) {}
			virtual ~error_handler() { safe_free(error_); }
			inline bool ok() const { return ok_; }
			inline const char *error() const { return error_; }
			inline void set_error(const char *msg) { safe_free(error_); error_ = _strdup(msg); }

		protected:
			bool ok_;
			char *error_;
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
			connection(const session &sess, const char *host);
			virtual ~connection();
			response send(const request &req);
			unsigned int flags() const { return flags_; }
			inline unsigned int timeout() const { return timeout_; }
			void set_option(option opt, bool on);
			inline void set_timeout(unsigned int seconds) { timeout_ = seconds; }

		private:
			wchar_t *host_;
			URL_COMPONENTS components_;
			unsigned int flags_;
			unsigned int timeout_;
		};


		class request
		{
			friend class connection;

		public:
			struct header_line
			{
				header_line(wchar_t *line);
				~header_line();
				header_line *next_;
				wchar_t *line_;
			};

			request(const char *method, const char *url);
			virtual ~request();
			void set_body(const char *data, size_t length);
			void add_header(const char *line);
			void set_option(option opt, bool on);
			
		private:
			wchar_t *method_;
			wchar_t *url_;
			char *body_;
			size_t body_length_;
			header_line *headers_head_;
			header_line *headers_tail_;
			unsigned int flags_;
		};


		class response : public handle_manager, public error_handler
		{
			friend class connection;

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
			bool read(char *buffer, size_t count, size_t *bytes_read);

		private:
			int status_;
		};

	} // namespace nostl

} // namespace http
