#include "wx.h"
#include <tlhelp32.h>
#include <bcrypt.h>
#include <stdio.h>
#include <string.h>

static uint64_t module_base(DWORD pid,const wchar_t* path,BOOL full) {
    HANDLE snap=CreateToolhelp32Snapshot(TH32CS_SNAPMODULE|TH32CS_SNAPMODULE32,pid);
    if(snap==INVALID_HANDLE_VALUE)return 0;
    MODULEENTRY32W me={0};me.dwSize=sizeof(me);uint64_t result=0;
    for(BOOL ok=Module32FirstW(snap,&me);ok;ok=Module32NextW(snap,&me))
        if(!_wcsicmp(full?me.szExePath:me.szModule,path)){result=(uintptr_t)me.modBaseAddr;break;}
    CloseHandle(snap);return result;
}
static uint64_t remote_loader(DWORD pid) {
    FARPROC f=GetProcAddress(GetModuleHandleW(L"kernel32.dll"),"LoadLibraryW");HMODULE owner=NULL;
    if(!f || !GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,(LPCWSTR)f,&owner))return 0;
    wchar_t file[MAX_PATH];if(!GetModuleFileNameW(owner,file,MAX_PATH))return 0;
    wchar_t* base=wcsrchr(file,L'\\');uint64_t remote=module_base(pid,base?base+1:file,FALSE);
    return remote?remote+((uintptr_t)f-(uintptr_t)owner):0;
}
static BOOL invoke(HANDLE process,uint64_t function,const wchar_t* text,DWORD* result) {
    SIZE_T bytes=(wcslen(text)+1)*sizeof(wchar_t),written=0;BOOL ok=FALSE;
    void* remote=VirtualAllocEx(process,NULL,bytes,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
    if(!remote)return FALSE;
    if(WriteProcessMemory(process,remote,text,bytes,&written) && written==bytes) {
        HANDLE thread=CreateRemoteThread(process,NULL,0,(LPTHREAD_START_ROUTINE)(uintptr_t)function,remote,0,NULL);
        if(thread){DWORD wait=WaitForSingleObject(thread,5000);
            if(wait==WAIT_OBJECT_0)ok=GetExitCodeThread(thread,result);
            else remote=NULL; // Leave argument intact if the thread is still using it.
            CloseHandle(thread);}
    }
    if(remote)VirtualFreeEx(process,remote,0,MEM_RELEASE);return ok;
}
static const unsigned char* rva_ptr(const unsigned char* data,DWORD size,DWORD rva) {
    if(size<sizeof(IMAGE_DOS_HEADER))return NULL;const IMAGE_DOS_HEADER* dos=(const void*)data;
    if(dos->e_magic!=IMAGE_DOS_SIGNATURE || dos->e_lfanew<0 || (DWORD)dos->e_lfanew>size-sizeof(IMAGE_NT_HEADERS64))return NULL;
    const IMAGE_NT_HEADERS64* nt=(const void*)(data+dos->e_lfanew);
    if(nt->Signature!=IMAGE_NT_SIGNATURE || nt->FileHeader.NumberOfSections>96)return NULL;
    const IMAGE_SECTION_HEADER* sections=IMAGE_FIRST_SECTION(nt);
    if((const unsigned char*)(sections+nt->FileHeader.NumberOfSections)>data+size)return NULL;
    for(int i=0;i<nt->FileHeader.NumberOfSections;i++){
        DWORD off=rva-sections[i].VirtualAddress;
        if(rva>=sections[i].VirtualAddress && off<sections[i].SizeOfRawData &&
            sections[i].PointerToRawData<size && off<size-sections[i].PointerToRawData)
            return data+sections[i].PointerToRawData+off;
    }return NULL;
}
static DWORD export_rva(const unsigned char* data,DWORD size) {
    const IMAGE_DOS_HEADER* dos=(const void*)data;const IMAGE_NT_HEADERS64* nt=(const void*)(data+dos->e_lfanew);
    const IMAGE_EXPORT_DIRECTORY* e=(const void*)rva_ptr(data,size,nt->OptionalHeader.DataDirectory[0].VirtualAddress);
    if(!e || e->NumberOfNames>1024 || e->NumberOfFunctions>4096)return 0;
    for(DWORD i=0;i<e->NumberOfNames;i++){
        const DWORD* nr=(const void*)rva_ptr(data,size,e->AddressOfNames+4*i);
        const WORD* ord=(const void*)rva_ptr(data,size,e->AddressOfNameOrdinals+2*i);
        if(!nr || !ord || *ord>=e->NumberOfFunctions)continue;
        const char* name=(const void*)rva_ptr(data,size,*nr);
        if(name && (const unsigned char*)name+12<=data+size && !memcmp(name,"KekemiStart",12)) {
            const DWORD* value=(const void*)rva_ptr(data,size,e->AddressOfFunctions+4*(*ord));return value?*value:0;
        }
    }return 0;
}
static BOOL payload_file(wchar_t* path,DWORD* entry) {
    HRSRC resource=FindResourceW(NULL,MAKEINTRESOURCEW(201),RT_RCDATA);
    if(!resource)return FALSE;
    DWORD bytes=SizeofResource(NULL,resource);const unsigned char* data=LockResource(LoadResource(NULL,resource));
    unsigned char digest[32];if(!data || !wx_sha256(data,bytes,digest))return FALSE;
    wchar_t temp[MAX_PATH],hex[65];DWORD len=GetTempPathW(MAX_PATH,temp);if(!len || len>=MAX_PATH-110)return FALSE;
    for(int i=0;i<32;i++)swprintf(hex+2*i,3,L"%02x",digest[i]);
    swprintf(path,MAX_PATH,L"%lskekemi-wx-native-%ls.dll",temp,hex);
    if(GetFileAttributesW(path)==INVALID_FILE_ATTRIBUTES) {
        HANDLE f=CreateFileW(path,GENERIC_WRITE,0,NULL,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,NULL);
        if(f!=INVALID_HANDLE_VALUE){DWORD written=0;BOOL ok=WriteFile(f,data,bytes,&written,NULL);CloseHandle(f);if(!ok || written!=bytes)return FALSE;}
    }
    *entry=export_rva(data,bytes);return *entry && wx_hash(path,digest,32);
}
BOOL wx_stop(WxSession* s,DWORD timeout) {
    BOOL stopped=TRUE;
    if(s->shared) {
        HANDLE target=OpenProcess(SYNCHRONIZE,FALSE,s->shared->target_pid);
        BOOL exited=!target || WaitForSingleObject(target,0)==WAIT_OBJECT_0;
        InterlockedExchange(&s->shared->command,WX_STOP);ULONGLONG end=GetTickCount64()+timeout;
        while(!exited && (s->shared->status==WX_STARTING || s->shared->status==WX_ACTIVE) && GetTickCount64()<end)Sleep(20);
        stopped=exited || s->shared->status==WX_STOPPED || s->shared->status==WX_FAILED;
        if(target)CloseHandle(target);
        if(!stopped)return FALSE;
        UnmapViewOfFile(s->shared);s->shared=NULL;
    }
    if(s->mapping)CloseHandle(s->mapping);memset(s,0,sizeof(*s));return stopped;
}
int wx_apply(WxContext* c,const WxCall* call,WxSession* s,wchar_t* message,size_t capacity) {
    if(s->shared){swprintf(message,capacity,L"增强已经启动，请先恢复默认。");return WX_ERROR;}
    WxContext checked={0};WxCall now={0};WxValues values={0};int result=wx_find_pid(c->pid,&checked,&now,&values);
    if(result || checked.started!=c->started || !wx_equal(&now,call) || memcmp(&checked.profile,&c->profile,sizeof(WxProfile))){wx_close(&checked);return result?result:WX_CHANGED;}wx_close(&checked);
    wchar_t file[MAX_PATH];DWORD entry=0;if(!payload_file(file,&entry)){swprintf(message,capacity,L"原生组件提取或校验失败。");return WX_ERROR;}
    HANDLE process=OpenProcess(PROCESS_QUERY_INFORMATION|PROCESS_CREATE_THREAD|PROCESS_VM_OPERATION|PROCESS_VM_READ|PROCESS_VM_WRITE|SYNCHRONIZE,FALSE,c->pid);
    if(!process)return WX_DENIED;
    uint64_t base=module_base(c->pid,file,TRUE);DWORD rc=0;
    if(!base){uint64_t loader=remote_loader(c->pid);
        if(!loader || !invoke(process,loader,file,&rc)){result=WX_ERROR;goto end;}
        base=module_base(c->pid,file,TRUE);if(!base){result=WX_ERROR;goto end;}}
    uint64_t random=0;if(BCryptGenRandom(NULL,(PUCHAR)&random,sizeof(random),BCRYPT_USE_SYSTEM_PREFERRED_RNG)<0){result=WX_ERROR;goto end;}
    swprintf(s->name,100,L"Local\\KekemiWxNative-%lu-%016llx",GetCurrentProcessId(),(unsigned long long)random);
    s->mapping=CreateFileMappingW(INVALID_HANDLE_VALUE,NULL,PAGE_READWRITE,0,sizeof(WxShared),s->name);
    if(!s->mapping || GetLastError()==ERROR_ALREADY_EXISTS){result=WX_ERROR;goto end;}
    s->shared=MapViewOfFile(s->mapping,FILE_MAP_ALL_ACCESS,0,0,sizeof(WxShared));if(!s->shared){result=WX_ERROR;goto end;}
    memset(s->shared,0,sizeof(*s->shared));s->shared->magic=WX_MAGIC;s->shared->version=WX_VERSION;
    s->shared->host_pid=GetCurrentProcessId();s->shared->target_pid=c->pid;s->shared->call=*call;s->shared->profile=c->profile;s->call=*call;
    if(!invoke(process,base+entry,s->name,&rc) || rc){swprintf(message,capacity,L"原生模块启动未确认（%lu），请稍后重试。",rc);result=WX_ERROR;goto end;}
    ULONGLONG deadline=GetTickCount64()+4000;
    while(s->shared->status==WX_STARTING && GetTickCount64()<deadline)Sleep(20);
    if(s->shared->status!=WX_ACTIVE || wx_values(c,call,&values)!=0 || values.target!=values.maximum ||
       (call->codec==WX_SILK && (values.current!=values.maximum || !s->shared->frames))) {
        fprintf(stderr,"native apply: status=%ld error=%ld target=%ld current=%ld frames=%lld overrides=%lld\n",
            s->shared->status,s->shared->error,s->shared->target,s->shared->current,s->shared->frames,s->shared->overrides);
        swprintf(message,capacity,L"未能确认编码保持（%ld），请关闭其他增强工具并保持开麦。",s->shared->error);result=WX_ERROR;goto end;}
    result=WX_OK;
end:
    CloseHandle(process);
    if(result && s->shared){wx_stop(s,2500);}
    else if(result && s->mapping){CloseHandle(s->mapping);memset(s,0,sizeof(*s));}
    return result;
}
