#include "stdafx.h"
#include "CppUnitTest.h"

#define _HAS_EXCEPTIONS 0
#include "../../http.h"

using namespace Microsoft::VisualStudio::CppUnitTestFramework;
using namespace http;

namespace Tests
{		
	TEST_CLASS(UnitTest1)
	{
	public:
		TEST_METHOD_INITIALIZE(Setup)
		{
			
		}

		TEST_METHOD_CLEANUP(TearDown)
		{
			
		}

		TEST_METHOD(Session)
		{
			session sess;
		}

		TEST_METHOD(Connection)
		{
			session sess;
			connection conn(sess, "http://www.microsoft.com");
		}

		TEST_METHOD(Get)
		{
			session sess;
			connection conn(sess, "http://www.microsoft.com");
			int status = conn.get("http://www.microsoft.com", nullptr);
			Assert::AreEqual(200, status);
		}

	};
}