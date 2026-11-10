#pragma once
#include <string>
#include <vector>
#include <cwctype>
#include <algorithm>
#include <cstdint>
namespace policy {
inline std::wstring lower(std::wstring s) {
    std::transform(s.begin(),s.end(),s.begin(),[](wchar_t c){return std::towlower(c);});
    return s;
}
inline std::wstring normalize(const std::wstring& s) {
    std::wstring result;
    for (auto c:lower(s)) if (std::iswalnum(c)) result+=c;
    return result;
}
inline bool related(const std::wstring& folder,const std::vector<std::wstring>& installed) {
    auto name=normalize(folder);
    if(name.size()<4) return true;
    for(const auto& entry:installed) {
        auto app=normalize(entry);
        if(app.size()>=4 && (app.find(name)!=std::wstring::npos || name.find(app)!=std::wstring::npos)) return true;
    }
    return false;
}
inline bool under(const std::wstring& path,const std::wstring& base) {
    auto p=lower(path), b=lower(base);
    while(b.size()>3 && (b.back()==L'\\'||b.back()==L'/')) b.pop_back();
    return p==b || (p.size()>b.size() && p.compare(0,b.size(),b)==0 && (p[b.size()]==L'\\'||p[b.size()]==L'/'));
}
inline bool oldEnough(uint64_t changed,uint64_t now,unsigned days) {
    const uint64_t age=uint64_t(days)*86400*10000000;
    return changed>0 && now>=age && changed<=now-age;
}
inline bool protectedName(const std::wstring& name) {
    static const wchar_t* names[]={L"Microsoft",L"Windows",L"Packages",L"Programs",L"Temp",L"Application Data",
      L"ConnectedDevicesPlatform",L"Comms",L"D3DSCache",L"CrashDumps",L"Diagnostics",L"Publishers",
      L"Package Cache",L"PackageCache",L"USOPrivate",L"USOShared",L"regid.1991-06.com.microsoft",
      L"NVIDIA",L"NVIDIA Corporation",L"AMD",L"Intel",L"Docker",L"chocolatey",L"ssh",L"Crypto",
      L"Credentials",L"Protect",L"Vault",L"Fonts",L"VirtualStore",L"Start Menu",L"Desktop",L"Documents",
      L"Favorites",L"Templates",L"DeviceMetadataCache",L"SoftwareDistribution",L"CleanPC",
      L"Google",L"Mozilla",L"Apple",L"Adobe",L"Common Files",L"Application Support",L"cache",L"pip",L"npm",L"NuGet",
      L"Downloaded Installations",L"WindowsApps",L"ModifiableWindowsApps",L"MSIXVC",L"Installer"};
    auto n=lower(name);
    for(auto value:names) if(n==lower(value)) return true;
    return name.empty() || name.front()==L'.' || name.front()==L'$' || name.front()==L'{';
}
}
