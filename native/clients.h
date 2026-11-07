#ifndef WX_CLIENTS_H
#define WX_CLIENTS_H
#include "wx.h"
#define WX_CLIENT_LIMIT 32
typedef struct {
    DWORD pid;uint64_t started;
    wchar_t exe[MAX_PATH],message[300];
    WxContext context;WxCall call;WxValues values;WxSession session;
    BOOL ready,error;int last_error;
    double history[60];int history_count;
} WxClient;
typedef struct { WxClient item[WX_CLIENT_LIMIT],empty;int selected;BOOL pinned; } WxClients;
void wx_clients_sync(WxClients*,const WxInstance*,int);
BOOL wx_clients_select(WxClients*,DWORD);
WxClient* wx_clients_active(WxClients*);
void wx_clients_choose(WxClients*);
void wx_clients_close(WxClients*);
#endif
