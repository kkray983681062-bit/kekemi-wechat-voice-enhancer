#include "wx.h"
#include "clients.h"
#include "version.h"
#include <commdlg.h>
#include <shellapi.h>
#include <shlobj.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <tlhelp32.h>

static WxClients clients;
static ULONGLONG last_scan=0;
static HWND window;
static HICON avatar;
static wchar_t selected[MAX_PATH],settings[MAX_PATH];
static BOOL busy=FALSE;
static BOOL manual_path=FALSE;
static double scale=1;
static int extra_width=0,extra_height=0;
static RECT buttons[7];
enum { BTN_READ, BTN_APPLY, BTN_RESTORE, BTN_SELECT, BTN_MIN, BTN_CLOSE, BTN_INSTANCE };
static int px(double n){return (int)(n*scale+0.5);}
static void layout(HWND w){
    RECT r;GetClientRect(w,&r);if(r.right<=0 || r.bottom<=0)return;
    scale=fmin(r.right/584.0,r.bottom/790.0);if(scale>1.5)scale=1.5;
    extra_width=(int)(r.right/scale+0.5)-584;
    extra_height=(int)(r.bottom/scale+0.5)-790;
    if(extra_width<0)extra_width=0;if(extra_height<0)extra_height=0;
    if(IsZoomed(w))SetWindowRgn(w,NULL,TRUE);
    else SetWindowRgn(w,CreateRoundRectRgn(0,0,r.right+1,r.bottom+1,px(18),px(18)),TRUE);
}
static void text(HDC dc,int x,int y,int width,int height,const wchar_t* value,int size,COLORREF color,BOOL bold,UINT format){
    HFONT font=CreateFontW(-px(size),0,0,0,bold?FW_SEMIBOLD:FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,0,0,CLEARTYPE_QUALITY,0,L"Microsoft YaHei UI");
    HGDIOBJ old=SelectObject(dc,font);SetTextColor(dc,color);SetBkMode(dc,TRANSPARENT);
    RECT r={px(x),px(y),px(x+width),px(y+height)};DrawTextW(dc,value,-1,&r,DT_SINGLELINE|DT_VCENTER|format);SelectObject(dc,old);DeleteObject(font);
}
static void box(HDC dc,int x,int y,int width,int height,COLORREF fill,COLORREF stroke,int radius){
    HBRUSH brush=CreateSolidBrush(fill);HPEN pen=CreatePen(PS_SOLID,1,stroke);
    HGDIOBJ ob=SelectObject(dc,brush),op=SelectObject(dc,pen);RoundRect(dc,px(x),px(y),px(x+width),px(y+height),px(radius),px(radius));
    SelectObject(dc,ob);SelectObject(dc,op);DeleteObject(brush);DeleteObject(pen);
}
static void button(HDC dc,int id,int x,int y,int width,int height,const wchar_t* label,BOOL active,BOOL primary){
    buttons[id]=(RECT){px(x),px(y),px(x+width),px(y+height)};
    box(dc,x,y,width,height,primary&&active?RGB(112,104,207):RGB(29,36,51),RGB(52,60,80),7);
    text(dc,x,y,width,height,label,12,active?RGB(237,240,247):RGB(94,104,126),FALSE,DT_CENTER);
}
static BOOL holding_client(const WxClient* c){return c->session.shared && c->session.shared->status==WX_ACTIVE && c->session.shared->enabled;}
static BOOL holding(void){return holding_client(wx_clients_active(&clients));}
static void paint(HDC target){
    WxClient* view=wx_clients_active(&clients);
    RECT client;GetClientRect(window,&client);if(client.right<=0 || client.bottom<=0)return;
    const int dx=extra_width,dy=extra_height,chart_width=532+dx,chart_span=123+dy,split=dx*2/5;
    HDC dc=CreateCompatibleDC(target);HBITMAP bitmap=CreateCompatibleBitmap(target,client.right,client.bottom);
    HGDIOBJ oldbitmap=SelectObject(dc,bitmap);HBRUSH bg=CreateSolidBrush(RGB(12,15,23));FillRect(dc,&client,bg);DeleteObject(bg);
    COLORREF white=RGB(237,240,247),muted=RGB(168,178,197),dim=RGB(111,122,144);
    DrawIconEx(dc,px(24),px(20),avatar,px(30),px(30),0,NULL,DI_NORMAL);
    text(dc,64,17,360,35,L"克克咪 微信语音增强",16,white,TRUE,DT_LEFT);
    text(dc,64,47,350,17,L"轻量版 · v" KEKEMI_VERSION_WSTRING,10,dim,FALSE,DT_LEFT);
    buttons[BTN_MIN]=(RECT){px(495+dx),px(8),px(532+dx),px(58)};buttons[BTN_CLOSE]=(RECT){px(535+dx),px(8),px(580+dx),px(58)};
    text(dc,495+dx,8,37,50,L"—",16,muted,FALSE,DT_CENTER);text(dc,535+dx,8,45,50,L"×",18,muted,FALSE,DT_CENTER);
    text(dc,26,88,270,25,L"当前编码目标",12,muted,FALSE,DT_LEFT);
    wchar_t number[40];if(view->ready)swprintf(number,40,L"%.2f",view->values.target/1000.0);else wcscpy(number,L"— —");
    text(dc,24,105,270,76,number,52,white,TRUE,DT_LEFT);text(dc,270,145,70,30,L"kbps",12,muted,FALSE,DT_LEFT);
    BOOL held=holding();box(dc,437+dx,96,121,29,held?RGB(18,40,33):RGB(27,32,45),RGB(44,52,69),6);
    const wchar_t* badge=held?(view->values.current==view->values.target?L"增强保持中":L"等待语音编码"):view->ready?L"等待应用":view->last_error==WX_CODEC?L"编码未适配":view->last_error==WX_UNSUPPORTED?L"接口未识别":view->last_error==WX_NO_PROCESS?L"等待微信":L"等待通话";
    text(dc,437+dx,96,121,29,badge,11,held?RGB(121,217,181):muted,FALSE,DT_CENTER);
    wchar_t instance_label[80];
    if(view->pid)swprintf(instance_label,80,L"微信 %d · PID %lu  ▾",clients.selected+1,view->pid);
    else wcscpy(instance_label,L"选择微信实例  ▾");
    button(dc,BTN_INSTANCE,351+dx,148,207,28,instance_label,!busy,FALSE);
    text(dc,26,200,250,25,L"最近 30 秒",11,muted,FALSE,DT_LEFT);text(dc,448+dx,200,110,25,L"— 编码目标",11,RGB(166,157,255),FALSE,DT_RIGHT);
    box(dc,26,230,chart_width,153+dy,RGB(16,19,31),RGB(16,19,31),6);
    HPEN grid=CreatePen(PS_DOT,1,RGB(37,44,58));HGDIOBJ oldpen=SelectObject(dc,grid);
    for(int i=0;i<4;i++){int y=250+i*chart_span/3;MoveToEx(dc,px(26),px(y),NULL);LineTo(dc,px(558+dx),px(y));}
    for(int i=1;i<6;i++){int x=26+i*chart_width/6;MoveToEx(dc,px(x),px(240),NULL);LineTo(dc,px(x),px(373+dy));}
    SelectObject(dc,oldpen);DeleteObject(grid);
    HPEN line=CreatePen(PS_SOLID,px(2),RGB(166,157,255));oldpen=SelectObject(dc,line);BOOL segment=FALSE;
    double max=150;for(int i=0;i<view->history_count;i++)if(view->history[i]*1.12>max)max=view->history[i]*1.12;
    for(int i=0;i<view->history_count;i++){if(view->history[i]<0){segment=FALSE;continue;}
        int x=px(26+(60-view->history_count+i)*chart_width/59.0),y=px(373+dy-view->history[i]/max*chart_span);
        if(segment)LineTo(dc,x,y);else MoveToEx(dc,x,y,NULL);segment=TRUE;}
    SelectObject(dc,oldpen);DeleteObject(line);
    text(dc,26,390+dy,100,25,L"30 秒前",10,dim,FALSE,DT_LEFT);text(dc,458+dx,390+dy,100,25,L"现在",10,dim,FALSE,DT_RIGHT);
    box(dc,26,421+dy,chart_width,40,RGB(12,15,23),RGB(34,39,53),1);
    text(dc,28,429+dy,66,24,L"通话状态",11,muted,FALSE,DT_LEFT);text(dc,94,429+dy,90,24,view->ready?L"通话中":view->last_error==WX_CODEC?L"已检测到":L"未就绪",11,white,FALSE,DT_LEFT);
    text(dc,198+dx/3,429+dy,60,24,L"采样率",11,muted,FALSE,DT_LEFT);if(view->ready)swprintf(number,40,L"%g kHz",view->values.hz/1000.0);else wcscpy(number,L"—");text(dc,250+dx/3,429+dy,85,24,number,11,white,FALSE,DT_LEFT);
    text(dc,342+dx*2/3,429+dy,45,24,L"声道",11,muted,FALSE,DT_LEFT);text(dc,382+dx*2/3,429+dy,70,24,view->ready?(view->values.channels==1?L"单声道":L"双声道"):L"—",11,white,FALSE,DT_LEFT);
    text(dc,466+dx,429+dy,40,24,L"编码",11,muted,FALSE,DT_LEFT);text(dc,507+dx,429+dy,49,24,view->ready?(view->call.codec==WX_SILK?L"SILK":L"Opus"):L"—",11,white,FALSE,DT_LEFT);
    text(dc,28,482+dy,300,28,L"增强编码目标",13,white,FALSE,DT_LEFT);
    text(dc,28,512+dy,365,22,L"Opus 128 / SILK 100 kbps · 应用后自动保持",10,dim,FALSE,DT_LEFT);
    box(dc,412+dx,481+dy,146,47,RGB(28,28,50),RGB(56,51,83),8);
    if(view->ready)swprintf(number,40,L"%d",view->values.maximum/1000);else wcscpy(number,L"—");text(dc,427+dx,486+dy,55,35,number,21,RGB(204,197,255),TRUE,DT_LEFT);
    text(dc,478+dx,499+dy,77,25,L"kbps · 目标",10,RGB(154,147,187),FALSE,DT_LEFT);
    button(dc,BTN_READ,26,550+dy,205+split,40,L"读取当前状态",!busy,FALSE);
    if(view->ready)swprintf(number,40,L"应用 %d kbps",view->values.maximum/1000);else wcscpy(number,L"应用增强");
    button(dc,BTN_APPLY,241+split,550+dy,317+dx-split,40,number,view->ready&&!busy,TRUE);
    text(dc,28,599+dy,453+dx,42,view->message,10,view->error?RGB(240,160,158):RGB(145,163,157),FALSE,DT_LEFT|DT_END_ELLIPSIS);
    buttons[BTN_RESTORE]=(RECT){px(488+dx),px(600+dy),px(560+dx),px(640+dy)};text(dc,488+dx,602+dy,70,30,L"恢复默认",10,held?muted:dim,FALSE,DT_RIGHT);
    box(dc,26,650+dy,chart_width,94,RGB(19,24,34),RGB(39,46,61),9);
    text(dc,40,657+dy,250,26,L"微信安装位置",11,white,FALSE,DT_LEFT);text(dc,420+dx,657+dy,124,26,*selected?L"已记住的位置":L"自动查找",10,RGB(130,189,166),FALSE,DT_RIGHT);
    text(dc,40,690+dy,400+dx,25,view->pid?view->exe:*selected?selected:L"未找到，可手动选择微信程序",11,muted,FALSE,DT_LEFT|DT_PATH_ELLIPSIS);
    button(dc,BTN_SELECT,457+dx,684+dy,87,29,L"选择微信",!busy,FALSE);
    text(dc,40,721+dy,490+dx,18,L"自动识别；多开可在上方切换，各实例独立保持。",9,dim,FALSE,DT_LEFT);
    text(dc,26,757+dy,240,25,L"关闭工具后恢复微信默认控制",9,dim,FALSE,DT_LEFT);
    text(dc,283+dx,757+dy,275,25,L"曲线显示编码目标，非网络实测速率",9,dim,FALSE,DT_RIGHT);
    BitBlt(target,0,0,client.right,client.bottom,dc,0,0,SRCCOPY);SelectObject(dc,oldbitmap);DeleteObject(bitmap);DeleteDC(dc);
}
static void redraw(void){if(window){InvalidateRect(window,NULL,FALSE);UpdateWindow(window);}}
static BOOL valid_exe(const wchar_t* path){
    const wchar_t* base=wcsrchr(path,L'\\');base=base?base+1:path;
    if(_wcsicmp(base,L"Weixin.exe") && _wcsicmp(base,L"WeChat.exe"))return FALSE;
    HANDLE f=CreateFileW(path,GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,NULL,OPEN_EXISTING,0,NULL);
    if(f==INVALID_HANDLE_VALUE)return FALSE;IMAGE_DOS_HEADER dos;DWORD n;BOOL ok=FALSE;
    if(ReadFile(f,&dos,sizeof(dos),&n,NULL) && n==sizeof(dos) && dos.e_magic==IMAGE_DOS_SIGNATURE && dos.e_lfanew>=64 && dos.e_lfanew<1048576){
        DWORD sig;WORD arch;SetFilePointer(f,dos.e_lfanew,NULL,FILE_BEGIN);
        ok=ReadFile(f,&sig,4,&n,NULL)&&n==4&&sig==IMAGE_NT_SIGNATURE&&ReadFile(f,&arch,2,&n,NULL)&&n==2&&arch==IMAGE_FILE_MACHINE_AMD64;
    }CloseHandle(f);return ok;
}
static void remember(void){if(*selected){WritePrivateProfileStringW(L"WeChat",L"Path",selected,settings);WritePrivateProfileStringW(L"WeChat",L"Manual",manual_path?L"1":L"0",settings);}}
static BOOL running_path(wchar_t* path){
    HANDLE snap=CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS,0);if(snap==INVALID_HANDLE_VALUE)return FALSE;
    PROCESSENTRY32W pe={0};pe.dwSize=sizeof(pe);BOOL found=FALSE;
    for(BOOL ok=Process32FirstW(snap,&pe);ok;ok=Process32NextW(snap,&pe)){
        if(_wcsicmp(pe.szExeFile,L"Weixin.exe") && _wcsicmp(pe.szExeFile,L"WeChat.exe"))continue;
        HANDLE process=OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION,FALSE,pe.th32ProcessID);if(!process)continue;
        DWORD size=MAX_PATH;BOOL read=QueryFullProcessImageNameW(process,0,path,&size);CloseHandle(process);
        if(read && valid_exe(path)){found=TRUE;break;}
    }CloseHandle(snap);return found;
}
static void discover(void){
    wchar_t folder[MAX_PATH];if(SUCCEEDED(SHGetFolderPathW(NULL,CSIDL_LOCAL_APPDATA,NULL,0,folder))){
        swprintf(settings,MAX_PATH,L"%ls\\KekemiWeChatVoiceEnhancer",folder);CreateDirectoryW(settings,NULL);
        wcscat(settings,L"\\native.ini");GetPrivateProfileStringW(L"WeChat",L"Path",L"",selected,MAX_PATH,settings);
        manual_path=GetPrivateProfileIntW(L"WeChat",L"Manual",0,settings)!=0;
        if(manual_path && *selected && valid_exe(selected))return;
    }
    wchar_t active[MAX_PATH];if(running_path(active)){wcscpy(selected,active);manual_path=FALSE;remember();return;}
    if(*selected && valid_exe(selected))return;*selected=0;manual_path=FALSE;
    const HKEY hives[]={HKEY_CURRENT_USER,HKEY_LOCAL_MACHINE};const REGSAM views[]={KEY_WOW64_64KEY,KEY_WOW64_32KEY};
    const wchar_t* names[]={L"Weixin",L"WeChat"};
    for(int h=0;h<2;h++)for(int v=0;v<2;v++)for(int n=0;n<2;n++){
        wchar_t key[200],value[MAX_PATH];DWORD size=sizeof(value);HKEY opened;
        swprintf(key,200,L"SOFTWARE\\Tencent\\%ls",names[n]);
        if(RegOpenKeyExW(hives[h],key,0,KEY_READ|views[v],&opened)==ERROR_SUCCESS){
            DWORD type=0;BOOL ok=RegQueryValueExW(opened,L"InstallPath",NULL,&type,(BYTE*)value,&size)==ERROR_SUCCESS;RegCloseKey(opened);
            if(ok && (type==REG_SZ || type==REG_EXPAND_SZ)){wchar_t candidate[MAX_PATH];swprintf(candidate,MAX_PATH,L"%ls\\%ls.exe",value,names[n]);if(valid_exe(candidate)){wcscpy(selected,candidate);remember();return;}}
        }
    }
}
static void poll_client(WxClient* view,BOOL explicit_read){
    WxCall next={0};WxValues next_values={0};int code;
    if(view->context.process && WaitForSingleObject(view->context.process,0)!=WAIT_TIMEOUT){wx_stop(&view->session,1000);wx_close(&view->context);}
    if(view->context.process){code=wx_locate(&view->context,&next);if(!code)code=wx_values(&view->context,&next,&next_values);}
    else {code=wx_find_pid(view->pid,&view->context,&next,&next_values);if(view->context.process && view->context.started!=view->started){wx_close(&view->context);code=WX_CHANGED;}}
    if(view->session.shared && (view->session.shared->status!=WX_ACTIVE || code || !wx_equal(&next,&view->session.call))){
        wx_stop(&view->session,2500);wcscpy(view->message,L"增强已停止；通话变化后请重新应用。");}
    if(!code){if(!wx_equal(&next,&view->call))view->history_count=0;if(explicit_read || holding_client(view) || !view->ready)view->error=FALSE;view->call=next;view->values=next_values;view->ready=TRUE;
        if(holding_client(view))swprintf(view->message,300,view->values.current==view->values.target?L"正在保持 %d kbps 编码目标。":L"目标已设为 %d kbps；等待语音编码。",view->values.maximum/1000);
        else if(explicit_read)swprintf(view->message,300,L"已读取 %ls 通话，可应用 %d kbps。",view->call.codec==WX_SILK?L"SILK":L"Opus",view->values.maximum/1000);
    }else{view->ready=FALSE;view->last_error=code;view->error=code!=WX_NO_CALL && code!=WX_NO_PROCESS && code!=WX_NO_ENGINE;wcscpy(view->message,wx_error(code));}
    if(view->history_count==60){memmove(view->history,view->history+1,59*sizeof(double));view->history_count--;}
    view->history[view->history_count++]=view->ready?view->values.target/1000.0:-1;
}
static void poll(BOOL explicit_read){
    if(busy)return;
    ULONGLONG now=GetTickCount64();
    if(explicit_read || !last_scan || now-last_scan>=2000){
        WxInstance found[WX_CLIENT_LIMIT]={0};int count=wx_instances(found,WX_CLIENT_LIMIT);
        wx_clients_sync(&clients,found,count);last_scan=now;
        if(!count){
            WxContext check={0};WxCall call_info={0};WxValues metrics={0};
            int code=wx_find(selected,&check,&call_info,&metrics);wx_close(&check);
            clients.empty.last_error=code?code:WX_NO_PROCESS;
            wcscpy(clients.empty.message,wx_error(clients.empty.last_error));
        }
    }
    WxClient* selected_client=wx_clients_active(&clients);
    for(int i=0;i<WX_CLIENT_LIMIT;i++)if(clients.item[i].pid)
        poll_client(&clients.item[i],explicit_read && &clients.item[i]==selected_client);
    wx_clients_choose(&clients);redraw();
}
static void action(int id){
    WxClient* view=wx_clients_active(&clients);
    if(id==BTN_CLOSE){PostMessageW(window,WM_CLOSE,0,0);return;}if(id==BTN_MIN){ShowWindow(window,SW_MINIMIZE);return;}
    if(busy)return;
    if(id==BTN_READ){poll(TRUE);return;}
    if(id==BTN_INSTANCE){
        poll(TRUE);HMENU menu=CreatePopupMenu();int count=0;
        DWORD menu_pids[WX_CLIENT_LIMIT]={0};uint64_t menu_started[WX_CLIENT_LIMIT]={0};
        for(int i=0;i<WX_CLIENT_LIMIT;i++)if(clients.item[i].pid){
            WxClient* c=&clients.item[i];wchar_t label[160];
            menu_pids[i]=c->pid;menu_started[i]=c->started;
            const wchar_t* state=holding_client(c)?L"增强保持中":c->ready?(c->call.codec==WX_SILK?L"SILK 通话":L"Opus 通话"):L"等待通话";
            swprintf(label,160,L"微信 %d · PID %lu · %ls",i+1,c->pid,state);
            AppendMenuW(menu,MF_STRING|(clients.selected==i?MF_CHECKED:0),1000+i,label);count++;
        }
        if(!count)AppendMenuW(menu,MF_STRING|MF_GRAYED,999,L"未发现可读取的微信实例");
        POINT pt={buttons[BTN_INSTANCE].left,buttons[BTN_INSTANCE].bottom};ClientToScreen(window,&pt);
        UINT choice=TrackPopupMenu(menu,TPM_RETURNCMD|TPM_LEFTALIGN|TPM_TOPALIGN,pt.x,pt.y,0,window,NULL);DestroyMenu(menu);
        if(choice>=1000 && choice<1000+WX_CLIENT_LIMIT){
            unsigned i=choice-1000;
            if(clients.item[i].pid==menu_pids[i] && clients.item[i].started==menu_started[i])wx_clients_select(&clients,menu_pids[i]);
        }
        poll(TRUE);return;
    }
    if(id==BTN_SELECT){wchar_t path[MAX_PATH]=L"";OPENFILENAMEW file={0};file.lStructSize=sizeof(file);file.hwndOwner=window;
        file.lpstrFilter=L"微信程序\0Weixin.exe;WeChat.exe\0应用程序\0*.exe\0\0";file.lpstrFile=path;file.nMaxFile=MAX_PATH;file.lpstrTitle=L"选择 64 位微信主程序";file.Flags=OFN_FILEMUSTEXIST|OFN_NOCHANGEDIR;
        if(GetOpenFileNameW(&file)){if(!valid_exe(path)){view->error=TRUE;wcscpy(view->message,L"请选择 64 位微信的 Weixin.exe 或 WeChat.exe。");redraw();return;}
            wcscpy(selected,path);manual_path=TRUE;remember();poll(TRUE);
            for(int i=0;i<WX_CLIENT_LIMIT;i++)if(clients.item[i].pid && !_wcsicmp(clients.item[i].exe,path)){wx_clients_select(&clients,clients.item[i].pid);break;}redraw();}return;
    }
    if(id==BTN_APPLY && view->ready && !holding()){
        busy=TRUE;view->error=FALSE;wcscpy(view->message,L"正在应用并等待实际编码回读…");redraw();wchar_t detail[300]=L"";
        int code=wx_apply(&view->context,&view->call,&view->session,detail,300);busy=FALSE;
        if(code){view->error=TRUE;wcscpy(view->message,*detail?detail:wx_error(code));redraw();}else poll(FALSE);
    }else if(id==BTN_RESTORE && view->session.shared){busy=TRUE;wx_stop(&view->session,3000);busy=FALSE;poll(FALSE);wcscpy(view->message,L"增强已停止，后续编码使用微信默认设置。");redraw();}
}
static LRESULT CALLBACK proc(HWND w,UINT msg,WPARAM wp,LPARAM lp){
    switch(msg){
    case WM_NCCALCSIZE:return 0;
    case WM_GETMINMAXINFO:{
        MINMAXINFO* limits=(MINMAXINFO*)lp;MONITORINFO monitor={0};monitor.cbSize=sizeof(monitor);
        limits->ptMinTrackSize=(POINT){520,704};
        if(GetMonitorInfoW(MonitorFromWindow(w,MONITOR_DEFAULTTONEAREST),&monitor)){
            int width=monitor.rcWork.right-monitor.rcWork.left,height=monitor.rcWork.bottom-monitor.rcWork.top;
            limits->ptMaxPosition=(POINT){monitor.rcWork.left-monitor.rcMonitor.left,monitor.rcWork.top-monitor.rcMonitor.top};
            limits->ptMaxSize=(POINT){width,height};
            if(limits->ptMinTrackSize.x>width)limits->ptMinTrackSize.x=width;
            if(limits->ptMinTrackSize.y>height)limits->ptMinTrackSize.y=height;
        }return 0;}
    case WM_NCHITTEST:{
        POINT p={(short)LOWORD(lp),(short)HIWORD(lp)};ScreenToClient(w,&p);RECT r;GetClientRect(w,&r);
        int edge=px(6);if(edge<6)edge=6;
        if(!IsZoomed(w)){
            BOOL left=p.x<edge,right=p.x>=r.right-edge,top=p.y<edge,bottom=p.y>=r.bottom-edge;
            if(top && left)return HTTOPLEFT;if(top && right)return HTTOPRIGHT;
            if(bottom && left)return HTBOTTOMLEFT;if(bottom && right)return HTBOTTOMRIGHT;
            if(left)return HTLEFT;if(right)return HTRIGHT;if(top)return HTTOP;if(bottom)return HTBOTTOM;
        }
        if(p.y<px(68) && !PtInRect(&buttons[BTN_MIN],p) && !PtInRect(&buttons[BTN_CLOSE],p))return HTCAPTION;
        return HTCLIENT;}
    case WM_SIZE:if(wp!=SIZE_MINIMIZED){layout(w);InvalidateRect(w,NULL,FALSE);}return 0;
    case WM_PAINT:{PAINTSTRUCT p;HDC dc=BeginPaint(w,&p);paint(dc);EndPaint(w,&p);return 0;}
    case WM_ERASEBKGND:return 1;
    case WM_TIMER:poll(FALSE);return 0;
    case WM_KEYDOWN:if(wp==VK_F5)action(BTN_READ);if(wp==VK_RETURN)action(BTN_APPLY);return 0;
    case WM_LBUTTONUP:{POINT p={(short)LOWORD(lp),(short)HIWORD(lp)};for(int i=0;i<7;i++)if(PtInRect(&buttons[i],p)){action(i);return 0;}break;}
    case WM_CLOSE:KillTimer(w,1);wx_clients_close(&clients);DestroyWindow(w);return 0;
    case WM_DESTROY:PostQuitMessage(0);return 0;
    }return DefWindowProcW(w,msg,wp,lp);
}
static int cli(int argc,wchar_t** argv){
    WxContext c={0};WxCall cl={0};WxValues a={0};WxSession s={0};wchar_t detail[300]=L"";
    if(!_wcsicmp(argv[1],L"--instances")){
        WxInstance found[WX_CLIENT_LIMIT]={0};int count=wx_instances(found,WX_CLIENT_LIMIT);
        for(int i=0;i<count;i++){
            int code=wx_find_pid(found[i].pid,&c,&cl,&a);
            printf("{\"pid\":%lu,\"status\":%d,\"codec\":%d,\"target\":%d}\n",found[i].pid,code,code?0:cl.codec,code?0:a.target);wx_close(&c);
        }return 0;
    }
    DWORD pid=0;if(!_wcsicmp(argv[1],L"--probe") && argc>2)pid=wcstoul(argv[2],NULL,10);
    else if(!_wcsicmp(argv[1],L"--exercise") && argc>3)pid=wcstoul(argv[3],NULL,10);
    int result=pid?wx_find_pid(pid,&c,&cl,&a):wx_find(L"",&c,&cl,&a);if(result){printf("{\"error\":%d}\n",result);wx_close(&c);return result;}
    printf("{\"phase\":\"before\",\"codec\":%d,\"target\":%d,\"current\":%d,\"hz\":%d,\"channels\":%d}\n",cl.codec,a.target,a.current,a.hz,a.channels);fflush(stdout);
    if(!_wcsicmp(argv[1],L"--probe")){wx_close(&c);return 0;}
    result=wx_apply(&c,&cl,&s,detail,300);
    if(result){char text[1200];if(WideCharToMultiByte(CP_UTF8,0,detail,-1,text,sizeof(text),NULL,NULL)>0)fprintf(stderr,"%s\n",text);
        printf("{\"apply_error\":%d}\n",result);wx_stop(&s,3000);wx_close(&c);return result;}
    int seconds=argc>2?_wtoi(argv[2]):10;if(seconds<1 || seconds>60)seconds=10;
    for(int i=0;i<seconds*4;i++){Sleep(250);result=wx_values(&c,&cl,&a);
        if(result || !s.shared || s.shared->status!=WX_ACTIVE || a.target!=a.maximum || a.current!=a.maximum){result=WX_ERROR;break;}}
    if(s.shared)printf("{\"phase\":\"holding\",\"target\":%ld,\"current\":%ld,\"frames\":%lld,\"failures\":%lld,\"overrides\":%lld}\n",s.shared->target,s.shared->current,s.shared->frames,s.shared->failures,s.shared->overrides);
    if(!wx_stop(&s,3000))result=WX_ERROR;Sleep(100);
    if(wx_values(&c,&cl,&a)==0){printf("{\"phase\":\"after\",\"target\":%d,\"current\":%d,\"default\":%d}\n",a.target,a.current,a.normal);if(a.target!=a.normal || a.current!=a.normal)result=WX_ERROR;}
    wx_close(&c);return result;
}
int WINAPI wWinMain(HINSTANCE instance,HINSTANCE previous,LPWSTR cmd,int show){
    (void)previous;(void)cmd;clients.selected=-1;int argc=0;wchar_t** argv=CommandLineToArgvW(GetCommandLineW(),&argc);
    if(argc>1 && (!_wcsicmp(argv[1],L"--probe") || !_wcsicmp(argv[1],L"--exercise") || !_wcsicmp(argv[1],L"--instances"))){int result=cli(argc,argv);LocalFree(argv);return result;}LocalFree(argv);
    HANDLE mutex=CreateMutexW(NULL,FALSE,L"Local\\KekemiWeChatVoiceEnhancer-v1");
    if(!mutex || GetLastError()==ERROR_ALREADY_EXISTS){MessageBoxW(NULL,L"增强工具已经运行，请先关闭其他增强窗口。",L"克克咪 微信语音增强",MB_OK|MB_ICONINFORMATION);if(mutex)CloseHandle(mutex);return 0;}
    SetProcessDPIAware();int height=GetSystemMetrics(SM_CYSCREEN);scale=(height-80)/790.0;if(scale>1.5)scale=1.5;if(scale<0.75)scale=0.75;
    avatar=LoadImageW(instance,MAKEINTRESOURCEW(101),IMAGE_ICON,px(32),px(32),LR_DEFAULTCOLOR);
    WNDCLASSW cls={0};cls.style=CS_DBLCLKS;cls.lpfnWndProc=proc;cls.hInstance=instance;cls.lpszClassName=L"KekemiWechatNative";cls.hCursor=LoadCursor(NULL,IDC_ARROW);cls.hIcon=avatar;RegisterClassW(&cls);
    int width=px(584),h=px(790);window=CreateWindowExW(WS_EX_APPWINDOW,cls.lpszClassName,L"克克咪 微信语音增强 · 轻量版",WS_POPUP|WS_THICKFRAME|WS_MINIMIZEBOX|WS_MAXIMIZEBOX|WS_SYSMENU,(GetSystemMetrics(SM_CXSCREEN)-width)/2,(height-h)/2,width,h,NULL,NULL,instance,NULL);
    if(!window){CloseHandle(mutex);return 1;}
    layout(window);
    discover();ShowWindow(window,show);UpdateWindow(window);poll(TRUE);SetTimer(window,1,500,NULL);
    MSG msg;while(GetMessageW(&msg,NULL,0,0)>0){TranslateMessage(&msg);DispatchMessageW(&msg);}DestroyIcon(avatar);CloseHandle(mutex);return 0;
}
