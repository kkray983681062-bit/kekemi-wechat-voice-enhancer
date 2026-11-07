#include "wx.h"
#include <tlhelp32.h>
#include <bcrypt.h>
#include <string.h>
#include <stdlib.h>

BOOL wx_read(HANDLE h,uint64_t p,void* out,SIZE_T n) {
    SIZE_T got=0;
    return p>=0x10000 && p<0x7fffffffffffULL && n<=65536 &&
        ReadProcessMemory(h,(LPCVOID)(uintptr_t)p,out,n,&got) && got==n;
}
static int number(HANDLE h,uint64_t p) { int v=-1;wx_read(h,p,&v,4);return v; }
BOOL wx_equal(const WxCall* a,const WxCall* b) {return memcmp(a,b,sizeof(*a))==0;}

int wx_values(const WxContext* c,const WxCall* x,WxValues* a) {
    int code=wx_check(c,x);if(code)return code;
    HANDLE h=c->process;uint64_t state=x->p[8],codec=x->p[7];memset(a,0,sizeof(*a));
    if(x->codec==WX_SILK) {
        a->target=number(h,state+0x5144);a->current=number(h,state+0x3b64);
        a->normal=number(h,codec+0x7c);a->hz=number(h,codec+0x18);
        a->channels=number(h,codec+0x78);a->frame_ms=20;a->maximum=100000;
        if(a->hz!=16000 || a->channels!=1 || number(h,codec+0x70)!=16000 ||
           number(h,codec+0x20)!=320 || number(h,codec+0x74)!=320 ||
           a->target<5000 || a->target>100000 || a->current<5000 || a->current>100000 ||
           a->normal<5000 || a->normal>100000)return WX_CODEC;
    } else {
        a->target=number(h,state+0xa4);a->current=number(h,state+0xa0);
        a->normal=number(h,codec+0x24);a->hz=number(h,state+0x90);
        a->channels=number(h,state+0x70);a->frame_ms=number(h,x->p[5]+x->member[6]+0x24);a->maximum=128000;
        if(a->target<6000 || a->target>510000 || a->normal<6000 || a->normal>510000 ||
           (a->channels!=1 && a->channels!=2) ||
           (a->hz!=8000 && a->hz!=12000 && a->hz!=16000 && a->hz!=24000 && a->hz!=48000))return WX_CODEC;
    }
    return wx_check(c,x);
}

BOOL wx_sha256(const void* data,DWORD length,unsigned char out[32]) {
    BCRYPT_ALG_HANDLE alg=NULL;BOOL ok=FALSE;
    if(BCryptOpenAlgorithmProvider(&alg,BCRYPT_SHA256_ALGORITHM,NULL,0)>=0) {
        ok=BCryptHash(alg,NULL,0,(PUCHAR)data,length,out,32)>=0;BCryptCloseAlgorithmProvider(alg,0);
    }return ok;
}
BOOL wx_hash(const wchar_t* path,const unsigned char* expected,DWORD length) {
    if(length!=32)return FALSE;
    HANDLE f=CreateFileW(path,GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,NULL,OPEN_EXISTING,0,NULL);
    if(f==INVALID_HANDLE_VALUE)return FALSE;
    BCRYPT_ALG_HANDLE alg=NULL;BCRYPT_HASH_HANDLE hash=NULL;unsigned char digest[32],buffer[65536];DWORD got;BOOL ok=FALSE;
    if(BCryptOpenAlgorithmProvider(&alg,BCRYPT_SHA256_ALGORITHM,NULL,0)<0)goto end;
    if(BCryptCreateHash(alg,&hash,NULL,0,NULL,0,0)<0)goto end;
    for(;;) {if(!ReadFile(f,buffer,sizeof(buffer),&got,NULL))goto end;if(!got)break;
        if(BCryptHashData(hash,buffer,got,0)<0)goto end;}
    ok=BCryptFinishHash(hash,digest,32,0)>=0 && memcmp(digest,expected,32)==0;
end:if(hash)BCryptDestroyHash(hash);if(alg)BCryptCloseAlgorithmProvider(alg,0);CloseHandle(f);return ok;
}
int wx_modules(DWORD pid,WxContext* c,wchar_t paths[3][MAX_PATH]) {
    HANDLE snap=CreateToolhelp32Snapshot(TH32CS_SNAPMODULE|TH32CS_SNAPMODULE32,pid);
    if(snap==INVALID_HANDLE_VALUE)return WX_DENIED;
    MODULEENTRY32W me={0};me.dwSize=sizeof(me);int mask=0;
    for(BOOL ok=Module32FirstW(snap,&me);ok;ok=Module32NextW(snap,&me)) {
        int i=!_wcsicmp(me.szModule,L"Weixin.dll")?0:!_wcsicmp(me.szModule,L"VoipEngine.dll")?1:!_wcsicmp(me.szModule,L"ConfSdk.dll")?2:-1;
        if(i<0)continue;mask|=1<<i;wcscpy(paths[i],me.szExePath);
        if(i==1)c->voice=(uintptr_t)me.modBaseAddr;if(i==2)c->conf=(uintptr_t)me.modBaseAddr;
    }CloseHandle(snap);return mask==7?WX_OK:WX_NO_ENGINE;
}
void wx_close(WxContext* c){if(c->process)CloseHandle(c->process);memset(c,0,sizeof(*c));}
static uint64_t process_started(HANDLE process){
    FILETIME created,exited,kernel,user;
    return GetProcessTimes(process,&created,&exited,&kernel,&user)?((uint64_t)created.dwHighDateTime<<32)|created.dwLowDateTime:0;
}
int wx_instances(WxInstance* out,int capacity){
    HANDLE snap=CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS,0);if(snap==INVALID_HANDLE_VALUE)return 0;
    PROCESSENTRY32W pe={0};pe.dwSize=sizeof(pe);int count=0;
    for(BOOL ok=Process32FirstW(snap,&pe);ok && count<capacity;ok=Process32NextW(snap,&pe)){
        if(_wcsicmp(pe.szExeFile,L"Weixin.exe") && _wcsicmp(pe.szExeFile,L"WeChat.exe"))continue;
        WxContext c={0};c.pid=pe.th32ProcessID;c.process=OpenProcess(PROCESS_QUERY_INFORMATION|PROCESS_VM_READ|SYNCHRONIZE,FALSE,c.pid);
        if(!c.process)continue;
        wchar_t paths[3][MAX_PATH]={{0}};wx_modules(c.pid,&c,paths);
        // OCR, player and utility subprocesses are not account instances.
        DWORD size=MAX_PATH;uint64_t started=process_started(c.process);
        if(*paths[0] && started && QueryFullProcessImageNameW(c.process,0,out[count].exe,&size)){
            out[count].pid=c.pid;out[count].started=started;count++;
        }
        wx_close(&c);
    }CloseHandle(snap);return count;
}
static int find_instance(const wchar_t* wanted,DWORD pid,WxContext* result,WxCall* call,WxValues* values) {
    HANDLE snap=CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS,0);if(snap==INVALID_HANDLE_VALUE)return WX_ERROR;
    PROCESSENTRY32W pe={0};pe.dwSize=sizeof(pe);int last=WX_NO_PROCESS,found=0;WxContext idle={0};
    for(BOOL ok=Process32FirstW(snap,&pe);ok;ok=Process32NextW(snap,&pe)) {
        if(_wcsicmp(pe.szExeFile,L"Weixin.exe") && _wcsicmp(pe.szExeFile,L"WeChat.exe"))continue;
        if(pid && pid!=pe.th32ProcessID)continue;
        WxContext x={0};x.pid=pe.th32ProcessID;x.process=OpenProcess(PROCESS_QUERY_INFORMATION|PROCESS_VM_READ|SYNCHRONIZE,FALSE,x.pid);
        if(!x.process){last=WX_DENIED;continue;}
        x.started=process_started(x.process);if(!x.started){wx_close(&x);last=WX_DENIED;continue;}
        DWORD size=MAX_PATH;if(!QueryFullProcessImageNameW(x.process,0,x.exe,&size)){wx_close(&x);continue;}
        if(wanted && *wanted && _wcsicmp(wanted,x.exe)){wx_close(&x);continue;}
        wchar_t paths[3][MAX_PATH];int rc=wx_modules(x.pid,&x,paths);
        if(rc){wx_close(&x);if(last==WX_NO_PROCESS)last=rc;continue;}
        if(!wx_resolve(paths[1],paths[2],&x.profile) || !wx_profile_loaded(&x)){last=WX_UNSUPPORTED;wx_close(&x);continue;}
        WxCall cx={0};WxValues vx={0};rc=wx_locate(&x,&cx);if(!rc)rc=wx_values(&x,&cx,&vx);
        if(!rc){if(found++)wx_close(&x);else{*result=x;*call=cx;*values=vx;}}
        else {last=rc;if(!idle.process)idle=x;else wx_close(&x);}
    }CloseHandle(snap);
    if(found){wx_close(&idle);if(found>1){wx_close(result);return WX_MULTIPLE;}return WX_OK;}
    *result=idle;return last;
}
int wx_find(const wchar_t* wanted,WxContext* result,WxCall* call,WxValues* values){return find_instance(wanted,0,result,call,values);}
int wx_find_pid(DWORD pid,WxContext* result,WxCall* call,WxValues* values){return pid?find_instance(L"",pid,result,call,values):WX_NO_PROCESS;}
const wchar_t* wx_error(int code) {
    switch(code){case WX_NO_PROCESS:return L"请先打开所选微信，并接通语音。";
    case WX_NO_ENGINE:return L"尚未检测到语音引擎，请先接通语音。";
    case WX_UNSUPPORTED:return L"未能识别当前音频接口，请反馈微信版本以便适配。";
    case WX_NO_CALL:return L"等待电脑微信语音通话。";
    case WX_CODEC:return L"已检测到通话，但编码器或音频参数未适配。";
    case WX_MULTIPLE:return L"当前微信实例内检测到多个通话，暂不能确定目标。";
    case WX_DENIED:return L"权限不足，请让微信和工具使用相同权限。";
    case WX_CHANGED:return L"通话已变化，增强已停止，请重新读取并应用。";
    default:return L"无法完成操作，请重新读取通话状态。";}
}
