#include "clients.h"
#include <string.h>
static void close_client(WxClient* c){wx_stop(&c->session,3000);wx_close(&c->context);memset(c,0,sizeof(*c));}
WxClient* wx_clients_active(WxClients* p){
    return p->selected>=0 && p->selected<WX_CLIENT_LIMIT && p->item[p->selected].pid?&p->item[p->selected]:&p->empty;
}
void wx_clients_choose(WxClients* p){
    WxClient* current=wx_clients_active(p);if(current!=&p->empty && (p->pinned || current->ready))return;
    int first=-1;
    for(int i=0;i<WX_CLIENT_LIMIT;i++)if(p->item[i].pid){if(first<0)first=i;if(p->item[i].ready){p->selected=i;return;}}
    if(current==&p->empty){p->selected=first;p->pinned=FALSE;}
}
void wx_clients_sync(WxClients* p,const WxInstance* rows,int count){
    for(int i=0;i<WX_CLIENT_LIMIT;i++)if(p->item[i].pid){
        BOOL alive=FALSE;for(int j=0;j<count;j++)if(p->item[i].pid==rows[j].pid && p->item[i].started==rows[j].started)alive=TRUE;
        if(!alive){close_client(&p->item[i]);if(p->selected==i){p->selected=-1;p->pinned=FALSE;}}
    }
    for(int j=0;j<count;j++){
        int free_slot=-1;BOOL known=FALSE;
        for(int i=0;i<WX_CLIENT_LIMIT;i++){
            if(p->item[i].pid==rows[j].pid){known=TRUE;break;}
            if(!p->item[i].pid && free_slot<0)free_slot=i;
        }
        if(known || free_slot<0)continue;
        WxClient* c=&p->item[free_slot];c->pid=rows[j].pid;c->started=rows[j].started;wcscpy(c->exe,rows[j].exe);
        c->last_error=WX_NO_CALL;wcscpy(c->message,L"正在读取此微信实例…");
    }
    wx_clients_choose(p);
}
BOOL wx_clients_select(WxClients* p,DWORD pid){
    for(int i=0;i<WX_CLIENT_LIMIT;i++)if(p->item[i].pid==pid && pid){p->selected=i;p->pinned=TRUE;return TRUE;}return FALSE;
}
void wx_clients_close(WxClients* p){for(int i=0;i<WX_CLIENT_LIMIT;i++)close_client(&p->item[i]);close_client(&p->empty);p->selected=-1;p->pinned=FALSE;}
