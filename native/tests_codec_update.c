/* Isolated 4.2.0.13 codec preflight with synthetic mono PCM. This executable
   never opens another process and is not included in the shipped application. */
#include "wx.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>
typedef void* (*CreateOpus)(int,int,int,int*);
typedef int (*ControlOpus)(void*,int,...);
typedef int (*EncodeOpus)(void*,const short*,int,unsigned char*,int);
typedef int (*SilkSize)(int*);
typedef int (*SilkInit)(void*,int*);
typedef int (*SilkEncode)(void*,const int*,const short*,int,unsigned char*,short*,short*);
static int number(void* p,unsigned off){int v;memcpy(&v,(char*)p+off,4);return v;}
int wmain(int argc,wchar_t** argv){
    assert(argc==3);WxProfile p;assert(wx_resolve(argv[1],argv[2],&p));
    assert(p.opus_ctl==0x51d020 && p.silk_encode==0x4e23e0);
    HMODULE module=LoadLibraryExW(argv[1],NULL,LOAD_WITH_ALTERED_SEARCH_PATH);assert(module);
    uintptr_t base=(uintptr_t)module;
    // These two test-only RVAs were followed from COpusCoder's virtual methods.
    CreateOpus create=(CreateOpus)(base+0x5197f0);EncodeOpus encode=(EncodeOpus)(base+0x51cf80);
    ControlOpus ctl=(ControlOpus)(base+p.opus_ctl);
    short pcm[320];unsigned char output[4096];for(int i=0;i<320;i++)pcm[i]=(short)(1000*sin(6.283185307179586*350*i/16000));
    int error=-1;void* opus=create(16000,1,2048,&error);assert(opus && !error);
    int opus_rates[]={30000,128000,30000};
    for(int r=0;r<3;r++){
        assert(!ctl(opus,4002,opus_rates[r]));int total=0;
        for(int i=0;i<20;i++){int bytes=encode(opus,pcm,320,output,sizeof(output));assert(bytes>0);total+=bytes;}
        int rate=0;assert(!ctl(opus,4003,&rate) && rate==opus_rates[r]);
        assert(number(opus,0xa4)==rate && number(opus,0xa0)==rate && number(opus,0x90)==16000 && number(opus,0x70)==1);
        printf("{\"codec\":\"Opus\",\"target\":%d,\"frames\":20,\"bytes\":%d}\n",rate,total);fflush(stdout);
    }
    SilkSize size;SilkInit init;FARPROC function=GetProcAddress(module,"SKP_Silk_SDK_Get_Encoder_Size");memcpy(&size,&function,sizeof(size));
    function=GetProcAddress(module,"SKP_Silk_SDK_InitEncoder");memcpy(&init,&function,sizeof(init));SilkEncode silk=(SilkEncode)(base+p.silk_encode);
    assert(size && init);int bytes=0;assert(!size(&bytes) && bytes>0x5148 && bytes<1048576);
    void* state=calloc(1,bytes);int status[32]={0};assert(state && !init(state,status));int silk_rates[]={30000,100000,30000};
    for(int r=0;r<3;r++){
        int control[11]={16000,24000,320,silk_rates[r],0,2,0,0,0,0,0},total=0;
        for(int i=0;i<20;i++){short count=sizeof(output),extra=0;assert(!silk(state,control,pcm,320,output,&count,&extra));assert(count>0);total+=count;}
        assert(number(state,0x5144)==silk_rates[r] && number(state,0x3b64)==silk_rates[r]);
        printf("{\"codec\":\"SILK\",\"target\":%d,\"frames\":20,\"bytes\":%d}\n",silk_rates[r],total);fflush(stdout);
    }
    free(state);puts("PASS: updated real codecs encode synthetic mono PCM at 30/128/30 and 30/100/30 kbps");return 0;
}
