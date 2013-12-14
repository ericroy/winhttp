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
			stringstream ss;
			int status = conn_.get("http://www.microsoft.com", &ss);
			Assert::AreEqual(200, status);
			Assert::IsTrue(ss.str().length() > 0);
		}

		TEST_METHOD(GetRelative)
		{
			stringstream ss;
			int status = conn_.get("/", &ss);
			Assert::AreEqual(200, status);
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
			stringstream ss;
			int status = conn_.get("/", &ss);
			Assert::AreEqual(200, status);
			Assert::IsTrue(ss.str().length() > 0);
		}

		session sess_;
		connection conn_;
	};
}