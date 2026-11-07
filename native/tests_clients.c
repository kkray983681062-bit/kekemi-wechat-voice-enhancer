#include "clients.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

int main(void){
    WxClients pool={0};pool.selected=-1;
    WxInstance rows[2]={{.pid=101,.started=11},{.pid=202,.started=22}};
    wcscpy(rows[0].exe,L"C:\\Weixin\\Weixin.exe");wcscpy(rows[1].exe,rows[0].exe);
    wx_clients_sync(&pool,rows,1);
    WxClient* first=wx_clients_active(&pool);assert(first && first->pid==101);
    first->ready=TRUE;first->values.target=128000;first->history[0]=128;first->history_count=1;
    // A second account can appear after the first process has been cached.
    wx_clients_sync(&pool,rows,2);assert(wx_clients_select(&pool,202));
    WxClient* second=wx_clients_active(&pool);assert(second!=first && second->pid==202 && !second->history_count);
    second->ready=TRUE;second->values.target=100000;second->history[0]=100;second->history_count=1;
    // Process order changes must not switch selection or mix account state.
    WxInstance reverse[2]={rows[1],rows[0]};wx_clients_sync(&pool,reverse,2);
    assert(wx_clients_active(&pool)==second && first->values.target==128000 && second->values.target==100000);
    assert(wx_clients_select(&pool,101) && wx_clients_active(&pool)->history[0]==128);
    assert(second->history[0]==100);
    // Recycled PID is a new account instance; old state must be discarded.
    rows[0].started=33;wx_clients_sync(&pool,rows,2);
    assert(!first->ready && first->values.target==0 && !first->history_count);
    wx_clients_sync(&pool,rows+1,1);assert(wx_clients_active(&pool)==second);
    wx_clients_sync(&pool,NULL,0);assert(wx_clients_active(&pool)==&pool.empty);
    assert(!wx_clients_select(&pool,101));wx_clients_close(&pool);
    puts("PASS: late second instance, same executable path, independent state, stable selection, process exit and PID reuse");
    return 0;
}
