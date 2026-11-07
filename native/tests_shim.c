#include <stdint.h>
#include <stdio.h>
#include <assert.h>
static uint64_t seen[8];
static uint64_t original(uint64_t a,uint64_t b,uint64_t c,uint64_t d,uint64_t e,uint64_t f,uint64_t g,uint64_t h){
    uint64_t args[]={a,b,c,d,e,f,g,h};for(int i=0;i<8;i++)seen[i]=args[i];return UINT64_C(0x123456789abcdef0);
}
void* wx_opus_original=original;
extern uint64_t wx_opus_detour(uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t);
uintptr_t wx_adjust_opus(void* state,uintptr_t request,uintptr_t value,uintptr_t fourth){
    (void)fourth;
    __asm__ volatile("pxor %%xmm0,%%xmm0;pxor %%xmm1,%%xmm1;pxor %%xmm2,%%xmm2;pxor %%xmm3,%%xmm3;pxor %%xmm4,%%xmm4;pxor %%xmm5,%%xmm5":::"xmm0","xmm1","xmm2","xmm3","xmm4","xmm5");
    return (uintptr_t)state==0x1234 && request==4002?128000:value;
}
static double fp_original(void* state,int command,double value,double fourth,uint64_t fifth){
    assert((uintptr_t)state==0x1234 && command==555 && fifth==9876);return value+fourth;
}
int main(void){
    uint64_t result=wx_opus_detour(0x1234,4002,35000,44,55,66,77,88);
    assert(result==UINT64_C(0x123456789abcdef0));assert(seen[0]==0x1234 && seen[1]==4002 && seen[2]==128000);
    for(int i=3;i<8;i++)assert(seen[i]==(uint64_t)(i+1)*11);
    wx_opus_detour(0x1234,555,0xabcdef,44,55,66,77,88);assert(seen[2]==0xabcdef && seen[7]==88);
    wx_opus_original=fp_original;
    double (*fp)(void*,int,double,double,uint64_t)=(void*)wx_opus_detour;
    assert(fp((void*)0x1234,555,2.25,8.5,9876)==10.75);
    puts("PASS: Opus parameter override, untouched commands, stack arguments, floating-point registers and return value");return 0;
}
