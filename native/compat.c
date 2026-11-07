/* Resolve compatible audio interfaces from read-only PE files. No version list,
   LoadLibrary, application-code execution, or persistent changes to WeChat. */
#include "wx.h"
#include "vendor/minhook/src/hde/hde64.h"
#include <string.h>
#include <stdlib.h>

typedef struct {
    const unsigned char* data; size_t size;
    const IMAGE_NT_HEADERS64* nt; const IMAGE_SECTION_HEADER* sections;
} Image;
static BOOL span(size_t size,size_t off,size_t n){return off<=size && n<=size-off;}
static uint32_t u32(const void* p){uint32_t v;memcpy(&v,p,4);return v;}
static uint64_t u64(const void* p){uint64_t v;memcpy(&v,p,8);return v;}
static BOOL image_open(Image* im,const void* data,size_t size){
    memset(im,0,sizeof(*im));if(!data || size<sizeof(IMAGE_DOS_HEADER))return FALSE;
    const IMAGE_DOS_HEADER* dos=data;
    if(dos->e_magic!=IMAGE_DOS_SIGNATURE || dos->e_lfanew<0 || !span(size,dos->e_lfanew,sizeof(IMAGE_NT_HEADERS64)))return FALSE;
    const IMAGE_NT_HEADERS64* nt=(const void*)((const unsigned char*)data+dos->e_lfanew);
    if(nt->Signature!=IMAGE_NT_SIGNATURE || nt->FileHeader.Machine!=IMAGE_FILE_MACHINE_AMD64 ||
       nt->OptionalHeader.Magic!=IMAGE_NT_OPTIONAL_HDR64_MAGIC ||
       nt->FileHeader.SizeOfOptionalHeader<sizeof(IMAGE_OPTIONAL_HEADER64) ||
       !nt->FileHeader.NumberOfSections || nt->FileHeader.NumberOfSections>96 ||
       nt->OptionalHeader.SizeOfImage>512*1024*1024 || nt->OptionalHeader.NumberOfRvaAndSizes<4)return FALSE;
    const IMAGE_SECTION_HEADER* s=IMAGE_FIRST_SECTION(nt);
    if(!span(size,(const unsigned char*)s-(const unsigned char*)data,nt->FileHeader.NumberOfSections*sizeof(*s)))return FALSE;
    for(unsigned i=0;i<nt->FileHeader.NumberOfSections;i++)
        if(!span(size,s[i].PointerToRawData,s[i].SizeOfRawData) ||
           !span(nt->OptionalHeader.SizeOfImage,s[i].VirtualAddress,s[i].SizeOfRawData) ||
           !span(nt->OptionalHeader.SizeOfImage,s[i].VirtualAddress,s[i].Misc.VirtualSize))return FALSE;
    im->data=data;im->size=size;im->nt=nt;im->sections=s;return TRUE;
}
static const IMAGE_SECTION_HEADER* section(const Image* im,uint32_t rva,size_t n){
    for(unsigned i=0;i<im->nt->FileHeader.NumberOfSections;i++){
        const IMAGE_SECTION_HEADER* s=im->sections+i;
        if(rva>=s->VirtualAddress && span(s->SizeOfRawData,rva-s->VirtualAddress,n))return s;
    }return NULL;
}
static const unsigned char* at(const Image* im,uint32_t rva,size_t n){
    const IMAGE_SECTION_HEADER* s=section(im,rva,n);
    return s?im->data+s->PointerToRawData+(rva-s->VirtualAddress):NULL;
}
static BOOL writable_address(const Image* im,uint32_t rva,size_t n){
    for(unsigned i=0;i<im->nt->FileHeader.NumberOfSections;i++){
        const IMAGE_SECTION_HEADER* s=im->sections+i;
        size_t size=s->Misc.VirtualSize>s->SizeOfRawData?s->Misc.VirtualSize:s->SizeOfRawData;
        if(rva>=s->VirtualAddress && span(size,rva-s->VirtualAddress,n))return (s->Characteristics&IMAGE_SCN_MEM_WRITE)!=0;
    }return FALSE;
}
static BOOL executable(const Image* im,uint32_t rva){
    const IMAGE_SECTION_HEADER* s=section(im,rva,1);return s && (s->Characteristics&IMAGE_SCN_MEM_EXECUTE);
}
static uint32_t absolute_rva(const Image* im,uint64_t va){
    uint64_t base=im->nt->OptionalHeader.ImageBase;
    return va>=base && va-base<im->nt->OptionalHeader.SizeOfImage?(uint32_t)(va-base):0;
}
static BOOL unique(uint32_t* result,uint32_t candidate){
    if(!candidate || (*result && *result!=candidate))return FALSE;*result=candidate;return TRUE;
}
static uint32_t vtable(const Image* im,const char* name){
    uint32_t td=0,col=0,vt=0;size_t len=strlen(name)+1;
    for(unsigned i=0;i<im->nt->FileHeader.NumberOfSections;i++){
        const IMAGE_SECTION_HEADER* s=im->sections+i;
        if(s->Characteristics&IMAGE_SCN_MEM_EXECUTE || s->SizeOfRawData<len+16)continue;
        const unsigned char* p=im->data+s->PointerToRawData;
        for(uint32_t j=16;j<=s->SizeOfRawData-len;j++)
            if(!memcmp(p+j,name,len) && !unique(&td,s->VirtualAddress+j-16))return 0;
    }if(!td)return 0;
    for(unsigned i=0;i<im->nt->FileHeader.NumberOfSections;i++){
        const IMAGE_SECTION_HEADER* s=im->sections+i;
        if(s->Characteristics&(IMAGE_SCN_MEM_EXECUTE|IMAGE_SCN_MEM_WRITE) || s->SizeOfRawData<24)continue;
        const unsigned char* p=im->data+s->PointerToRawData;
        for(uint32_t j=0;j<=s->SizeOfRawData-24;j+=4){
            uint32_t rva=s->VirtualAddress+j;
            if(u32(p+j)==1 && !u32(p+j+4) && !u32(p+j+8) && u32(p+j+12)==td &&
               u32(p+j+20)==rva && at(im,u32(p+j+16),16) && !unique(&col,rva))return 0;
        }
    }if(!col)return 0;
    for(unsigned i=0;i<im->nt->FileHeader.NumberOfSections;i++){
        const IMAGE_SECTION_HEADER* s=im->sections+i;
        if(s->Characteristics&(IMAGE_SCN_MEM_EXECUTE|IMAGE_SCN_MEM_WRITE) || s->SizeOfRawData<24)continue;
        const unsigned char* p=im->data+s->PointerToRawData;
        for(uint32_t j=0;j<=s->SizeOfRawData-24;j+=8)
            if(absolute_rva(im,u64(p+j))==col && executable(im,absolute_rva(im,u64(p+j+8))) &&
               !unique(&vt,s->VirtualAddress+j+8))return 0;
    }return vt;
}
static uint32_t method(const Image* im,uint32_t vt,unsigned slot){
    const unsigned char* p=at(im,vt+slot,8);uint32_t r=p?absolute_rva(im,u64(p)):0;
    return executable(im,r)?r:0;
}
static uint32_t exported(const Image* im,const char* name){
    IMAGE_DATA_DIRECTORY dir=im->nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT];
    const IMAGE_EXPORT_DIRECTORY* e=(const void*)at(im,dir.VirtualAddress,sizeof(*e));
    if(!e || e->NumberOfNames>100000 || e->NumberOfFunctions>100000)return 0;
    const unsigned char* names=at(im,e->AddressOfNames,e->NumberOfNames*4);
    const unsigned char* ords=at(im,e->AddressOfNameOrdinals,e->NumberOfNames*2);
    const unsigned char* funcs=at(im,e->AddressOfFunctions,e->NumberOfFunctions*4);
    if(!names || !ords || !funcs)return 0;
    for(uint32_t i=0;i<e->NumberOfNames;i++){
        const unsigned char* p=at(im,u32(names+i*4),strlen(name)+1);uint16_t ord;memcpy(&ord,ords+i*2,2);
        if(p && !memcmp(p,name,strlen(name)+1) && ord<e->NumberOfFunctions){
            uint32_t rva=u32(funcs+ord*4);return executable(im,rva)?rva:0;
        }
    }return 0;
}
static uint32_t primary_function(const Image* im,const unsigned char* runtime){
    // MSVC shrink-wrapping splits a function into chained .pdata records.
    // https://learn.microsoft.com/en-us/cpp/build/exception-handling-x64
    for(int depth=0;depth<16;depth++){
        uint32_t unwind=u32(runtime+8);const unsigned char* h=at(im,unwind,4);if(!h)return 0;
        if(!(h[0]&7) || (h[0]&7)>2)return 0;
        if(!(h[0]&(4<<3)))return u32(runtime);
        if(h[0]&((1|2)<<3))return 0;
        runtime=at(im,unwind+4+(((uint32_t)h[2]+1)&~1U)*2,12);if(!runtime)return 0;
    }return 0;
}
static uint32_t function_end(const Image* im,uint32_t entry){
    IMAGE_DATA_DIRECTORY dir=im->nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXCEPTION];
    const unsigned char* p=at(im,dir.VirtualAddress,dir.Size);if(!p || dir.Size%12)return 0;
    for(uint32_t i=0;i<dir.Size;i+=12)if(u32(p+i)==entry){
        uint32_t end=u32(p+i+4);
        for(uint32_t j=i+12;j<dir.Size;j+=12){
            if(u32(p+j)!=end || primary_function(im,p+j)!=entry)break;
            uint32_t next=u32(p+j+4);if(next<=end)return 0;end=next;
        }
        return end>entry && end-entry<65536 && at(im,entry,end-entry)?end:0;
    }return 0;
}
static unsigned decode(const Image* im,uint32_t rva,uint32_t end,hde64s* d){
    unsigned char code[32]={0};size_t n=end-rva;if(n>16)n=16;
    const unsigned char* p=at(im,rva,n);if(!p)return 0;memcpy(code,p,n);
    unsigned len=hde64_disasm(code,d);return !(d->flags&F_ERROR) && len<=n?len:0;
}
static uint32_t service_global(const Image* im,uint32_t vt){
    uint32_t start=exported(im,"GetWXConfService"),end=function_end(im,start),stored=0,loaded=0;BOOL type_seen=FALSE;
    if(!start || !end)return 0;
    for(uint32_t rva=start;rva<end;){
        hde64s d;unsigned n=decode(im,rva,end,&d);if(!n)return 0;
        if(d.rex_w && d.modrm_mod==0 && d.modrm_rm==5 && (d.flags&F_DISP32)){
            uint32_t target=rva+n+(int32_t)d.disp.disp32;
            if(d.opcode==0x8d && target==vt)type_seen=TRUE;
            if(writable_address(im,target,8) && !d.rex_r && d.modrm_reg==0){
                if(d.opcode==0x89 && !unique(&stored,target))return 0;
                if(d.opcode==0x8b && !unique(&loaded,target))return 0;
            }
        }rva+=n;
    }return type_seen && stored && stored==loaded?stored:0;
}
static BOOL prefix(const Image* im,uint32_t rva,const unsigned char* bytes,size_t n){
    const unsigned char* p=at(im,rva,n);return p && executable(im,rva) && !memcmp(p,bytes,n);
}
static uint32_t called_prefix(const Image* im,uint32_t entry,const unsigned char* bytes,size_t size){
    uint32_t end=function_end(im,entry),result=0;if(!entry || !end)return 0;
    for(uint32_t rva=entry;rva<end;){
        hde64s d;unsigned n=decode(im,rva,end,&d);if(!n)return 0;
        if(d.opcode==0xe8 && d.len==5){
            uint32_t target=rva+5+(int32_t)d.imm.imm32;
            if(prefix(im,target,bytes,size) && !unique(&result,target))return 0;
        }rva+=n;
    }return result;
}
static uint32_t string_rva(const Image* im,const char* text){
    uint32_t result=0;size_t n=strlen(text)+1;
    for(unsigned i=0;i<im->nt->FileHeader.NumberOfSections;i++){
        const IMAGE_SECTION_HEADER* s=im->sections+i;
        if(s->Characteristics&IMAGE_SCN_MEM_EXECUTE || s->SizeOfRawData<n)continue;
        const unsigned char* p=im->data+s->PointerToRawData;
        for(uint32_t j=0;j<=s->SizeOfRawData-n;j++)if(!memcmp(p+j,text,n) && !unique(&result,s->VirtualAddress+j))return 0;
    }return result;
}
static uint32_t function_owner(const Image* im,uint32_t rva){
    IMAGE_DATA_DIRECTORY dir=im->nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXCEPTION];
    const unsigned char* p=at(im,dir.VirtualAddress,dir.Size);if(!p || dir.Size%12)return 0;
    for(uint32_t i=0;i<dir.Size;i+=12)if(u32(p+i)<=rva && rva<u32(p+i+4))return primary_function(im,p+i);
    return 0;
}
static BOOL scan_members(const Image* im,uint32_t entry,WxProfile* p){
    uint32_t end=function_end(im,entry);if(!end)return FALSE;
    unsigned char* seen=calloc(end-entry,1);if(!seen)return FALSE;
    uint32_t work[2048]={entry},pending=1;BOOL ok=TRUE;
    while(pending && ok){
        uint32_t rva=work[--pending];
        while(rva>=entry && rva<end && !seen[rva-entry]){
            seen[rva-entry]=1;hde64s d;unsigned n=decode(im,rva,end,&d);if(!n){ok=FALSE;break;}
            if(d.rex_w && (d.opcode==0x8b || d.opcode==0x89) && d.modrm_mod==2 &&
               d.modrm_rm!=4 && (d.flags&F_DISP32) && d.disp.disp32>=0x100 && d.disp.disp32<=0x8000){
                uint32_t field=d.disp.disp32;BOOL duplicate=FALSE;
                for(uint32_t k=0;k<p->coder_count;k++)if(p->coder_members[k]==field)duplicate=TRUE;
                if(!duplicate){if(p->coder_count==32){ok=FALSE;break;}p->coder_members[p->coder_count++]=field;}
            }
            BOOL jump=d.opcode==0xe9 || d.opcode==0xeb;
            BOOL conditional=(d.opcode>=0x70 && d.opcode<=0x7f) || (d.opcode>=0xe0 && d.opcode<=0xe3) ||
                (d.opcode==0x0f && d.opcode2>=0x80 && d.opcode2<=0x8f);
            if(jump || conditional){
                int32_t rel=(d.flags&F_IMM8)?(int8_t)d.imm.imm8:(int32_t)d.imm.imm32;
                uint32_t target=rva+n+rel;
                if(target>=entry && target<end && !seen[target-entry]){
                    if(pending==2048){ok=FALSE;break;}work[pending++]=target;
                }
            }
            if(jump || d.opcode==0xc3 || d.opcode==0xc2 || d.opcode==0xcc ||
               (d.opcode==0x0f && d.opcode2==0x0b) || (d.opcode==0xff && (d.modrm_reg==4 || d.modrm_reg==5)))break;
            rva+=n;
        }
    }
    free(seen);return ok;
}
static BOOL member_candidates(const Image* im,WxProfile* p){
    uint32_t text=string_rva(im,"XVERecChannel::SetSendCodec");if(!text)return FALSE;
    uint32_t functions[16],count=0;
    for(unsigned i=0;i<im->nt->FileHeader.NumberOfSections;i++){
        const IMAGE_SECTION_HEADER* s=im->sections+i;
        if(!(s->Characteristics&IMAGE_SCN_MEM_EXECUTE) || s->SizeOfRawData<7)continue;
        const unsigned char* code=im->data+s->PointerToRawData;
        for(uint32_t j=0;j<=s->SizeOfRawData-7;j++){
            if((code[j]&0xf8)!=0x48 || code[j+1]!=0x8d || (code[j+2]&0xc7)!=5)continue;
            uint32_t rva=s->VirtualAddress+j,target=rva+7+(int32_t)u32(code+j+3);
            if(target!=text)continue;uint32_t entry=function_owner(im,rva);if(!entry)continue;
            BOOL seen=FALSE;for(uint32_t k=0;k<count;k++)if(functions[k]==entry)seen=TRUE;
            if(!seen){if(count==16)return FALSE;functions[count++]=entry;}
        }
    }
    for(uint32_t i=0;i<count;i++)if(!scan_members(im,functions[i],p))return FALSE;
    return p->coder_count!=0;
}
static BOOL silk_extra_result(const Image* im,uint32_t entry){
    static const unsigned char bytes[]={0x48,0x8b,0x84,0x24,0xb0,0,0,0,0x0f,0xb7,0x8b,0xd4,0x59,0,0,0x66,0x89,0x08};
    uint32_t end=function_end(im,entry);if(!end)return FALSE;
    for(uint32_t rva=entry;rva<end;){
        if(end-rva>=sizeof(bytes) && prefix(im,rva,bytes,sizeof(bytes)))return TRUE;
        hde64s d;unsigned n=decode(im,rva,end,&d);if(!n)return FALSE;rva+=n;
    }return FALSE;
}
BOOL wx_resolve_images(const void* voice,size_t voice_n,const void* conf,size_t conf_n,WxProfile* out){
    memset(out,0,sizeof(*out));Image v,c;if(!image_open(&v,voice,voice_n) || !image_open(&c,conf,conf_n))return FALSE;
    WxProfile p={0};
    p.service_vt=vtable(&c,".?AVWXConfService@wxvoipsdk@@");
    p.backend_vt=vtable(&c,".?AVConfCallMgr@wxvoipsdk@@");
    p.engine_vt=vtable(&v,".?AVCMVQQEngine@@");p.xve_vt=vtable(&v,".?AVCXVoiceEngine@@");
    p.opus_vt=vtable(&v,".?AVCOpusCoder@@");p.silk_vt=vtable(&v,".?AVCSILKCoder@@");
    p.channel_vt=vtable(&v,".?AVXVERecChannel@@");
    if(!p.service_vt || !p.backend_vt || !p.engine_vt || !p.xve_vt || !p.opus_vt || !p.silk_vt || !p.channel_vt)return FALSE;
    p.service=service_global(&c,p.service_vt);
    static const unsigned char opus[]={0x48,0x8b,0xc4,0x89,0x50,0x10,0x4c,0x89,0x40,0x18,0x4c,0x89,0x48,0x20,0x53,0x55,0x41,0x56,0x48,0x81,0xec,0xa0,0,0};
    static const unsigned char silk[]={0x40,0x53,0x55,0x56,0x57,0x41,0x54,0x48,0x83,0xec,0x50,0x8b,0x2a,0x4d,0x8b,0xe0,0x41,0x8b,0xf1,0x4c,0x8b,0xc2,0x48,0x8b};
    static const unsigned char op_set[]={0x89,0x51,0x24,0x33,0xc0,0xc3};
    static const unsigned char si_set[]={0x8b,0x44,0x24,0x28,0x89,0x41,0x28,0x8b,0x44,0x24,0x30,0x89,0x41,0x30,0x33,0xc0,0x89,0x51,0x24,0x89,0x51,0x7c,0xc3};
    if(!prefix(&v,method(&v,p.opus_vt,0x28),op_set,sizeof(op_set)) ||
       !prefix(&v,method(&v,p.silk_vt,0x28),si_set,sizeof(si_set)))return FALSE;
    p.opus_ctl=called_prefix(&v,method(&v,p.opus_vt,0),opus,sizeof(opus));
    // The exported SILK function has the same prefix but a different ABI.
    // Follow the actual C++ encode method, never choose the first byte match.
    p.silk_encode=called_prefix(&v,method(&v,p.silk_vt,8),silk,sizeof(silk));
    if(!p.service || !p.opus_ctl || !p.silk_encode || !silk_extra_result(&v,p.silk_encode) || !member_candidates(&v,&p))return FALSE;
    p.voice_stamp=v.nt->FileHeader.TimeDateStamp;p.voice_size=v.nt->OptionalHeader.SizeOfImage;
    p.conf_stamp=c.nt->FileHeader.TimeDateStamp;p.conf_size=c.nt->OptionalHeader.SizeOfImage;
    *out=p;return TRUE;
}
typedef struct {HANDLE file,mapping;const void* data;size_t size;} Mapped;
static void close_map(Mapped* m){if(m->data)UnmapViewOfFile(m->data);if(m->mapping)CloseHandle(m->mapping);if(m->file && m->file!=INVALID_HANDLE_VALUE)CloseHandle(m->file);}
static BOOL map_file(const wchar_t* path,Mapped* m){
    memset(m,0,sizeof(*m));m->file=CreateFileW(path,GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_DELETE,NULL,OPEN_EXISTING,0,NULL);
    LARGE_INTEGER n;if(m->file==INVALID_HANDLE_VALUE || !GetFileSizeEx(m->file,&n) || n.QuadPart<64 || n.QuadPart>512*1024*1024)return FALSE;
    m->size=(size_t)n.QuadPart;m->mapping=CreateFileMappingW(m->file,NULL,PAGE_READONLY,0,0,NULL);
    if(m->mapping)m->data=MapViewOfFile(m->mapping,FILE_MAP_READ,0,0,0);return m->data!=NULL;
}
BOOL wx_resolve(const wchar_t* voice,const wchar_t* conf,WxProfile* out){
    static wchar_t paths[2][MAX_PATH];static WIN32_FILE_ATTRIBUTE_DATA saved[2];static WxProfile cached;static BOOL valid=FALSE;
    WIN32_FILE_ATTRIBUTE_DATA info[2];memset(info,0,sizeof(info));memset(out,0,sizeof(*out));
    if(!GetFileAttributesExW(voice,GetFileExInfoStandard,&info[0]) || !GetFileAttributesExW(conf,GetFileExInfoStandard,&info[1]))return FALSE;
    if(valid && !_wcsicmp(paths[0],voice) && !_wcsicmp(paths[1],conf) && !memcmp(info,saved,sizeof(info))){*out=cached;return TRUE;}
    Mapped v={0},c={0};BOOL ok=FALSE;
    if(map_file(voice,&v) && map_file(conf,&c))ok=wx_resolve_images(v.data,v.size,c.data,c.size,out);
    close_map(&v);close_map(&c);
    if(ok){wcscpy(paths[0],voice);wcscpy(paths[1],conf);memcpy(saved,info,sizeof(info));cached=*out;valid=TRUE;}
    return ok;
}
static BOOL loaded_image(const WxContext* c,uint64_t base,uint32_t stamp,uint32_t size){
    IMAGE_DOS_HEADER dos;IMAGE_NT_HEADERS64 nt;
    return wx_read(c->process,base,&dos,sizeof(dos)) && dos.e_magic==IMAGE_DOS_SIGNATURE && dos.e_lfanew>=0 && dos.e_lfanew<65536 &&
        wx_read(c->process,base+dos.e_lfanew,&nt,sizeof(nt)) && nt.Signature==IMAGE_NT_SIGNATURE &&
        nt.FileHeader.Machine==IMAGE_FILE_MACHINE_AMD64 && nt.OptionalHeader.Magic==IMAGE_NT_OPTIONAL_HDR64_MAGIC &&
        nt.FileHeader.TimeDateStamp==stamp && nt.OptionalHeader.SizeOfImage==size;
}
BOOL wx_profile_loaded(const WxContext* c){
    return loaded_image(c,c->voice,c->profile.voice_stamp,c->profile.voice_size) &&
        loaded_image(c,c->conf,c->profile.conf_stamp,c->profile.conf_size);
}
