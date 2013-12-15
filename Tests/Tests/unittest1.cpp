#include "stdafx.h"
#include "CppUnitTest.h"

#undef _HAS_EXCEPTIONS
#define _HAS_EXCEPTIONS 1

#define WINHTTP_NOSTL 1

#include "../../http_stl.h"
#include "../../http_nostl.h"

#include <sstream>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;
using namespace std;

#if WINHTTP_NOSTL
using namespace http::nostl;
#else
using namespace http::stl;
#endif

namespace Tests
{
	void Check(response &resp)
	{
#if WINHTTP_NOSTL
		char buffer[128];
		size_t read;
		bool success = resp.read(buffer, 128, &read);
		Assert::IsTrue(success);
		Assert::IsTrue(read > 0);
#else
		stringstream ss;
		resp.read(ss);
		Assert::IsTrue(ss.str().length() > 0);
#endif
	}

	TEST_CLASS(StackInstantiation)
	{
	public:
		TEST_METHOD(Session)
		{
			session sess;
		}

		TEST_METHOD(Connection)
		{
			session sess;
			connection conn(sess, "http://www.microsoft.com");
		}
	};


	TEST_CLASS(Requests)
	{
	public:
		Requests() : sess_(), conn_(sess_, "http://www.microsoft.com/blah.html") {}
		TEST_METHOD_INITIALIZE(Setup) {}
		TEST_METHOD_CLEANUP(TearDown) {}

		TEST_METHOD(Get)
		{
			request req("GET", "http://www.microsoft.com");
			response resp = conn_.send(req);

			Assert::AreEqual(200, resp.status());
			Check(resp);
		}

		TEST_METHOD(GetRelative)
		{
			request req("GET", "/");
			response resp = conn_.send(req);

			Assert::AreEqual(200, resp.status());
			Check(resp);
		}

		TEST_METHOD(GetWithAdditionalHeaders)
		{
			request req("GET", "/");
			req.add_header("Accept-Language: en-US");
			
			response resp = conn_.send(req);

			Assert::AreEqual(200, resp.status());
			Check(resp);
		}

		session sess_;
		connection conn_;
	};

	TEST_CLASS(RequestsSSL)
	{
	public:
		RequestsSSL() : sess_(), conn_(sess_, "https://google.com/index.html") {}
		TEST_METHOD_INITIALIZE(Setup) {}
		TEST_METHOD_CLEANUP(TearDown) {}

		TEST_METHOD(GetSSL)
		{
			request req("GET", "/");
			response resp = conn_.send(req);

			Assert::AreEqual(200, resp.status());
			Check(resp);
		}

		session sess_;
		connection conn_;
	};
}