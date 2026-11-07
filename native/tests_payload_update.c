/* Exercise the production hook worker against the installed codecs,
   inside THIS isolated process. The call graph is a fixture, PCM is generated,
   and no running WeChat process is opened or modified. */
#include "payload.c"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
typedef void* (*CreateOpus)(int,int,int,int*);
typedef int (*EncodeOpus)(void*,const short*,int,unsigned char*,int);
typedef int (*SilkSize)(int*);
typedef int (*SilkInit)(void*,int*);
static void p(void* b,unsigned o,uint64_t v){memcpy((char*)b+o,&v,8);}
static void n(void* b,unsigned o,int v){memcpy((char*)b+o,&v,4);}
static void* fake[9];
static void graph(WxProfile profile,uintptr_t voice,void* state,int codec){
    size_t sizes[]={0x200000,0x68,0xa00,0x400,0x80,0x58,0x5300,16,0xa0};
    for(int i=0;i<9;i++){fake[i]=calloc(1,sizes[i]);assert(fake[i]);}
    context=(WxContext){.process=GetCurrentProcess(),.pid=GetCurrentProcessId(),.conf=(uintptr_t)fake[0],.voice=voice,.profile=profile};
    p(fake[0],profile.service,(uintptr_t)fake[1]);p(fake[1],0,context.conf+profile.service_vt);p(fake[1],8,(uintptr_t)fake[2]);
    p(fake[2],0,context.conf+profile.backend_vt);p(fake[2],0x980,(uintptr_t)fake[3]+8);
    p(fake[3],0,voice+profile.engine_vt);p(fake[3],8,voice+0xf8ab90);p(fake[3],0x240,(uintptr_t)fake[4]);
    p(fake[4],0,voice+profile.xve_vt);p(fake[4],0x20,(uintptr_t)fake[5]);n(fake[4],0x28,1);
    p(fake[5],0,(uintptr_t)fake[6]);p(fake[6],0,voice+profile.channel_vt);p(fake[6],0x5230,(uintptr_t)fake[7]);n(fake[6],0x5240,codec);n(fake[6],0x5254,20);
    p(fake[7],0,(uintptr_t)fake[8]);p(fake[8],0,voice+(codec==WX_SILK?profile.silk_vt:profile.opus_vt));n(fake[8],8,codec);p(fake[8],0x10,(uintptr_t)state);
    n(fake[8],0x24,30000);
    if(codec==WX_SILK){int control[11]={16000,24000,320,30000,0,2,0,0,0,0,0};memcpy((char*)fake[8]+0x18,control,sizeof(control));n(fake[8],0x70,16000);n(fake[8],0x74,320);n(fake[8],0x78,1);n(fake[8],0x7c,30000);}
    assert(!wx_locate(&context,&identity));
}
static void frame(void* state,int codec,EncodeOpus opus,SilkEncode silk){
    short pcm[320];unsigned char output[4096];for(int i=0;i<320;i++)pcm[i]=(short)(1000*sin(6.283185307179586*350*i/16000));
    if(codec==WX_SILK){short count=sizeof(output),extra=0;assert(!silk(state,(int*)((char*)fake[8]+0x18),pcm,320,output,&count,&extra));assert(count>0);}
    else assert(opus(state,pcm,320,output,sizeof(output))>0);
}
static void exercise(WxProfile profile,uintptr_t base,int codec){
    void* state;int error=0,status[32]={0};
    OpusCtl ctl=(OpusCtl)(base+profile.opus_ctl);EncodeOpus encode=(EncodeOpus)(base+0x51cf80);SilkEncode silk=(SilkEncode)(base+profile.silk_encode);
    if(codec==WX_SILK){
        SilkSize size;SilkInit init;FARPROC fn=GetProcAddress((HMODULE)base,"SKP_Silk_SDK_Get_Encoder_Size");memcpy(&size,&fn,sizeof(size));
        fn=GetProcAddress((HMODULE)base,"SKP_Silk_SDK_InitEncoder");memcpy(&init,&fn,sizeof(init));int bytes=0;assert(size && init && !size(&bytes));state=calloc(1,bytes);assert(state && !init(state,status));
    }else{state=((CreateOpus)(base+0x5197f0))(16000,1,2048,&error);assert(state && !error && !ctl(state,4002,30000));}
    graph(profile,base,state,codec);frame(state,codec,encode,silk);WxValues values;assert(!wx_values(&context,&identity,&values) && values.target==30000);
    HANDLE mapping=CreateFileMappingW(INVALID_HANDLE_VALUE,NULL,PAGE_READWRITE,0,sizeof(WxShared),NULL);assert(mapping);
    shared=MapViewOfFile(mapping,FILE_MAP_ALL_ACCESS,0,0,sizeof(WxShared));assert(shared);
    WxShared* view=MapViewOfFile(mapping,FILE_MAP_ALL_ACCESS,0,0,sizeof(WxShared));assert(view);
    shared->host_pid=shared->target_pid=GetCurrentProcessId();shared->call=identity;shared->profile=profile;
    HANDLE thread=CreateThread(NULL,0,worker,mapping,0,NULL);assert(thread);
    ULONGLONG deadline=GetTickCount64()+5000;
    if(codec!=WX_SILK){
        // A muted call has no new PCM frames. Setting the target must still
        // arm the hook; the encoder's last-used rate updates on the next frame.
        ULONGLONG idle=GetTickCount64()+1000;
        while(view->status==WX_STARTING && GetTickCount64()<idle)Sleep(10);
        assert(view->status==WX_ACTIVE && view->enabled);
        assert(!wx_values(&context,&identity,&values) && values.target==128000 && values.current==30000);
        assert(!view->overrides);
        frame(state,codec,encode,silk);
        assert(!wx_values(&context,&identity,&values) && values.current==128000);
    }
    while(view->status==WX_STARTING && GetTickCount64()<deadline){frame(state,codec,encode,silk);Sleep(10);}
    assert(view->status==WX_ACTIVE && view->enabled);
    int maximum=codec==WX_SILK?100000:128000;
    // Simulate WeChat issuing subsequent defaults while enhancement is active.
    n(fake[8],0x24,40000);if(codec==WX_SILK)n(fake[8],0x7c,40000);
    for(int i=0;i<100;i++){
        if(codec!=WX_SILK)assert(!ctl(state,4002,40000));frame(state,codec,encode,silk);
        assert(!wx_values(&context,&identity,&values) && values.target==maximum && values.current==maximum && values.normal==40000);Sleep(2);
    }
    assert(view->overrides>=100 && !view->failures);if(codec==WX_SILK)assert(view->frames>=100);
    InterlockedExchange(&view->command,WX_STOP);deadline=GetTickCount64()+4000;
    while(WaitForSingleObject(thread,0)==WAIT_TIMEOUT && GetTickCount64()<deadline){frame(state,codec,encode,silk);Sleep(10);}
    assert(WaitForSingleObject(thread,0)==WAIT_OBJECT_0 && view->status==WX_STOPPED && !view->error && !view->enabled);
    assert(!wx_values(&context,&identity,&values) && values.target==40000 && values.current==40000);
    printf("{\"codec\":%d,\"held\":%d,\"overrides\":%lld,\"successful_hook_frames\":%lld,\"failures\":%lld,\"restored\":%d}\n",codec,maximum,view->overrides,view->frames,view->failures,values.target);
    UnmapViewOfFile(view);CloseHandle(thread);for(int i=0;i<9;i++)free(fake[i]);if(codec==WX_SILK)free(state);
}
int wmain(int argc,wchar_t** argv){
    assert(argc==3);WxProfile profile;assert(wx_resolve(argv[1],argv[2],&profile));assert(profile.opus_ctl==0x51d020 && profile.silk_encode==0x4e23e0);
    HMODULE module=LoadLibraryExW(argv[1],NULL,LOAD_WITH_ALTERED_SEARCH_PATH);assert(module);
    exercise(profile,(uintptr_t)module,WX_OPUS16);exercise(profile,(uintptr_t)module,WX_SILK);
    puts("PASS: production native hooks hold 128/100 against repeated default requests and restore the latest default");return 0;
}
