#define wWinMain cleanpcApplication
#include "main.cpp"
#undef wWinMain
#include "check.h"
#include <iostream>
int wmain(){
 CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);wchar_t t[32768];GetTempPathW(32768,t);GUID guid;CoCreateGuid(&guid);wchar_t id[64];StringFromGUID2(guid,id,64);auto base=join(t,wstring(L"CleanPC-Recycle-")+id);CHECK(CreateDirectoryW(base.c_str(),nullptr));
 tempRoot=base;configRoot=join(base,L"CleanPC");auto p=join(base,L"cleanpc-recycle-fixture.tmp");HANDLE h=CreateFileW(p.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,0,nullptr);CHECK(h!=INVALID_HANDLE_VALUE);DWORD n;CHECK(WriteFile(h,"CLEANPC-RECYCLE-FIXTURE",23,&n,nullptr));auto old=nowTicks()-uint64_t(70)*86400*10000000;FILETIME ft{DWORD(old),DWORD(old>>32)};CHECK(SetFileTime(h,&ft,nullptr,&ft));CloseHandle(h);
 unsigned budget=100;auto stats=inspect(p,false,budget);CHECK(stats.valid);Entry e;e.path=p;e.temp=true;e.bytes=stats.bytes;e.files=stats.files;e.newest=stats.newest;CHECK(identity(p,e));
 h=CreateFileW(p.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,0,nullptr);CHECK(h!=INVALID_HANDLE_VALUE);auto locked=performClean({e});CHECK(locked.removed.empty()&&locked.errors.size()==1);CHECK(GetFileAttributesW(p.c_str())!=INVALID_FILE_ATTRIBUTES);CloseHandle(h);
 auto result=performClean({e});if(result.removed.size()==1){CHECK(GetFileAttributesW(p.c_str())==INVALID_FILE_ATTRIBUTES);std::cout<<"Recycle operation succeeded; verify fixture is in test Trash.\n";}else{CHECK(!result.errors.empty());std::wcerr<<result.errors.front()<<L"\n";CHECK(result.errors.front().find(L"Windows не выполнила")!=wstring::npos);CHECK(GetFileAttributesW(p.c_str())!=INVALID_FILE_ATTRIBUTES);CHECK(DeleteFileW(p.c_str()));std::cout<<"Recycle unsupported: original file retained; no permanent-delete fallback.\n";}
 CHECK(DeleteFileW(join(configRoot,L"history.log").c_str()));CHECK(RemoveDirectoryW(configRoot.c_str()));CHECK(RemoveDirectoryW(base.c_str()));CoUninitialize();return 0;
}
