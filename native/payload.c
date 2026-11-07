#include "wx.h"
#include "vendor/minhook/include/MinHook.h"
#include <string.h>

typedef int (*OpusCtl)(void*,int,...);
typedef int (*SilkEncode)(void*,const int32_t*,const short*,int,unsigned char*,short*,short*);
void* wx_opus_original=NULL;
static SilkEncode silk_original=NULL;
extern void wx_opus_detour(void);
static WxContext context;
static WxCall identity;
static WxShared* shared=NULL;
static volatile LONG running=0,enabled=0,inflight=0;
static BOOL initialized=FALSE,opus_created=FALSE,silk_created=FALSE;
static void* hook_target=NULL;

static BOOL same_call(void){return wx_check(&context,&identity)==WX_OK;}
// Called from an assembly shim which preserves all original arguments/registers.
uintptr_t wx_adjust_opus(void* encoder,uintptr_t command,uintptr_t value,uintptr_t unused) {
    (void)unused;InterlockedIncrement(&inflight);
    if(InterlockedCompareExchange(&enabled,0,0) && (uintptr_t)encoder==identity.p[8] && command==4002) {
        if(same_call()){value=128000;InterlockedIncrement64(&shared->overrides);}
        else InterlockedExchange(&enabled,0);
    }
    InterlockedDecrement(&inflight);return value;
}
static int silk_detour(void* encoder,const int32_t* control,const short* samples,int count,
                       unsigned char* output,short* bytes,short* extra) {
    InterlockedIncrement(&inflight);
    BOOL apply=InterlockedCompareExchange(&enabled,0,0) &&
        (uintptr_t)encoder==identity.p[8] && (uintptr_t)control==identity.p[7]+0x18;
    int result;
    if(apply && same_call() && control[3]>=5000 && control[3]<=100000) {
        int32_t copy[11];memcpy(copy,control,sizeof(copy));copy[3]=100000;
        InterlockedIncrement64(&shared->overrides);
        result=silk_original(encoder,copy,samples,count,output,bytes,extra);
        if(result){InterlockedIncrement64(&shared->failures);InterlockedExchange(&enabled,0);}
        else {InterlockedIncrement64(&shared->frames);if(*bytes>0)InterlockedExchangeAdd64(&shared->bytes,*bytes);}
    } else {
        if(apply)InterlockedExchange(&enabled,0);
        result=silk_original(encoder,control,samples,count,output,bytes,extra);
    }
    InterlockedDecrement(&inflight);return result;
}
static BOOL signature(void* target,const unsigned char* expected,size_t len) {
    unsigned char bytes[24];return len<=24 && wx_read(GetCurrentProcess(),(uintptr_t)target,bytes,len) && !memcmp(bytes,expected,len);
}
static void update_values(void) {
    WxValues values={0};if(wx_values(&context,&identity,&values)==0){
        InterlockedExchange(&shared->target,values.target);InterlockedExchange(&shared->current,values.current);
        InterlockedExchange(&shared->normal,values.normal);
    }
}
static DWORD WINAPI worker(void* param) {
    HANDLE mapping=(HANDLE)param,host=NULL;int error=0;BOOL modified=FALSE;
    host=OpenProcess(SYNCHRONIZE,FALSE,shared->host_pid);
    if(!host || shared->target_pid!=GetCurrentProcessId() || !same_call()){error=1;goto cleanup;}
    WxValues initial={0};if(wx_values(&context,&identity,&initial)){error=1;goto cleanup;}
    if(!initialized){if(MH_Initialize()!=MH_OK){error=2;goto cleanup;}initialized=TRUE;}
    if(identity.codec==WX_OPUS || identity.codec==WX_OPUS16) {
        static const unsigned char expected[]={0x48,0x8b,0xc4,0x89,0x50,0x10,0x4c,0x89,0x40,0x18,0x4c,0x89,0x48,0x20,0x53,0x55,0x41,0x56,0x48,0x81,0xec,0xa0,0,0};
        hook_target=(void*)(uintptr_t)(context.voice+context.profile.opus_ctl);
        if(!signature(hook_target,expected,sizeof(expected))){error=3;goto cleanup;}
        if(!opus_created){if(MH_CreateHook(hook_target,wx_opus_detour,&wx_opus_original)!=MH_OK){error=4;goto cleanup;}opus_created=TRUE;}
    } else if(identity.codec==WX_SILK) {
        static const unsigned char expected[]={0x40,0x53,0x55,0x56,0x57,0x41,0x54,0x48,0x83,0xec,0x50,0x8b,0x2a,0x4d,0x8b,0xe0,0x41,0x8b,0xf1,0x4c,0x8b,0xc2,0x48,0x8b};
        hook_target=(void*)(uintptr_t)(context.voice+context.profile.silk_encode);
        if(!signature(hook_target,expected,sizeof(expected))){error=3;goto cleanup;}
        if(!silk_created){if(MH_CreateHook(hook_target,silk_detour,(void**)&silk_original)!=MH_OK){error=4;goto cleanup;}silk_created=TRUE;}
    } else {error=1;goto cleanup;}
    InterlockedExchange(&enabled,1);
    if(MH_EnableHook(hook_target)!=MH_OK){error=4;goto cleanup;}
    modified=TRUE;
    if(identity.codec!=WX_SILK && ((OpusCtl)wx_opus_original)((void*)(uintptr_t)identity.p[8],4002,128000)!=0){error=5;goto cleanup;}
    for(int i=0;i<100;i++){
        update_values();
        // Opus accepts the target immediately, but its last-used rate changes
        // only on the next audio frame. A muted call can still be armed.
        if(shared->target==initial.maximum &&
            (identity.codec!=WX_SILK || (shared->current==initial.maximum && shared->frames>0)))break;
        if(i==99 || !enabled || !same_call()){error=5;goto cleanup;}Sleep(20);
    }
    InterlockedExchange(&shared->enabled,1);InterlockedExchange(&shared->status,WX_ACTIVE);
    while(shared->command!=WX_STOP && WaitForSingleObject(host,100)==WAIT_TIMEOUT) {
        if(!enabled || !same_call()){error=shared->failures?6:7;break;}update_values();
    }
cleanup:
    InterlockedExchange(&enabled,0);
    if(modified)MH_DisableHook(hook_target);
    while(InterlockedCompareExchange(&inflight,0,0))Sleep(1);
    if(modified && identity.codec!=WX_SILK && wx_opus_original && same_call()) {
        WxValues values={0};if(wx_values(&context,&identity,&values)==0 && values.normal>=6000 && values.normal<=510000)
            ((OpusCtl)wx_opus_original)((void*)(uintptr_t)identity.p[8],4002,values.normal);
    }
    // SILK never changes the application's controls. Wait for a natural frame.
    for(int i=0;i<50 && same_call();i++) {update_values();if(shared->target==shared->normal && shared->current==shared->normal)break;Sleep(20);}
    InterlockedExchange(&shared->enabled,0);InterlockedExchange(&shared->error,error);
    InterlockedExchange(&shared->status,error?WX_FAILED:WX_STOPPED);
    if(host)CloseHandle(host);
    UnmapViewOfFile(shared);shared=NULL;CloseHandle(mapping);hook_target=NULL;
    InterlockedExchange(&running,0);
    // Disabled hooks/trampolines stay resident until WeChat exits. Never unload
    // code while an audio callback could be returning through it.
    return 0;
}
__declspec(dllexport) DWORD WINAPI KekemiStart(void* name) {
    if(InterlockedCompareExchange(&running,1,0))return 10;
    HANDLE mapping=OpenFileMappingW(FILE_MAP_ALL_ACCESS,FALSE,(LPCWSTR)name);
    if(!mapping){InterlockedExchange(&running,0);return 11;}
    shared=MapViewOfFile(mapping,FILE_MAP_ALL_ACCESS,0,0,sizeof(WxShared));
    if(!shared || shared->magic!=WX_MAGIC || shared->version!=WX_VERSION) {
        if(shared)UnmapViewOfFile(shared);shared=NULL;CloseHandle(mapping);InterlockedExchange(&running,0);return 12;
    }
    identity=shared->call;context.process=GetCurrentProcess();context.pid=GetCurrentProcessId();
    context.conf=(uintptr_t)GetModuleHandleW(L"ConfSdk.dll");context.voice=(uintptr_t)GetModuleHandleW(L"VoipEngine.dll");
    wchar_t paths[3][MAX_PATH];
    if(wx_modules(context.pid,&context,paths) || !wx_resolve(paths[1],paths[2],&context.profile) ||
       memcmp(&context.profile,&shared->profile,sizeof(WxProfile)) || !wx_profile_loaded(&context)){
        UnmapViewOfFile(shared);shared=NULL;CloseHandle(mapping);InterlockedExchange(&running,0);return 14;
    }
    HANDLE thread=CreateThread(NULL,0,worker,mapping,0,NULL);
    if(!thread){UnmapViewOfFile(shared);shared=NULL;CloseHandle(mapping);InterlockedExchange(&running,0);return 13;}
    CloseHandle(thread);return 0;
}
BOOL WINAPI DllMain(HINSTANCE instance,DWORD why,LPVOID reserved){(void)reserved;if(why==DLL_PROCESS_ATTACH)DisableThreadLibraryCalls(instance);return TRUE;}
