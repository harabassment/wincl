#define UNICODE
#define _UNICODE
#define WINVER 0x0A00
#ifdef _WIN32_WINNT
#undef _WIN32_WINNT
#endif
#define _WIN32_WINNT 0x0A00
#include <windows.h>
#include <commctrl.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <shellapi.h>
#include <tlhelp32.h>
#include <uxtheme.h>
#include <string>
#include <vector>
#include <set>
#include <memory>
#include <atomic>
#include <thread>
#include <sstream>
#include <iomanip>
#include "policy.h"

using std::wstring;
using std::vector;
constexpr UINT SCAN_DONE=WM_APP+1, CLEAN_DONE=WM_APP+2, PROGRESS=WM_APP+3;
enum { ID_SCAN=100,ID_CANCEL,ID_RECYCLE,ID_OPEN,ID_EXCLUDE,ID_EXPORT,ID_TEMP,ID_LEFT,ID_LIST,ID_BIN };
HWND window,list,details,status,scanButton,cancelButton,recycleButton,openButton,excludeButton,exportButton,tempCheck,leftCheck,titleLabel,subtitleLabel,binButton;
HFONT font,titleFont;
std::atomic<bool> cancelFlag=false;
bool busy=false;
wstring localRoot,roamingRoot,sharedRoot,tempRoot,configRoot;
vector<wstring> ignored;
struct Entry {
    wstring path,reason; bool temp=false,enabled=true;
    uint64_t bytes=0,files=0,newest=0; DWORD volume=0,indexHigh=0,indexLow=0;
};
vector<Entry> entries;
struct Inventory { vector<wstring> names,paths; bool complete=true; };
struct ScanResult { vector<Entry> items; unsigned skipped=0; bool partial=false,inventoryComplete=true; wstring error; };
struct CleanResult { vector<wstring> removed,errors; uint64_t bytes=0; };
uint64_t ticks(FILETIME ft){return (uint64_t(ft.dwHighDateTime)<<32)|ft.dwLowDateTime;}
uint64_t nowTicks(){FILETIME ft;GetSystemTimeAsFileTime(&ft);return ticks(ft);}
wstring join(const wstring&a,const wstring&b){if(a.empty())return L"";return a+(a.back()==L'\\'?L"":L"\\")+b;}
wstring filename(const wstring&p){auto i=p.find_last_of(L"\\/");return i==wstring::npos?p:p.substr(i+1);}
wstring parent(const wstring&p){auto i=p.find_last_of(L"\\/");return i==wstring::npos?L"":p.substr(0,i);}
wstring full(const wstring&p){if(p.empty())return L"";wchar_t b[32768];DWORD n=GetFullPathNameW(p.c_str(),32768,b,nullptr);return n&&n<32768?b:L"";}
wstring known(REFKNOWNFOLDERID id){PWSTR p=nullptr;wstring s;if(SUCCEEDED(SHGetKnownFolderPath(id,0,nullptr,&p))){s=p;CoTaskMemFree(p);}return full(s);}
wstring sizeText(uint64_t bytes){std::wostringstream s;double n=double(bytes);const wchar_t* units[]={L"Б",L"КБ",L"МБ",L"ГБ",L"ТБ"};int i=0;while(n>=1024&&i<4){n/=1024;i++;}s<<std::fixed<<std::setprecision(i?1:0)<<n<<L" "<<units[i];return s.str();}
wstring dateText(uint64_t value){FILETIME ft{DWORD(value),DWORD(value>>32)},local;SYSTEMTIME st{};FileTimeToLocalFileTime(&ft,&local);FileTimeToSystemTime(&local,&st);wchar_t b[64];swprintf(b,64,L"%02u.%02u.%04u",st.wDay,st.wMonth,st.wYear);return b;}
wstring errorText(DWORD code){wchar_t* p=nullptr;FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER|FORMAT_MESSAGE_FROM_SYSTEM|FORMAT_MESSAGE_IGNORE_INSERTS,nullptr,code,0,(LPWSTR)&p,0,nullptr);wstring r=p?p:L"Неизвестная ошибка";if(p)LocalFree(p);return r;}
void message(const wstring&s,UINT flags=MB_OK|MB_ICONINFORMATION){MessageBoxW(window,s.c_str(),L"Чисто · CleanPC",flags);}
void progress(const wstring&s){auto p=new wstring(s);if(!PostMessageW(window,PROGRESS,0,(LPARAM)p))delete p;}
std::string utf8(const wstring&s){int n=WideCharToMultiByte(CP_UTF8,0,s.data(),int(s.size()),nullptr,0,nullptr,nullptr);std::string r(n,'\0');WideCharToMultiByte(CP_UTF8,0,s.data(),int(s.size()),r.data(),n,nullptr,nullptr);return r;}
wstring fromUtf8(const std::string&s){int n=MultiByteToWideChar(CP_UTF8,0,s.data(),int(s.size()),nullptr,0);wstring r(n,L'\0');MultiByteToWideChar(CP_UTF8,0,s.data(),int(s.size()),r.data(),n);return r;}
bool noLinks(const wstring&path){
    wstring p=full(path);if(p.empty()||p.rfind(L"\\\\",0)==0)return false;
    while(p.size()>3){DWORD a=GetFileAttributesW(p.c_str());if(a==INVALID_FILE_ATTRIBUTES||(a&FILE_ATTRIBUTE_REPARSE_POINT))return false;p=parent(p);}
    return true;
}
bool ignoredPath(const wstring&p){for(const auto&i:ignored)if(policy::under(p,i))return true;return false;}
bool safeConfig(){DWORD a=GetFileAttributesW(configRoot.c_str());if(a==INVALID_FILE_ATTRIBUTES){if(!CreateDirectoryW(configRoot.c_str(),nullptr))return false;}return noLinks(configRoot);}
bool writeText(const wstring&path,const wstring&text,bool append=false){
    HANDLE h=CreateFileW(path.c_str(),GENERIC_WRITE,FILE_SHARE_READ,nullptr,append?OPEN_ALWAYS:CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL|FILE_FLAG_OPEN_REPARSE_POINT,nullptr);
    if(h==INVALID_HANDLE_VALUE)return false;BY_HANDLE_FILE_INFORMATION info{};
    if(!GetFileInformationByHandle(h,&info)||(info.dwFileAttributes&FILE_ATTRIBUTE_REPARSE_POINT)){CloseHandle(h);return false;}
    if(append)SetFilePointer(h,0,nullptr,FILE_END);auto data=utf8(text);DWORD written=0;bool ok=WriteFile(h,data.data(),DWORD(data.size()),&written,nullptr)&&written==data.size();CloseHandle(h);return ok;
}
void logLine(const wstring&s){if(safeConfig())writeText(join(configRoot,L"history.log"),dateText(nowTicks())+L" "+s+L"\r\n",true);}
void loadIgnores(){
    if(!safeConfig())return;wstring p=join(configRoot,L"exclusions.txt");if(!noLinks(p))return;
    HANDLE h=CreateFileW(p.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT,nullptr);if(h==INVALID_HANDLE_VALUE)return;
    DWORD size=GetFileSize(h,nullptr);if(size>1024*1024){CloseHandle(h);return;}std::string data(size,'\0');DWORD n=0;ReadFile(h,data.data(),size,&n,nullptr);CloseHandle(h);data.resize(n);
    std::wistringstream in(fromUtf8(data));wstring line;while(std::getline(in,line)){if(!line.empty()&&line.back()==L'\r')line.pop_back();if(!line.empty())ignored.push_back(line);}
}
wstring regString(HKEY key,const wchar_t*name){DWORD type=0,n=0;if(RegQueryValueExW(key,name,nullptr,&type,nullptr,&n)!=ERROR_SUCCESS||(type!=REG_SZ&&type!=REG_EXPAND_SZ)||n>65536)return L"";vector<wchar_t>b(n/2+2);if(RegQueryValueExW(key,name,nullptr,&type,(BYTE*)b.data(),&n)!=ERROR_SUCCESS)return L"";wstring s=b.data();if(type==REG_EXPAND_SZ){wchar_t e[32768];DWORD count=ExpandEnvironmentStringsW(s.c_str(),e,32768);if(count&&count<32768)s=e;}return s;}
void processPaths(Inventory&inv){
    HANDLE snap=CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS,0);if(snap==INVALID_HANDLE_VALUE){inv.complete=false;return;}PROCESSENTRY32W pe{};pe.dwSize=sizeof pe;
    if(Process32FirstW(snap,&pe))do {HANDLE p=OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION,FALSE,pe.th32ProcessID);if(!p)continue;wchar_t b[32768];DWORD n=32768;if(QueryFullProcessImageNameW(p,0,b,&n))inv.paths.push_back(full(b));CloseHandle(p);}while(Process32NextW(snap,&pe));CloseHandle(snap);
}
Inventory inventory(){
    Inventory inv;const wchar_t* path=L"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall";
    for(auto hive:{HKEY_CURRENT_USER,HKEY_LOCAL_MACHINE})for(REGSAM view:{KEY_WOW64_64KEY,KEY_WOW64_32KEY}){
        HKEY key=nullptr;LSTATUS r=RegOpenKeyExW(hive,path,0,KEY_READ|view,&key);if(r==ERROR_FILE_NOT_FOUND)continue;if(r!=ERROR_SUCCESS){inv.complete=false;continue;}
        for(DWORD i=0;;i++){wchar_t name[512];DWORD n=512;r=RegEnumKeyExW(key,i,name,&n,nullptr,nullptr,nullptr,nullptr);if(r==ERROR_NO_MORE_ITEMS)break;if(r!=ERROR_SUCCESS){inv.complete=false;break;}HKEY app=nullptr;if(RegOpenKeyExW(key,name,0,KEY_READ|view,&app)!=ERROR_SUCCESS){inv.complete=false;continue;}
            for(auto field:{L"DisplayName",L"Publisher"}){auto value=regString(app,field);if(!value.empty())inv.names.push_back(value);}
            auto location=regString(app,L"InstallLocation");if(!location.empty())inv.paths.push_back(full(location));RegCloseKey(app);
        }RegCloseKey(key);
    }
    processPaths(inv);
    // Program directories also protect portable/partially registered app names.
    for(auto id:{&FOLDERID_ProgramFiles,&FOLDERID_ProgramFilesX86}){
        auto root=known(*id);if(root.empty()||!noLinks(root))continue;WIN32_FIND_DATAW d{};HANDLE f=FindFirstFileW(join(root,L"*").c_str(),&d);if(f==INVALID_HANDLE_VALUE)continue;
        do{if((d.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY)&&d.cFileName[0]!=L'.')inv.names.push_back(d.cFileName);}while(FindNextFileW(f,&d));FindClose(f);
    }
    return inv;
}
bool relatedPath(const wstring&p,const Inventory&inv){for(const auto&v:inv.paths)if(!v.empty()&&(policy::under(v,p)||policy::under(p,v)))return true;return false;}
struct Stats {uint64_t bytes=0,files=0,newest=0;bool valid=true;};
bool identity(const wstring&p,Entry&e){HANDLE h=CreateFileW(p.c_str(),FILE_READ_ATTRIBUTES,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,FILE_FLAG_BACKUP_SEMANTICS|FILE_FLAG_OPEN_REPARSE_POINT,nullptr);if(h==INVALID_HANDLE_VALUE)return false;BY_HANDLE_FILE_INFORMATION i{};bool ok=GetFileInformationByHandle(h,&i)&&!(i.dwFileAttributes&FILE_ATTRIBUTE_REPARSE_POINT);CloseHandle(h);if(ok){e.volume=i.dwVolumeSerialNumber;e.indexHigh=i.nFileIndexHigh;e.indexLow=i.nFileIndexLow;}return ok;}
Stats inspect(const wstring&path,bool,unsigned&budget,bool checkLocks=false){
    Stats s;vector<wstring>todo{path};
    while(!todo.empty()){
        if(cancelFlag||budget==0){s.valid=false;return s;}--budget;wstring p=todo.back();todo.pop_back();WIN32_FILE_ATTRIBUTE_DATA a{};
        if(!GetFileAttributesExW(p.c_str(),GetFileExInfoStandard,&a)||(a.dwFileAttributes&(FILE_ATTRIBUTE_REPARSE_POINT|FILE_ATTRIBUTE_SYSTEM|FILE_ATTRIBUTE_OFFLINE|FILE_ATTRIBUTE_RECALL_ON_DATA_ACCESS|FILE_ATTRIBUTE_RECALL_ON_OPEN))){s.valid=false;return s;}
        s.newest=std::max(s.newest,std::max(ticks(a.ftLastWriteTime),ticks(a.ftCreationTime)));
        if(a.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY){
            WIN32_FIND_DATAW d{};HANDLE f=FindFirstFileW(join(p,L"*").c_str(),&d);if(f==INVALID_HANDLE_VALUE){if(GetLastError()!=ERROR_FILE_NOT_FOUND)s.valid=false;if(!s.valid)return s;continue;}
            do{if(wcscmp(d.cFileName,L".")&&wcscmp(d.cFileName,L".."))todo.push_back(join(p,d.cFileName));if(todo.size()>50000){s.valid=false;break;}}while(FindNextFileW(f,&d));DWORD err=GetLastError();FindClose(f);if(!s.valid||(err!=ERROR_NO_MORE_FILES&&err!=ERROR_SUCCESS)){s.valid=false;return s;}
        }else{
            if(checkLocks){HANDLE h=CreateFileW(p.c_str(),GENERIC_READ|DELETE,0,nullptr,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT,nullptr);if(h==INVALID_HANDLE_VALUE){s.valid=false;return s;}CloseHandle(h);}
            s.files++;s.bytes+=(uint64_t(a.nFileSizeHigh)<<32)|a.nFileSizeLow;
        }
    }return s;
}
void scanTemp(ScanResult&r,unsigned&budget){
    if(tempRoot.empty()||!noLinks(tempRoot))return;vector<wstring>todo{tempRoot};unsigned visited=0;
    while(!todo.empty()&&!cancelFlag&&budget){auto p=todo.back();todo.pop_back();WIN32_FIND_DATAW d{};HANDLE f=FindFirstFileW(join(p,L"*").c_str(),&d);if(f==INVALID_HANDLE_VALUE){r.skipped++;continue;}
        do{
            if(cancelFlag||!budget)break;--budget;if(++visited%500==0)progress(L"Временные файлы: проверено "+std::to_wstring(visited));
            if(!wcscmp(d.cFileName,L".")||!wcscmp(d.cFileName,L".."))continue;auto path=join(p,d.cFileName);
            if(ignoredPath(path)||(d.dwFileAttributes&(FILE_ATTRIBUTE_REPARSE_POINT|FILE_ATTRIBUTE_SYSTEM|FILE_ATTRIBUTE_OFFLINE|FILE_ATTRIBUTE_READONLY|FILE_ATTRIBUTE_RECALL_ON_DATA_ACCESS|FILE_ATTRIBUTE_RECALL_ON_OPEN)))continue;
            if(d.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY){if(todo.size()<50000)todo.push_back(path);else r.partial=true;continue;}
            auto newest=std::max(ticks(d.ftLastWriteTime),ticks(d.ftCreationTime));if(!policy::oldEnough(newest,nowTicks(),7))continue;
            Entry e;e.path=path;e.temp=true;e.bytes=(uint64_t(d.nFileSizeHigh)<<32)|d.nFileSizeLow;e.files=1;e.newest=newest;e.reason=L"Файл в пользовательской папке Temp; создание и изменение старше 7 дней. Возраст не гарантирует, что файл больше не нужен.";
            if(identity(path,e))r.items.push_back(e);else r.skipped++;
            if(r.items.size()>=10000){r.partial=true;break;}
        }while(FindNextFileW(f,&d));FindClose(f);if(r.items.size()>=10000)break;
    }
    if(!budget||cancelFlag)r.partial=true;
}
ScanResult performScan(bool temp,bool leftovers){
    ScanResult r;unsigned budget=300000;Inventory inv;if(leftovers){progress(L"Читаю сведения об установленных программах…");inv=inventory();r.inventoryComplete=inv.complete;}
    if(temp)scanTemp(r,budget);
    if(leftovers)for(auto root:{localRoot,roamingRoot,sharedRoot}){
        if(cancelFlag||!budget)break;if(root.empty()||!noLinks(root)){r.skipped++;continue;}WIN32_FIND_DATAW d{};HANDLE f=FindFirstFileW(join(root,L"*").c_str(),&d);if(f==INVALID_HANDLE_VALUE){r.skipped++;continue;}
        do{
            if(cancelFlag||!budget||r.items.size()>=10000){r.partial=true;break;}
            if(!(d.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY)||(d.dwFileAttributes&(FILE_ATTRIBUTE_REPARSE_POINT|FILE_ATTRIBUTE_SYSTEM))||policy::protectedName(d.cFileName))continue;
            auto p=join(root,d.cFileName);if(ignoredPath(p)||policy::related(d.cFileName,inv.names)||relatedPath(p,inv))continue;
            if(!policy::oldEnough(std::max(ticks(d.ftLastWriteTime),ticks(d.ftCreationTime)),nowTicks(),30))continue;
            progress(L"Проверяю папку: "+wstring(d.cFileName));unsigned limited=std::min(budget,50000u);unsigned before=limited;auto s=inspect(p,true,limited);budget-=before-limited;
            if(!s.valid){r.skipped++;continue;}if(!policy::oldEnough(s.newest,nowTicks(),30))continue;
            Entry e;e.path=p;e.bytes=s.bytes;e.files=s.files;e.newest=s.newest;e.enabled=inv.complete;
            e.reason=L"Возможный остаток: имя не совпало с названиями и издателями в списках удаления и папками Program Files; внутри не найдено новых или изменённых объектов за 30 дней. Запущенные процессы с путём в этой папке не найдены. Это НЕ доказательство ненужности: здесь могут быть данные рабочей или portable-программы.";
            if(!inv.complete)e.reason+=L"\r\nУдаление заблокировано: сведения об установленных программах прочитаны не полностью.";
            if(identity(p,e))r.items.push_back(e);else r.skipped++;
        }while(FindNextFileW(f,&d));FindClose(f);
    }
    if(cancelFlag||!budget)r.partial=true;
    std::sort(r.items.begin(),r.items.end(),[](const Entry&a,const Entry&b){if(a.temp!=b.temp)return a.temp;return a.bytes>b.bytes;});return r;
}
bool allowed(const Entry&e){
    if(!e.enabled||!noLinks(e.path)||ignoredPath(e.path))return false;auto p=full(e.path);
    if(e.temp)return !tempRoot.empty()&&p!=tempRoot&&policy::under(p,tempRoot);
    auto base=parent(p);return (base==localRoot||base==roamingRoot||base==sharedRoot)&&!policy::protectedName(filename(p));
}
HRESULT recycle(const wstring&path){
    wchar_t drive[4]={path[0],L':',L'\\',0};if(path.size()<3||path[1]!=L':'||GetDriveTypeW(drive)!=DRIVE_FIXED)return HRESULT_FROM_WIN32(ERROR_NOT_SUPPORTED);
    IFileOperation* op=nullptr;HRESULT hr=CoCreateInstance(CLSID_FileOperation,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&op));if(FAILED(hr))return hr;
    // FOFX_RECYCLEONDELETE | FOFX_EARLYFAILURE | FOFX_ADDUNDORECORD.
    // Never retry a failed recycle operation with permanent deletion.
    hr=op->SetOperationFlags(FOF_ALLOWUNDO|FOF_NOCONFIRMATION|FOF_NOERRORUI|FOF_SILENT|0x00080000|0x00100000|0x20000000);
    IShellItem* item=nullptr;if(SUCCEEDED(hr))hr=SHCreateItemFromParsingName(path.c_str(),nullptr,IID_PPV_ARGS(&item));
    if(SUCCEEDED(hr))hr=op->DeleteItem(item,nullptr);if(SUCCEEDED(hr))hr=op->PerformOperations();BOOL aborted=FALSE;if(SUCCEEDED(hr)){op->GetAnyOperationsAborted(&aborted);if(aborted)hr=E_ABORT;}if(item)item->Release();op->Release();
    if(SUCCEEDED(hr)&&GetFileAttributesW(path.c_str())!=INVALID_FILE_ATTRIBUTES)hr=E_FAIL;return hr;
}
CleanResult performClean(const vector<Entry>&selected){
    CleanResult r;HRESULT init=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);if(FAILED(init)){r.errors.push_back(L"Не удалось запустить службу файловых операций Windows.");return r;}
    Inventory inv=inventory();for(const auto&e:selected){if(cancelFlag)break;progress(L"В корзину: "+filename(e.path));
        wstring why;Entry current;
        if(!allowed(e))why=L"Путь недоступен, исключён или содержит ссылку.";
        else if(!identity(e.path,current)||current.volume!=e.volume||current.indexHigh!=e.indexHigh||current.indexLow!=e.indexLow)why=L"Объект изменился после проверки. Запустите поиск заново.";
        else if(!e.temp&&(!inv.complete||policy::related(filename(e.path),inv.names)||relatedPath(e.path,inv)))why=L"Обнаружена связь с установленной или запущенной программой либо неполный список программ.";
        if(why.empty()){
            unsigned budget=50000;auto s=inspect(e.path,!e.temp,budget,true);
            if(!s.valid)why=L"Файл занят, нет доступа, найдена ссылка либо превышен лимит проверки.";
            else if(s.bytes!=e.bytes||s.files!=e.files||s.newest!=e.newest||!policy::oldEnough(s.newest,nowTicks(),e.temp?7:30))why=L"Содержимое изменилось после поиска. Запустите поиск заново.";
        }
        if(why.empty()){
            auto hr=recycle(e.path);if(FAILED(hr)){std::wostringstream s;s<<L"Windows не выполнила перемещение в корзину (0x"<<std::hex<<uint32_t(hr)<<L"). Повторного удаления без корзины нет.";why=s.str();}
        }
        if(why.empty()){r.removed.push_back(e.path);r.bytes+=e.bytes;logLine(L"RECYCLED "+e.path);}else{r.errors.push_back(e.path+L"\r\n"+why);logLine(L"SKIPPED "+e.path+L" | "+why);}
    }CoUninitialize();return r;
}
void updateSelection(){
    uint64_t total=0;int count=0;for(int i=0;i<int(entries.size());i++)if(ListView_GetCheckState(list,i)){count++;total+=entries[i].bytes;}
    SetWindowTextW(recycleButton,(L"В корзину ("+std::to_wstring(count)+L")").c_str());EnableWindow(recycleButton,!busy&&count>0);
    if(!busy)SetWindowTextW(status,(L"Найдено: "+std::to_wstring(entries.size())+L"  ·  Выбрано: "+std::to_wstring(count)+L"  ·  "+sizeText(total)+L"  ·  Ничего не выбрано автоматически").c_str());
}
void setBusy(bool value){busy=value;for(HWND h:{scanButton,openButton,excludeButton,exportButton,tempCheck,leftCheck})EnableWindow(h,!value);EnableWindow(cancelButton,value);EnableWindow(list,!value);EnableWindow(recycleButton,!value);if(value)EnableWindow(recycleButton,FALSE);else updateSelection();}
void populate(){
    SendMessageW(list,WM_SETREDRAW,FALSE,0);ListView_DeleteAllItems(list);
    for(int i=0;i<int(entries.size());i++){
        auto&e=entries[i];auto name=filename(e.path);LVITEMW item{};item.mask=LVIF_TEXT;item.iItem=i;item.pszText=name.data();ListView_InsertItem(list,&item);
        wstring category=e.temp?L"Временный файл":L"На ручную проверку";if(!e.enabled)category=L"Удаление заблокировано";
        ListView_SetItemText(list,i,1,category.data());auto bytes=sizeText(e.bytes);ListView_SetItemText(list,i,2,bytes.data());auto date=dateText(e.newest);ListView_SetItemText(list,i,3,date.data());ListView_SetItemText(list,i,4,e.path.data());
    }SendMessageW(list,WM_SETREDRAW,TRUE,0);InvalidateRect(list,nullptr,TRUE);updateSelection();
}
void showDetails(){int i=ListView_GetNextItem(list,-1,LVNI_SELECTED);if(i<0||i>=int(entries.size()))return;const auto&e=entries[i];SetWindowTextW(details,(e.path+L"\r\n"+sizeText(e.bytes)+L" · файлов: "+std::to_wstring(e.files)+L" · последняя запись: "+dateText(e.newest)+L"\r\n"+e.reason).c_str());}
void openSelected(){int i=ListView_GetNextItem(list,-1,LVNI_SELECTED);if(i<0)return;const auto&e=entries[i];if(!noLinks(e.path)){message(L"Папка недоступна или содержит ссылку.");return;}wstring arg=L"/select,\""+e.path+L"\"";ShellExecuteW(window,L"open",L"explorer.exe",arg.c_str(),nullptr,SW_SHOWNORMAL);}
void exportReport(){
    wchar_t file[32768]=L"CleanPC-report.tsv";OPENFILENAMEW of{};of.lStructSize=sizeof of;of.hwndOwner=window;of.lpstrFilter=L"Таблица TSV (*.tsv)\0*.tsv\0\0";of.lpstrFile=file;of.nMaxFile=32768;of.Flags=OFN_OVERWRITEPROMPT|OFN_PATHMUSTEXIST;of.lpstrDefExt=L"tsv";
    if(!GetSaveFileNameW(&of))return;wstring text=L"\ufeffКатегория\tБайт\tФайлов\tПоследняя запись\tПуть\tПричина\r\n";
    for(const auto&e:entries){wstring reason=e.reason;for(auto&c:reason)if(c==L'\r'||c==L'\n'||c==L'\t')c=L' ';text+=(e.temp?L"Temp":L"Ручная проверка")+wstring(L"\t")+std::to_wstring(e.bytes)+L"\t"+std::to_wstring(e.files)+L"\t"+dateText(e.newest)+L"\t"+e.path+L"\t"+reason+L"\r\n";}
    if(!writeText(file,text))message(L"Не удалось сохранить отчёт.",MB_OK|MB_ICONERROR);
}
void layout(){RECT rc;GetClientRect(window,&rc);int w=rc.right,h=rc.bottom;MoveWindow(titleLabel,22,16,w-44,38,TRUE);MoveWindow(subtitleLabel,24,57,w-48,43,TRUE);MoveWindow(tempCheck,24,105,315,26,TRUE);MoveWindow(leftCheck,350,105,w-375,26,TRUE);MoveWindow(scanButton,24,145,155,36,TRUE);MoveWindow(cancelButton,189,145,110,36,TRUE);MoveWindow(openButton,309,145,150,36,TRUE);MoveWindow(excludeButton,469,145,145,36,TRUE);MoveWindow(exportButton,624,145,145,36,TRUE);MoveWindow(binButton,779,145,165,36,TRUE);int bottom=h-200;MoveWindow(list,24,195,w-48,bottom-195,TRUE);MoveWindow(details,24,bottom+10,w-48,112,TRUE);MoveWindow(recycleButton,w-240,h-63,216,38,TRUE);MoveWindow(status,24,h-61,w-280,48,TRUE);}
HWND control(const wchar_t*cls,const wchar_t*text,DWORD style,int id){HWND h=CreateWindowExW(0,cls,text,WS_CHILD|WS_VISIBLE|style,0,0,1,1,window,(HMENU)(INT_PTR)id,GetModuleHandleW(nullptr),nullptr);SendMessageW(h,WM_SETFONT,(WPARAM)font,TRUE);return h;}
LRESULT CALLBACK procedure(HWND hwnd,UINT msg,WPARAM wp,LPARAM lp){
    switch(msg){
    case WM_CREATE:{window=hwnd;font=CreateFontW(-16,0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Segoe UI");titleFont=CreateFontW(-28,0,0,0,FW_SEMIBOLD,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Segoe UI");
        titleLabel=control(L"STATIC",L"Чисто",0,0);SendMessageW(titleLabel,WM_SETFONT,(WPARAM)titleFont,TRUE);
        subtitleLabel=control(L"STATIC",L"Временные файлы и возможные остатки программ. Сначала просмотр — затем ваш выбор.\r\nПапка без совпадения в списке программ может содержать нужные данные.",0,0);
        tempCheck=control(L"BUTTON",L"Временные файлы · старше 7 дней",BS_AUTOCHECKBOX|WS_TABSTOP,ID_TEMP);SendMessageW(tempCheck,BM_SETCHECK,BST_CHECKED,0);
        leftCheck=control(L"BUTTON",L"Возможные остатки · без изменений 30 дней",BS_AUTOCHECKBOX|WS_TABSTOP,ID_LEFT);SendMessageW(leftCheck,BM_SETCHECK,BST_CHECKED,0);
        scanButton=control(L"BUTTON",L"Проверить систему",BS_PUSHBUTTON|WS_TABSTOP,ID_SCAN);cancelButton=control(L"BUTTON",L"Остановить",BS_PUSHBUTTON|WS_TABSTOP,ID_CANCEL);EnableWindow(cancelButton,FALSE);
        openButton=control(L"BUTTON",L"Показать папку",BS_PUSHBUTTON|WS_TABSTOP,ID_OPEN);excludeButton=control(L"BUTTON",L"Не показывать",BS_PUSHBUTTON|WS_TABSTOP,ID_EXCLUDE);exportButton=control(L"BUTTON",L"Сохранить отчёт",BS_PUSHBUTTON|WS_TABSTOP,ID_EXPORT);binButton=control(L"BUTTON",L"Открыть корзину",BS_PUSHBUTTON|WS_TABSTOP,ID_BIN);
        list=control(WC_LISTVIEWW,L"",LVS_REPORT|LVS_SHOWSELALWAYS|WS_TABSTOP|WS_BORDER,ID_LIST);ListView_SetExtendedListViewStyle(list,LVS_EX_CHECKBOXES|LVS_EX_FULLROWSELECT|LVS_EX_DOUBLEBUFFER|LVS_EX_LABELTIP);SetWindowTheme(list,L"Explorer",nullptr);
        const wchar_t* columns[]={L"Имя",L"Категория",L"Размер",L"Изменено",L"Путь"};int widths[]={210,200,105,110,520};for(int i=0;i<5;i++){LVCOLUMNW c{};c.mask=LVCF_TEXT|LVCF_WIDTH;c.pszText=(LPWSTR)columns[i];c.cx=widths[i];ListView_InsertColumn(list,i,&c);}
        details=control(L"EDIT",L"Нажмите «Проверить систему». Поиск ничего не удаляет.\r\nВсе находки появятся без галочек. Выберите строку, чтобы увидеть причину.",ES_MULTILINE|ES_READONLY|ES_AUTOVSCROLL|WS_VSCROLL|WS_BORDER,0);
        recycleButton=control(L"BUTTON",L"В корзину (0)",BS_PUSHBUTTON|WS_TABSTOP,ID_RECYCLE);EnableWindow(recycleButton,FALSE);status=control(L"STATIC",L"Работает локально · без сети · без прав администратора",0,0);layout();return 0;}
    case WM_SIZE:if(list)layout();return 0;
    case WM_CTLCOLORSTATIC:{HDC dc=(HDC)wp;SetBkColor(dc,RGB(255,255,255));SetTextColor(dc,(HWND)lp==titleLabel?RGB(19,115,95):RGB(55,65,75));return (LRESULT)GetStockObject(WHITE_BRUSH);}
    case WM_GETMINMAXINFO:((MINMAXINFO*)lp)->ptMinTrackSize={1000,720};return 0;
    case WM_COMMAND:switch(LOWORD(wp)){
        case ID_SCAN:{if(busy)break;bool t=SendMessageW(tempCheck,BM_GETCHECK,0,0)==BST_CHECKED,l=SendMessageW(leftCheck,BM_GETCHECK,0,0)==BST_CHECKED;if(!t&&!l){message(L"Выберите хотя бы один вид поиска.");break;}cancelFlag=false;entries.clear();populate();SetWindowTextW(details,L"Поиск выполняется. Удаление начнётся только после вашего выбора и подтверждения.");setBusy(true);std::thread([t,l]{auto r=new ScanResult;try{*r=performScan(t,l);}catch(...){r->error=L"Не удалось завершить поиск. Сузьте область и повторите.";}PostMessageW(window,SCAN_DONE,0,(LPARAM)r);}).detach();break;}
        case ID_CANCEL:cancelFlag=true;SetWindowTextW(status,L"Останавливаю после текущего объекта…");EnableWindow(cancelButton,FALSE);break;
        case ID_OPEN:if(!busy)openSelected();break;
        case ID_BIN:ShellExecuteW(window,L"open",L"shell:RecycleBinFolder",nullptr,nullptr,SW_SHOWNORMAL);break;
        case ID_EXPORT:if(!busy)exportReport();break;
        case ID_EXCLUDE:{if(busy)break;int i=ListView_GetNextItem(list,-1,LVNI_SELECTED);if(i<0){message(L"Выберите строку для исключения.");break;}auto p=entries[i].path;ignored.push_back(p);wstring text;for(auto&s:ignored)text+=s+L"\r\n";if(!safeConfig()||!writeText(join(configRoot,L"exclusions.txt"),text)){ignored.pop_back();message(L"Не удалось сохранить исключение.",MB_OK|MB_ICONERROR);break;}entries.erase(entries.begin()+i);populate();SetWindowTextW(details,L"Путь исключён. Для сброса исключений удалите exclusions.txt в %LOCALAPPDATA%\\CleanPC.");break;}
        case ID_RECYCLE:{if(busy)break;vector<Entry> selected;bool leftovers=false;uint64_t total=0;for(int i=0;i<int(entries.size());i++)if(ListView_GetCheckState(list,i)){selected.push_back(entries[i]);leftovers|=!entries[i].temp;total+=entries[i].bytes;}if(selected.empty())break;
            wstring text=L"Переместить выбранные объекты в корзину Windows?\r\nОбъектов: "+std::to_wstring(selected.size())+L" · "+sizeText(total)+L"\r\n\r\nЗакройте программы, связанные с выбранными файлами.";
            if(leftovers)text+=L"\r\n\r\nСреди выбранного есть возможные остатки. Нужные настройки, сохранения и данные тоже могут попасть в этот список. Проверьте содержимое папок.";
            text+=L"\r\n\r\nПока корзина не очищена, место на диске не освобождается. Восстановление — через корзину Windows.";
            if(MessageBoxW(window,text.c_str(),L"Подтвердите перемещение",MB_YESNO|MB_ICONWARNING|MB_DEFBUTTON2)!=IDYES)break;
            cancelFlag=false;setBusy(true);std::thread([selected]{auto r=new CleanResult;try{*r=performClean(selected);}catch(...){r->errors.push_back(L"Операция прервана. Проверьте корзину и повторите поиск.");}PostMessageW(window,CLEAN_DONE,0,(LPARAM)r);}).detach();break;}
        }return 0;
    case WM_NOTIFY:{auto n=(NMHDR*)lp;if(n->hwndFrom==list){if(n->code==LVN_ITEMCHANGING){auto change=(NMLISTVIEW*)lp;if(change->iItem>=0&&change->iItem<int(entries.size())&&!entries[change->iItem].enabled&&((change->uNewState&LVIS_STATEIMAGEMASK)>>12)==2)return TRUE;}if(n->code==LVN_ITEMCHANGED){showDetails();if(!busy)updateSelection();}if(n->code==NM_DBLCLK&&!busy)openSelected();}return 0;}
    case PROGRESS:{std::unique_ptr<wstring> p((wstring*)lp);SetWindowTextW(status,p->c_str());return 0;}
    case SCAN_DONE:{std::unique_ptr<ScanResult>r((ScanResult*)lp);entries=std::move(r->items);populate();setBusy(false);wstring text=L"Поиск завершён. Найдено: "+std::to_wstring(entries.size())+L". Пропущено недоступных или сложных папок: "+std::to_wstring(r->skipped)+L".\r\nРазмеры указаны по длине файлов; это не точный прогноз освобождённого места.";if(r->partial)text+=L"\r\nПоиск остановлен или достигнут лимит. Список неполный.";if(!r->inventoryComplete)text+=L"\r\nСписок программ прочитан не полностью. Удаление возможных остатков заблокировано.";if(!r->error.empty())text+=L"\r\n"+r->error;SetWindowTextW(details,text.c_str());return 0;}
    case CLEAN_DONE:{std::unique_ptr<CleanResult>r((CleanResult*)lp);for(auto&p:r->removed)entries.erase(std::remove_if(entries.begin(),entries.end(),[&](const Entry&e){return e.path==p;}),entries.end());populate();setBusy(false);wstring text=L"Перемещено в корзину: "+std::to_wstring(r->removed.size())+L" · "+sizeText(r->bytes)+L".\r\nОшибок / пропусков: "+std::to_wstring(r->errors.size())+L". Восстановление — через корзину Windows.\r\n";for(unsigned i=0;i<r->errors.size()&&i<20;i++)text+=L"\r\n"+r->errors[i]+L"\r\n";if(cancelFlag)text+=L"\r\nОперация остановлена. Невыполненные объекты остались на месте.";SetWindowTextW(details,text.c_str());return 0;}
    case WM_CLOSE:if(busy){cancelFlag=true;SetWindowTextW(status,L"Останавливаю… После завершения окно можно закрыть.");return 0;}DestroyWindow(hwnd);return 0;
    case WM_DESTROY:DeleteObject(font);DeleteObject(titleFont);PostQuitMessage(0);return 0;
    }return DefWindowProcW(hwnd,msg,wp,lp);
}
int WINAPI wWinMain(HINSTANCE instance,HINSTANCE,LPWSTR,int show){
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_SYSTEM_AWARE);CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);INITCOMMONCONTROLSEX cc{sizeof cc,ICC_LISTVIEW_CLASSES|ICC_STANDARD_CLASSES};InitCommonControlsEx(&cc);
    localRoot=known(FOLDERID_LocalAppData);roamingRoot=known(FOLDERID_RoamingAppData);sharedRoot=known(FOLDERID_ProgramData);if(localRoot.empty())return 1;tempRoot=join(localRoot,L"Temp");configRoot=join(localRoot,L"CleanPC");loadIgnores();
    WNDCLASSEXW wc{sizeof wc};wc.hInstance=instance;wc.lpfnWndProc=procedure;wc.lpszClassName=L"CleanPC.Native.v1";wc.hCursor=LoadCursorW(nullptr,IDC_ARROW);wc.hIcon=LoadIconW(instance,MAKEINTRESOURCEW(101));if(!wc.hIcon)wc.hIcon=LoadIconW(nullptr,IDI_APPLICATION);wc.hbrBackground=(HBRUSH)(COLOR_WINDOW+1);RegisterClassExW(&wc);
    HWND hwnd=CreateWindowExW(0,wc.lpszClassName,L"Чисто · проверка и очистка Windows",WS_OVERLAPPEDWINDOW,CW_USEDEFAULT,CW_USEDEFAULT,1180,830,nullptr,nullptr,instance,nullptr);if(!hwnd)return 2;ShowWindow(hwnd,show);UpdateWindow(hwnd);MSG msg;while(GetMessageW(&msg,nullptr,0,0)>0){if(!IsDialogMessageW(hwnd,&msg)){TranslateMessage(&msg);DispatchMessageW(&msg);}}CoUninitialize();return 0;
}
