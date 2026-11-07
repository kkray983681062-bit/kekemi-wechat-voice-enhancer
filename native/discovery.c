#include "wx.h"
#include <string.h>

static uint64_t ptr(const WxContext* c,uint64_t at){uint64_t value=0;wx_read(c->process,at,&value,8);return value;}
static int num(const WxContext* c,uint64_t at){int value=-1;wx_read(c->process,at,&value,4);return value;}

/* Return the complete object, including when the application holds a secondary
   C++ interface. The COL must belong to this module and the complete object
   must have the independently resolved primary vtable of the same type. */
static uint64_t object(const WxContext* c,uint64_t raw,BOOL conf,uint32_t primary,uint32_t* adjustment){
    uint64_t base=conf?c->conf:c->voice,size=conf?c->profile.conf_size:c->profile.voice_size;
    uint64_t vt=ptr(c,raw);*adjustment=0;
    if(!raw || !primary)return 0;
    if(vt==base+primary)return raw;
    if(vt<base+8 || vt-base>=size)return 0;
    uint64_t col=ptr(c,vt-8),expected=ptr(c,base+primary-8);uint32_t a[6],b[6];
    if(col<base || col-base+sizeof(a)>size || expected<base || expected-base+sizeof(b)>size ||
       !wx_read(c->process,col,a,sizeof(a)) || !wx_read(c->process,expected,b,sizeof(b)))return 0;
    if(a[0]!=1 || a[2] || a[1]>65536 || a[3]!=b[3] || a[5]!=col-base ||
       b[0]!=1 || b[1] || b[5]!=expected-base || raw<a[1]+0x10000ULL)return 0;
    uint64_t complete=raw-a[1];if(ptr(c,complete)!=base+primary)return 0;
    *adjustment=a[1];return complete;
}

static int child(const WxContext* c,WxCall* x,int index,BOOL conf,uint32_t primary,uint32_t bound){
    uint32_t adjust=0,hint=c->hint.member[index];uint64_t parent=x->p[index-1];
    if(hint && hint<bound){
        uint64_t raw=ptr(c,parent+hint),value=object(c,raw,conf,primary,&adjust);
        if(value){x->p[index]=value;x->member[index]=hint;x->adjustment[index]=adjust;return WX_OK;}
        if(!raw)return WX_NO_CALL;
    }
    uint64_t found=0;
    for(uint32_t off=8;off<bound;off+=8){
        uint64_t value=object(c,ptr(c,parent+off),conf,primary,&adjust);if(!value)continue;
        if(found && found!=value)return WX_MULTIPLE;
        if(!found){found=value;x->p[index]=value;x->member[index]=off;x->adjustment[index]=adjust;}
    }return found?WX_OK:WX_NO_CALL;
}

static BOOL channel_table(const WxContext* c,uint64_t owner,uint32_t off,uint64_t* table,int* count){
    *table=ptr(c,owner+off);*count=num(c,owner+off+8);
    if(!*table || *count<1 || *count>64)return FALSE;
    for(int i=0;i<*count;i++){
        uint32_t adjust;uint64_t p=ptr(c,*table+i*0x58);
        if(object(c,p,FALSE,c->profile.channel_vt,&adjust))return TRUE;
    }return FALSE;
}

int wx_check(const WxContext* c,const WxCall* x){
    if(!x->p[8] || x->stride!=0x58 || x->index<0)return WX_CHANGED;
    if(ptr(c,c->conf+c->profile.service)!=x->p[0]+x->adjustment[0])return WX_CHANGED;
    for(int i=1;i<9;i++){
        uint64_t at=i==5?x->p[4]+(uint64_t)x->index*x->stride:x->p[i-1]+x->member[i];
        if(ptr(c,at)!=x->p[i]+x->adjustment[i])return WX_CHANGED;
    }
    const uint32_t tables[]={c->profile.service_vt,c->profile.backend_vt,c->profile.engine_vt,c->profile.xve_vt,0,c->profile.channel_vt,0,
        x->codec==WX_SILK?c->profile.silk_vt:c->profile.opus_vt};
    for(int i=0;i<8;i++)if(tables[i] && ptr(c,x->p[i])!=(i<2?c->conf:c->voice)+tables[i])return WX_CHANGED;
    if(num(c,x->p[7]+8)!=x->codec || num(c,x->p[5]+x->member[6]+0x10)!=x->codec || num(c,x->p[5]+0x10)!=x->channel)return WX_CHANGED;
    int count=num(c,x->p[3]+x->count_member);
    if(count<=x->index || count>64)return WX_CHANGED;
    // Check other send slots without rediscovering/scanning parent objects on
    // the audio thread. A second active encoder must not be silently selected.
    for(int i=0;i<count;i++)if(i!=x->index){
        uint32_t adjust;uint64_t ch=object(c,ptr(c,x->p[4]+(uint64_t)i*x->stride),FALSE,c->profile.channel_vt,&adjust);
        if(!ch)continue;uint64_t codec=ptr(c,ptr(c,ch+x->member[6]));uint64_t vt=ptr(c,codec);
        if((vt==c->voice+c->profile.opus_vt || vt==c->voice+c->profile.silk_vt) && ptr(c,codec+0x10))return WX_MULTIPLE;
    }return WX_OK;
}

int wx_locate(WxContext* c,WxCall* out){
    WxCall x={0};const WxProfile* p=&c->profile;int rc;
    uint64_t raw=ptr(c,c->conf+p->service);if(!raw)return WX_NO_CALL;
    x.p[0]=object(c,raw,TRUE,p->service_vt,&x.adjustment[0]);if(!x.p[0])return WX_UNSUPPORTED;
    if((rc=child(c,&x,1,TRUE,p->backend_vt,0x68)) ||
       (rc=child(c,&x,2,FALSE,p->engine_vt,0xa00)) ||
       (rc=child(c,&x,3,FALSE,p->xve_vt,0x400)))return rc;
    int count=0;uint64_t table=0;uint32_t hint=c->hint.member[4];
    if(hint && hint<0x80 && channel_table(c,x.p[3],hint,&table,&count))x.member[4]=hint;
    else {
        table=0;count=0;
        for(uint32_t off=8;off<0x80;off+=8){
            uint64_t t;int n;if(!channel_table(c,x.p[3],off,&t,&n))continue;
            if(table && table!=t)return WX_MULTIPLE;
            if(!table){table=t;count=n;x.member[4]=off;}
        }
    }
    if(!table)return WX_NO_CALL;
    x.p[4]=table;x.count_member=x.member[4]+8;x.stride=0x58;int found=0;
    for(int i=0;i<count;i++){
        WxCall candidate=x;candidate.index=i;
        candidate.p[5]=object(c,ptr(c,table+(uint64_t)i*x.stride),FALSE,p->channel_vt,&candidate.adjustment[5]);
        if(!candidate.p[5])continue;
        candidate.channel=num(c,candidate.p[5]+0x10);if(candidate.channel<0)continue;
        uint64_t matched=0;
        for(uint32_t j=0;j<p->coder_count && j<32;j++){
            uint32_t off=p->coder_members[j],adjust;uint64_t holder=ptr(c,candidate.p[5]+off),codec_raw=ptr(c,holder);
            uint64_t codec=object(c,codec_raw,FALSE,p->opus_vt,&adjust);int id=0;
            if(codec){id=num(c,codec+8);if(id!=WX_OPUS && id!=WX_OPUS16)return WX_CODEC;}
            else {codec=object(c,codec_raw,FALSE,p->silk_vt,&adjust);if(codec)id=WX_SILK;}
            if(!codec)continue;
            if(matched && (candidate.p[7]!=codec || candidate.member[6]!=off))return WX_MULTIPLE;
            if(num(c,codec+8)!=id || num(c,candidate.p[5]+off+0x10)!=id)return WX_CODEC;
            candidate.codec=id;candidate.p[6]=holder;candidate.member[6]=off;
            candidate.p[7]=codec;candidate.adjustment[7]=adjust;candidate.member[8]=0x10;
            candidate.p[8]=ptr(c,codec+0x10);matched=codec;
        }
        if(!matched || !candidate.p[8])continue;
        if(found++)return WX_MULTIPLE;*out=candidate;
    }
    if(!found)return WX_NO_CALL;
    rc=wx_check(c,out);if(!rc)c->hint=*out;return rc;
}
