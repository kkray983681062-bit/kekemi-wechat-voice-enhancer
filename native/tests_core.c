#include "wx.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static void p(void* b,unsigned o,uint64_t v){memcpy((char*)b+o,&v,8);}
static void n(void* b,unsigned o,int v){memcpy((char*)b+o,&v,4);}
static void run_layout(unsigned adjust,unsigned engine_member,unsigned backend_member,unsigned table_member,unsigned coder_member){
    void* conf=calloc(1,0x10000);void* voice=calloc(1,0x10000);
    void* s=calloc(1,0x68);void* b=calloc(1,0xa00);void* e=calloc(1,0x400);void* x=calloc(1,0x80);
    void* t=calloc(1,0xb0);void* ch=calloc(1,0x8000);void* holder=calloc(1,16);void* codec=calloc(1,0xa0);void* st=calloc(1,0x6000);
    WxProfile profile={.service=0x100,.service_vt=0x1000,.backend_vt=0x1100,.engine_vt=0x2000,.xve_vt=0x3000,.channel_vt=0x4000,
        .opus_vt=0x5000,.silk_vt=0x6000,.voice_size=0x10000,.conf_size=0x10000,.coder_count=2,.coder_members={0x5100,coder_member}};
    WxContext c={.process=GetCurrentProcess(),.conf=(uintptr_t)conf,.voice=(uintptr_t)voice,.profile=profile};
    p(conf,0x100,(uintptr_t)s);p(s,0,c.conf+0x1000);p(s,8,(uintptr_t)b);p(b,0,c.conf+0x1100);p(b,backend_member,(uintptr_t)e+adjust);
    p(e,0,c.voice+0x2000);
    if(adjust){
        p(e,adjust,c.voice+0x2100);p(voice,0x1ff8,c.voice+0x7000);p(voice,0x20f8,c.voice+0x7100);
        n(voice,0x7000,1);n(voice,0x700c,0x7800);n(voice,0x7014,0x7000);
        n(voice,0x7100,1);n(voice,0x7104,adjust);n(voice,0x710c,0x7800);n(voice,0x7114,0x7100);
    }
    p(e,engine_member,(uintptr_t)x);p(x,0,c.voice+0x3000);p(x,table_member,(uintptr_t)t);n(x,table_member+8,1);
    p(t,0,(uintptr_t)ch);p(ch,0,c.voice+0x4000);n(ch,0x10,7);p(ch,coder_member,(uintptr_t)holder);p(holder,0,(uintptr_t)codec);
    p(codec,0,c.voice+0x6000);n(codec,8,4);p(codec,0x10,(uintptr_t)st);n(ch,coder_member+0x10,4);
    n(codec,0x18,16000);n(codec,0x70,16000);n(codec,0x20,320);n(codec,0x74,320);n(codec,0x78,1);n(codec,0x7c,30000);n(st,0x5144,100000);n(st,0x3b64,100000);
    WxCall call={0};WxValues a={0};assert(!wx_locate(&c,&call));assert(!wx_values(&c,&call,&a));
    assert(call.p[2]==(uintptr_t)e && call.adjustment[2]==adjust && call.member[3]==engine_member && call.member[6]==coder_member);
    assert(a.target==100000 && a.normal==30000 && a.maximum==100000);
    // A stale cached table candidate must not become an apparent second table.
    c.hint.member[4]=0x10;p(x,0x10,0x12345678);n(x,0x18,4);
    assert(!wx_locate(&c,&call) && call.member[4]==table_member);
    p(x,0x10,0);n(x,0x18,0);
    p(b,backend_member,0);assert(wx_locate(&c,&call)==WX_NO_CALL);assert(wx_check(&c,&c.hint)==WX_CHANGED);p(b,backend_member,(uintptr_t)e+adjust);
    assert(!wx_locate(&c,&call));p(e,engine_member,0);assert(wx_locate(&c,&call)==WX_NO_CALL);p(e,engine_member,(uintptr_t)x);
    assert(!wx_locate(&c,&call));n(codec,0x78,2);assert(wx_values(&c,&call,&a)==WX_CODEC);n(codec,0x78,1);
    WxCall previous=call;
    p(codec,0,c.voice+0x5000);n(codec,8,17);n(ch,coder_member+0x10,17);n(codec,0x24,30000);n(st,0xa4,128000);n(st,0xa0,128000);n(st,0x90,16000);n(st,0x70,1);
    assert(wx_check(&c,&previous)==WX_CHANGED);assert(!wx_locate(&c,&call));assert(!wx_values(&c,&call,&a));assert(a.target==128000 && a.maximum==128000);
    n(codec,8,18);n(ch,coder_member+0x10,18);assert(wx_locate(&c,&call)==WX_CODEC);n(codec,8,17);n(ch,coder_member+0x10,17);
    n(x,table_member+8,2);p(t,0x58,(uintptr_t)ch);assert(wx_locate(&c,&call)==WX_MULTIPLE);assert(wx_check(&c,&c.hint)==WX_MULTIPLE);
    p(t,0,0);assert(!wx_locate(&c,&call) && call.index==1);assert(!wx_values(&c,&call,&a));
    p(conf,0x100,0);assert(wx_locate(&c,&call)==WX_NO_CALL);assert(wx_check(&c,&call)==WX_CHANGED);
    free(conf);free(voice);free(s);free(b);free(e);free(x);free(t);free(ch);free(holder);free(codec);free(st);
}
int main(void){
    run_layout(0,0x210,0x980,0x20,0x5230);
    run_layout(8,0x240,0x980,0x20,0x5230);
    run_layout(16,0x2b0,0x600,0x40,0x5400);
    puts("PASS: old/new/moved member layouts, secondary interfaces, both codecs, fast identity checks, idle transitions, nonzero channel slot, multiple calls");return 0;
}
