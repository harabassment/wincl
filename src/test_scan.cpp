#define wWinMain cleanpcApplication
#include "main.cpp"
#undef wWinMain
#include "check.h"
#include <iostream>
void oldObject(const wstring&p){HANDLE h=CreateFileW(p.c_str(),FILE_WRITE_ATTRIBUTES,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,FILE_FLAG_BACKUP_SEMANTICS,nullptr);CHECK(h!=INVALID_HANDLE_VALUE);auto old=nowTicks()-uint64_t(70)*86400*10000000;FILETIME ft{DWORD(old),DWORD(old>>32)};CHECK(SetFileTime(h,&ft,nullptr,&ft));CloseHandle(h);}
void folder(const wstring&p){CHECK(CreateDirectoryW(p.c_str(),nullptr));}
void file(const wstring&p){HANDLE h=CreateFileW(p.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,0,nullptr);CHECK(h!=INVALID_HANDLE_VALUE);DWORD n;CHECK(WriteFile(h,"fixture",7,&n,nullptr));CloseHandle(h);}
int wmain(){
 CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);wchar_t t[32768];GetTempPathW(32768,t);GUID guid;CoCreateGuid(&guid);wchar_t id[64];StringFromGUID2(guid,id,64);auto base=join(t,wstring(L"CleanPC-Scan-")+id);folder(base);
 localRoot=join(base,L"Local");roamingRoot=join(base,L"Roaming");sharedRoot=join(base,L"Shared");for(auto p:{localRoot,roamingRoot,sharedRoot})folder(p);tempRoot=join(localRoot,L"Temp");folder(tempRoot);configRoot=join(localRoot,L"CleanPC");
 auto temp=join(tempRoot,L"old.tmp");file(temp);oldObject(temp);
 auto leftover=join(roamingRoot,L"RemovedSampleEditor123");folder(leftover);auto data=join(leftover,L"data.bin");file(data);oldObject(data);oldObject(leftover);
 auto active=join(roamingRoot,L"ActiveSampleEditor123");folder(active);auto recent=join(active,L"new.bin");file(recent);oldObject(active);
 auto system=join(sharedRoot,L"Microsoft");folder(system);oldObject(system);
 auto result=performScan(true,true);CHECK(result.error.empty());CHECK(result.items.size()==2);CHECK(result.items[0].temp);CHECK(!result.items[1].temp);CHECK(result.items[0].bytes==7&&result.items[1].bytes==7);
 ignored.push_back(leftover);result=performScan(true,true);CHECK(result.items.size()==1&&result.items[0].temp);ignored.clear();
 // User cancellation produces an explicitly partial scan.
 cancelFlag=true;result=performScan(true,true);CHECK(result.partial);cancelFlag=false;
 CHECK(DeleteFileW(temp.c_str()));CHECK(DeleteFileW(data.c_str()));CHECK(DeleteFileW(recent.c_str()));for(auto p:{leftover,active,system,tempRoot,localRoot,roamingRoot,sharedRoot,base})CHECK(RemoveDirectoryW(p.c_str()));
 CoUninitialize();std::cout<<"Scan fixtures passed: old Temp + residual found; recent child + protected folder skipped; exclusion and cancellation passed\n";return 0;
}
