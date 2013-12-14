#include "stdafx.h"
#include "CppUnitTest.h"
#include <sstream>

#undef _HAS_EXCEPTIONS
#define _HAS_EXCEPTIONS 1
#include "../../http.h"

using namespace Microsoft::VisualStudio::CppUnitTestFramework;
using namespace std;
using namespace http;

namespace Tests
{		
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

			stringstream ss;
			resp.read(ss);
			Assert::IsTrue(ss.str().length() > 0);
		}

		TEST_METHOD(GetRelative)
		{
			request req("GET", "/");
			response resp = conn_.send(req);

			Assert::AreEqual(200, resp.status());

			stringstream ss;
			resp.read(ss);
			Assert::IsTrue(ss.str().length() > 0);
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

			stringstream ss;
			resp.read(ss);
			Assert::IsTrue(ss.str().length() > 0);
		}

		session sess_;
		connection conn_;
	};
}