#include "wx.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned char* read_file(const wchar_t* name,size_t* n){
    FILE* f=_wfopen(name,L"rb");assert(f);fseek(f,0,SEEK_END);*n=ftell(f);rewind(f);
    unsigned char* p=malloc(*n);assert(p && fread(p,1,*n,f)==*n);fclose(f);return p;
}
static IMAGE_NT_HEADERS64* nt(unsigned char* p){return (void*)(p+((IMAGE_DOS_HEADER*)p)->e_lfanew);}
static unsigned char* rva(unsigned char* p,DWORD addr){
    IMAGE_NT_HEADERS64* h=nt(p);IMAGE_SECTION_HEADER* s=IMAGE_FIRST_SECTION(h);
    for(unsigned i=0;i<h->FileHeader.NumberOfSections;i++)if(addr>=s[i].VirtualAddress && addr-s[i].VirtualAddress<s[i].SizeOfRawData)
        return p+s[i].PointerToRawData+addr-s[i].VirtualAddress;
    assert(0);return NULL;
}
static void denied(unsigned char* v,size_t vn,unsigned char* c,size_t cn){
    WxProfile p;memset(&p,0xaa,sizeof(p));assert(!wx_resolve_images(v,vn,c,cn,&p));
    WxProfile zero={0};assert(!memcmp(&p,&zero,sizeof(p)));
}
int wmain(int argc,wchar_t** argv){
    assert(argc==3);size_t vn,cn;unsigned char* v=read_file(argv[1],&vn),*c=read_file(argv[2],&cn);WxProfile p={0},q={0};
    assert(wx_resolve_images(v,vn,c,cn,&p));
    printf("{\"service\":%u,\"backend\":%u,\"engine\":%u,\"xve\":%u,\"opus_vt\":%u,\"silk_vt\":%u,\"opus_ctl\":%u,\"silk_encode\":%u}\n",p.service,p.backend_vt,p.engine_vt,p.xve_vt,p.opus_vt,p.silk_vt,p.opus_ctl,p.silk_encode);
    // Independently disassembled 4.2.0.13: distinguish the seven-argument SILK
    // call from the similarly prefixed six-argument exported SDK function.
    assert(p.service==0x131fc8 && p.service_vt==0xebc58 && p.backend_vt==0xf8ac0);
    assert(p.engine_vt==0xf8ab38 && p.xve_vt==0xfb0898 && p.opus_vt==0xfd3398 && p.silk_vt==0xfd2f58);
    assert(p.opus_ctl==0x51d020 && p.silk_encode==0x4e23e0 && p.silk_encode!=0x4e2080);
    assert(p.channel_vt==0xfcab90);int coder_field=0;for(unsigned i=0;i<p.coder_count;i++)if(p.coder_members[i]==0x5230)coder_field=1;assert(coder_field);
    // Unrelated binary changes must no longer invalidate compatible interfaces.
    v[0x40]^=1;assert(wx_resolve_images(v,vn,c,cn,&q) && !memcmp(&p,&q,sizeof(p)));v[0x40]^=1;
    nt(v)->FileHeader.TimeDateStamp++;assert(wx_resolve_images(v,vn,c,cn,&q) && q.opus_ctl==p.opus_ctl);nt(v)->FileHeader.TimeDateStamp--;
    denied(v,64,c,cn);WORD machine=nt(v)->FileHeader.Machine;nt(v)->FileHeader.Machine=IMAGE_FILE_MACHINE_I386;denied(v,vn,c,cn);nt(v)->FileHeader.Machine=machine;
    unsigned char* ctl=rva(v,p.opus_ctl);*ctl^=1;denied(v,vn,c,cn);*ctl^=1;
    unsigned char* sil=rva(v,p.silk_encode);*sil^=1;denied(v,vn,c,cn);*sil^=1;
    unsigned char* extra=rva(v,0x4e2716);*extra^=1;denied(v,vn,c,cn);*extra^=1;
    // A wrapper with changed field layout must fail before an instance is used.
    uint64_t method;memcpy(&method,rva(v,p.opus_vt+0x28),8);
    unsigned char* setter=rva(v,(DWORD)(method-nt(v)->OptionalHeader.ImageBase));setter[2]^=1;denied(v,vn,c,cn);setter[2]^=1;
    // Missing RTTI is not treated as an idle call or silently guessed.
    const char name[]=".?AVCOpusCoder@@";unsigned char* type=NULL;
    for(size_t i=0;i+sizeof(name)<=vn;i++)if(!memcmp(v+i,name,sizeof(name))){type=v+i;break;}
    assert(type);*type='!';denied(v,vn,c,cn);*type='.';
    // Ambiguous primary vtables must not be selected by scan order.
    IMAGE_SECTION_HEADER* sections=IMAGE_FIRST_SECTION(nt(v));unsigned char* gap=NULL;
    for(unsigned i=0;i<nt(v)->FileHeader.NumberOfSections && !gap;i++){
        IMAGE_SECTION_HEADER* s=sections+i;if(s->Characteristics&(IMAGE_SCN_MEM_EXECUTE|IMAGE_SCN_MEM_WRITE))continue;
        for(DWORD j=0;j+32<=s->SizeOfRawData;j+=8){unsigned char zeros[32]={0};unsigned char* here=v+s->PointerToRawData+j;
            if(!memcmp(here,zeros,32)){gap=here;break;}}
    }
    assert(gap);memcpy(gap,rva(v,p.opus_vt-8),24);denied(v,vn,c,cn);memset(gap,0,24);
    assert(wx_resolve_images(v,vn,c,cn,&q) && !memcmp(&p,&q,sizeof(p)));
    free(v);free(c);puts("PASS: actual relocated interfaces, unrelated updates, malformed images, unknown/ambiguous types, wrapper ABI, and correct SILK target");return 0;
}
