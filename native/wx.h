#ifndef WX_NATIVE_H
#define WX_NATIVE_H
#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#include <windows.h>
#include <stdint.h>
#include <wchar.h>

enum { WX_OK, WX_NO_PROCESS, WX_NO_ENGINE, WX_UNSUPPORTED, WX_NO_CALL,
       WX_CODEC, WX_MULTIPLE, WX_DENIED, WX_ERROR, WX_CHANGED };
enum { WX_OPUS=11, WX_OPUS16=17, WX_SILK=4 };
typedef struct {
    uint64_t p[9];
    uint32_t member[9], adjustment[9], count_member, stride;
    int index, channel, codec, reserved;
} WxCall;
typedef struct { int target, current, normal, hz, channels, frame_ms, maximum; } WxValues;
typedef struct {
    uint32_t service, service_vt, backend_vt, engine_vt, xve_vt, opus_vt, silk_vt;
    uint32_t opus_ctl, silk_encode;
    uint32_t voice_stamp, voice_size, conf_stamp, conf_size;
    uint32_t channel_vt, coder_count, coder_members[32];
} WxProfile;
typedef struct {
    HANDLE process; DWORD pid; uint64_t conf, voice;
    uint64_t started;
    WxProfile profile;
    WxCall hint;
    wchar_t exe[MAX_PATH];
} WxContext;
typedef struct {
    uint32_t magic, version, host_pid, target_pid;
    WxCall call;
    WxProfile profile;
    volatile LONG command, status, error, enabled;
    volatile LONG target, current, normal;
    volatile LONG64 frames, failures, bytes, overrides;
} WxShared;
enum { WX_MAGIC=0x57585631, WX_VERSION=3, WX_STARTING=0, WX_ACTIVE=1,
       WX_STOPPED=2, WX_FAILED=3, WX_STOP=2 };

int wx_locate(WxContext*, WxCall*);
int wx_check(const WxContext*, const WxCall*);
int wx_values(const WxContext*, const WxCall*, WxValues*);
int wx_find(const wchar_t*, WxContext*, WxCall*, WxValues*);
int wx_find_pid(DWORD, WxContext*, WxCall*, WxValues*);
typedef struct { DWORD pid; uint64_t started; wchar_t exe[MAX_PATH]; } WxInstance;
int wx_instances(WxInstance*, int);
int wx_modules(DWORD, WxContext*, wchar_t paths[3][MAX_PATH]);
BOOL wx_resolve_images(const void*, size_t, const void*, size_t, WxProfile*);
BOOL wx_resolve(const wchar_t*, const wchar_t*, WxProfile*);
BOOL wx_profile_loaded(const WxContext*);
BOOL wx_hash(const wchar_t*, const unsigned char*, DWORD);
BOOL wx_sha256(const void*, DWORD, unsigned char out[32]);
BOOL wx_read(HANDLE, uint64_t, void*, SIZE_T);
BOOL wx_equal(const WxCall*,const WxCall*);
void wx_close(WxContext*);
const wchar_t* wx_error(int);

typedef struct {
    HANDLE mapping;
    WxShared* shared;
    wchar_t name[100];
    WxCall call;
} WxSession;
int wx_apply(WxContext*, const WxCall*, WxSession*, wchar_t*, size_t);
BOOL wx_stop(WxSession*, DWORD);
#endif
