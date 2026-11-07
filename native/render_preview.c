/* Render the production UI with example data. No WeChat process is accessed. */
#define wWinMain production_wWinMain
#include "ui.c"
#undef wWinMain
#include <assert.h>
int main(int argc,char** argv){
    assert(argc==2);SetProcessDPIAware();
    const int width=876,height=1185;
    window=CreateWindowExW(0,L"STATIC",L"Preview",WS_POPUP,0,0,width,height,NULL,NULL,GetModuleHandleW(NULL),NULL);assert(window);
    scale=1.5;extra_width=extra_height=0;
    avatar=LoadImageW(GetModuleHandleW(NULL),MAKEINTRESOURCEW(101),IMAGE_ICON,48,48,LR_DEFAULTCOLOR);assert(avatar);
    WxInstance rows[2]={{.pid=1234,.started=1},{.pid=5678,.started=2}};
    wcscpy(rows[0].exe,L"C:\\Program Files\\Tencent\\Weixin\\Weixin.exe");wcscpy(rows[1].exe,rows[0].exe);
    clients.selected=-1;wx_clients_sync(&clients,rows,2);WxClient* c=wx_clients_active(&clients);
    c->ready=TRUE;c->call.codec=WX_OPUS16;c->values=(WxValues){.target=128000,.current=128000,.normal=30000,.hz=16000,.channels=1,.maximum=128000};
    WxShared active={.status=WX_ACTIVE,.enabled=1};c->session.shared=&active;
    wcscpy(c->message,L"正在保持 128 kbps 编码目标。");c->history_count=60;
    for(int i=0;i<60;i++)c->history[i]=i<15?30:i<17?30+(i-14)*49:128;
    HDC dc=CreateCompatibleDC(NULL);BITMAPINFO info={0};info.bmiHeader.biSize=sizeof(info.bmiHeader);
    info.bmiHeader.biWidth=width;info.bmiHeader.biHeight=height;info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;
    void* pixels=NULL;HBITMAP bitmap=CreateDIBSection(dc,&info,DIB_RGB_COLORS,&pixels,NULL,0);assert(bitmap && pixels);
    HGDIOBJ old=SelectObject(dc,bitmap);paint(dc);GdiFlush();
    BITMAPFILEHEADER header={0};header.bfType=0x4d42;header.bfOffBits=sizeof(header)+sizeof(info.bmiHeader);header.bfSize=header.bfOffBits+width*height*4;
    FILE* out=fopen(argv[1],"wb");assert(out);assert(fwrite(&header,sizeof(header),1,out)==1);
    assert(fwrite(&info.bmiHeader,sizeof(info.bmiHeader),1,out)==1);assert(fwrite(pixels,width*height*4,1,out)==1);fclose(out);
    SelectObject(dc,old);DeleteObject(bitmap);DeleteDC(dc);DestroyIcon(avatar);DestroyWindow(window);
    c->session.shared=NULL;wx_clients_close(&clients);return 0;
}
