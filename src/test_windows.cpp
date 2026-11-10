#define wWinMain cleanpcApplication
#include "main.cpp"
#undef wWinMain
#include "check.h"
#include <iostream>
int wmain(){
    CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);
    wchar_t t[32768];GetTempPathW(32768,t);GUID guid;CoCreateGuid(&guid);wchar_t id[64];StringFromGUID2(guid,id,64);
    auto sandbox=join(t,wstring(L"CleanPC-Test-")+id);CHECK(CreateDirectoryW(sandbox.c_str(),nullptr));
    auto p=join(sandbox,L"old.tmp");HANDLE h=CreateFileW(p.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,0,nullptr);CHECK(h!=INVALID_HANDLE_VALUE);DWORD n;CHECK(WriteFile(h,"hello",5,&n,nullptr));
    uint64_t old=nowTicks()-uint64_t(70)*86400*10000000;FILETIME ft{DWORD(old),DWORD(old>>32)};CHECK(SetFileTime(h,&ft,nullptr,&ft));CloseHandle(h);
    unsigned budget=100;auto s=inspect(p,false,budget);CHECK(s.valid&&s.bytes==5&&s.files==1&&s.newest==old);
    CHECK(noLinks(p));Entry e;e.path=p;e.temp=true;e.bytes=s.bytes;e.files=s.files;e.newest=s.newest;CHECK(identity(p,e));Entry other;CHECK(identity(p,other)&&other.indexLow==e.indexLow&&other.volume==e.volume);
    tempRoot=sandbox;CHECK(allowed(e));Entry outside=e;outside.path=join(parent(sandbox),L"other.tmp");CHECK(!allowed(outside));
    h=CreateFileW(p.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,0,nullptr);CHECK(h!=INVALID_HANDLE_VALUE);budget=100;s=inspect(p,false,budget,true);CHECK(!s.valid);CloseHandle(h);
    budget=0;s=inspect(p,false,budget);CHECK(!s.valid);
    ignored.push_back(p);CHECK(!allowed(e));ignored.clear();
    auto link=join(sandbox,L"link.tmp");bool linkTest=CreateSymbolicLinkW(link.c_str(),p.c_str(),SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE);if(linkTest){CHECK(!noLinks(link));budget=100;s=inspect(link,false,budget);CHECK(!s.valid);DeleteFileW(link.c_str());}
    h=CreateFileW(p.c_str(),FILE_APPEND_DATA,0,nullptr,OPEN_EXISTING,0,nullptr);CHECK(h!=INVALID_HANDLE_VALUE);CHECK(WriteFile(h,"!",1,&n,nullptr));CloseHandle(h);budget=100;s=inspect(p,false,budget);CHECK(s.bytes==6&&s.bytes!=e.bytes);
    CHECK(DeleteFileW(p.c_str()));CHECK(RemoveDirectoryW(sandbox.c_str()));CoUninitialize();std::cout<<"Windows file checks passed; symlink check "<<(linkTest?"passed":"unavailable")<<"\n";return 0;
}
