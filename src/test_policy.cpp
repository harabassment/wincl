#include "policy.h"
#include "check.h"
#include <iostream>
int main(){
 using namespace policy;
 CHECK(related(L"ExampleApp",{L"Example App 3.2"}));
 CHECK(related(L"ExampleAppCache",{L"Example App"}));
 CHECK(related(L"Example",{L"Example Inc."}));
 CHECK(related(L"abc",{}));
 CHECK(!related(L"RemovedEditor",{L"Other Application"}));
 CHECK(protectedName(L"MICROSOFT"));
 CHECK(protectedName(L"Packages"));
 CHECK(protectedName(L".config"));
 CHECK(!protectedName(L"RemovedEditor"));
 CHECK(under(L"C:\\Users\\Me\\AppData\\Local\\App\\x",L"c:\\users\\me\\appdata\\local\\app"));
 CHECK(!under(L"C:\\TempEvil\\file",L"C:\\Temp"));
 CHECK(!under(L"C:\\Users\\Other",L"C:\\Users\\Me"));
 CHECK(under(L"C:\\Temp\\x",L"C:\\Temp\\"));
 const uint64_t now=200ULL*86400*10000000;
 CHECK(oldEnough(now-31ULL*86400*10000000,now,30));
 CHECK(!oldEnough(now-29ULL*86400*10000000,now,30));
 CHECK(!oldEnough(now+1,now,7));
 CHECK(!oldEnough(0,now,7));
 std::cout<<"18 policy assertions passed\n";
}
