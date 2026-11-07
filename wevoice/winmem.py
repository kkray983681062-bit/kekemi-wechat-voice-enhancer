"""Read only structural/audio fields. Does not scan chat buffers or record audio."""
import ctypes as C
from ctypes import wintypes as W
import struct

K=C.WinDLL('kernel32',use_last_error=True)
SIZE_T=C.c_size_t

class MODULEENTRY32W(C.Structure):
    _fields_=[('dwSize',W.DWORD),('th32ModuleID',W.DWORD),('th32ProcessID',W.DWORD),
              ('GlblcntUsage',W.DWORD),('ProccntUsage',W.DWORD),('modBaseAddr',C.c_void_p),
              ('modBaseSize',W.DWORD),('hModule',W.HMODULE),('szModule',W.WCHAR*256),
              ('szExePath',W.WCHAR*260)]

K.CreateToolhelp32Snapshot.argtypes=[W.DWORD,W.DWORD]
K.CreateToolhelp32Snapshot.restype=W.HANDLE
K.Module32FirstW.argtypes=[W.HANDLE,C.POINTER(MODULEENTRY32W)]
K.Module32NextW.argtypes=[W.HANDLE,C.POINTER(MODULEENTRY32W)]
K.CloseHandle.argtypes=[W.HANDLE]
K.OpenProcess.argtypes=[W.DWORD,W.BOOL,W.DWORD]
K.OpenProcess.restype=W.HANDLE
K.ReadProcessMemory.argtypes=[W.HANDLE,C.c_void_p,C.c_void_p,SIZE_T,C.POINTER(SIZE_T)]
K.ReadProcessMemory.restype=W.BOOL

def modules(pid):
    snap=K.CreateToolhelp32Snapshot(0x18,pid)
    if snap==C.c_void_p(-1).value: raise C.WinError(C.get_last_error())
    try:
        entry=MODULEENTRY32W(); entry.dwSize=C.sizeof(entry)
        result={}; ok=K.Module32FirstW(snap,C.byref(entry))
        while ok:
            result[entry.szModule.lower()]={'base':entry.modBaseAddr,'size':entry.modBaseSize,'path':entry.szExePath}
            ok=K.Module32NextW(snap,C.byref(entry))
        return result
    finally: K.CloseHandle(snap)

class Reader:
    def __init__(self,pid):
        self.handle=K.OpenProcess(0x410,False,pid)
        if not self.handle: raise C.WinError(C.get_last_error())
    def close(self):
        if self.handle:K.CloseHandle(self.handle); self.handle=None
    def read(self,address,size):
        if not 0x10000<=address<0x7fffffffffff or not 0<size<=65536: raise ValueError('Invalid structural read')
        buf=C.create_string_buffer(size); got=SIZE_T()
        if not K.ReadProcessMemory(self.handle,address,buf,size,C.byref(got)) or got.value!=size:
            raise C.WinError(C.get_last_error())
        return buf.raw
    def ptr(self,address):return struct.unpack('<Q',self.read(address,8))[0]
    def i32(self,address):return struct.unpack('<i',self.read(address,4))[0]

