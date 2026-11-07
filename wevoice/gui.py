"""Dark Windows desktop UI matching the approved preview."""
import ctypes
import queue
import threading
import time
import tkinter as tk
from collections import deque
from pathlib import Path
from tkinter import filedialog, messagebox
from PIL import Image, ImageTk

from . import TITLE, VERSION
from .core import VoiceError
from .engine import Engine
from .locator import Settings, discover

ASSETS = Path(__file__).resolve().parent.parent / 'assets'
BG = '#0c0f17'
TEXT = '#edf0f7'
MUTED = '#a8b2c5'
DIM = '#6f7a90'


class App:
    def __init__(self):
        try:
            ctypes.windll.shcore.SetProcessDpiAwareness(2)
        except OSError:
            pass
        self.root = tk.Tk()
        self.root.title(TITLE)
        self.root.configure(bg=BG)
        self.root.resizable(False, False)
        self.root.iconbitmap(str(ASSETS / 'app.ico'))
        self.root.update_idletasks()
        self.scale = min(self.root.winfo_fpixels('1i') / 96,
                         (self.root.winfo_screenheight() - 70) / 790)
        self.scale = max(0.75, self.scale)
        w, h = int(584 * self.scale), int(790 * self.scale)
        x = (self.root.winfo_screenwidth() - w) // 2
        y = max(10, (self.root.winfo_screenheight() - h) // 2)
        self.root.geometry(f'{w}x{h}+{x}+{y}')
        self.root.overrideredirect(True)
        self.canvas = tk.Canvas(self.root, width=w, height=h, bg=BG, highlightthickness=0)
        self.canvas.pack(fill='both', expand=True)
        self.avatar = ImageTk.PhotoImage(Image.open(ASSETS / 'avatar.jpg').resize(
            (int(30*self.scale), int(30*self.scale)), Image.Resampling.LANCZOS))
        self.path, self.origin = '', '正在自动查找'
        self.data = None
        self.history = deque()
        self.call_key = None
        self.message = '正在查找微信并读取通话状态…'
        self.error = False
        self.busy = True
        self.polling = False
        self.closing = False
        self.buttons = []
        self.drag_origin = None
        self.commands, self.results = queue.Queue(), queue.Queue()
        self.next_poll = time.monotonic() + 1
        self.worker_thread = threading.Thread(target=self.worker, daemon=True)
        self.worker_thread.start()
        self.commands.put(('discover', None))
        self.canvas.bind('<ButtonPress-1>', self.on_click)
        self.canvas.bind('<B1-Motion>', self.on_drag)
        self.canvas.bind('<ButtonRelease-1>', lambda _: setattr(self, 'drag_origin', None))
        self.canvas.bind('<Motion>', self.on_motion)
        self.root.bind('<Alt-F4>', lambda _: self.close())
        self.root.bind('<F5>', lambda _: self.request('read'))
        self.root.bind('<Return>', lambda _: self.request('apply') if self.data else None)
        self.root.bind('<Map>', self.mapped)
        self.root.protocol('WM_DELETE_WINDOW', self.close)
        self.root.after(80, self.style_window)
        self.root.after(100, self.tick)
        self.draw()

    def style_window(self):
        # Give the borderless window a normal taskbar entry and Windows 11 corners.
        u = ctypes.windll.user32
        u.GetParent.argtypes = [ctypes.c_void_p]
        u.GetParent.restype = ctypes.c_void_p
        hwnd = u.GetParent(self.root.winfo_id())
        u.GetWindowLongW.argtypes = [ctypes.c_void_p, ctypes.c_int]
        u.SetWindowLongW.argtypes = [ctypes.c_void_p, ctypes.c_int, ctypes.c_long]
        ex = u.GetWindowLongW(hwnd, -20)
        u.SetWindowLongW(hwnd, -20, (ex & ~0x80) | 0x40000)
        try:
            corner = ctypes.c_int(2)
            ctypes.windll.dwmapi.DwmSetWindowAttribute(ctypes.c_void_p(hwnd), 33,
                                                      ctypes.byref(corner), 4)
        except OSError:
            pass

    def mapped(self, event):
        if event.widget == self.root and self.root.state() == 'normal':
            self.root.overrideredirect(True)

    def minimize(self):
        self.root.overrideredirect(False)
        self.root.iconify()

    def worker(self):
        engine, settings = Engine(), Settings()
        path = ''
        try:
            while True:
                operation, arg = self.commands.get()
                try:
                    if operation == 'quit':
                        engine.close()
                        self.results.put(('quit', None, None))
                        return
                    if operation == 'discover':
                        path, origin = discover(settings)
                        self.results.put(('path', (path, origin), None))
                    elif operation == 'select':
                        engine.close()
                        settings.save(arg)
                        path = settings.load()
                        self.results.put(('path', (path, '手动选择 · 已记住'), None))
                    if operation == 'apply':
                        result = engine.apply(path)
                    else:
                        if operation == 'restore':
                            engine.restore()
                        result = engine.read(path)
                    self.results.put((operation, result, None))
                except Exception as exc:
                    if isinstance(exc, VoiceError):
                        error = str(exc)
                    elif isinstance(exc, ValueError):
                        error = str(exc)
                    else:
                        error = '无法完成操作，请确认微信正在运行并已接通语音。'
                    self.results.put((operation, None, error))
                    if operation == 'quit':
                        return
        finally:
            try:
                engine.close()
            except Exception:
                pass

    def request(self, operation):
        if self.busy or self.closing:
            return
        if operation == 'poll':
            if self.polling:
                return
            self.polling = True
        else:
            self.busy = True
        if operation != 'poll':
            self.message = {'read': '正在读取当前通话…', 'apply': '正在应用并回读 128 kbps…',
                            'restore': '正在恢复微信默认码率…'}.get(operation, self.message)
        self.commands.put((operation, None))
        self.draw()

    def choose(self):
        if self.busy or self.closing:
            return
        selected = filedialog.askopenfilename(parent=self.root, title='选择 64 位微信主程序',
                    initialdir=str(Path(self.path).parent) if self.path else None,
                    filetypes=[('微信主程序', 'Weixin.exe WeChat.exe'), ('应用程序', '*.exe')])
        if selected:
            self.busy = True
            self.message = '正在保存微信位置…'
            self.commands.put(('select', selected))
            self.draw()

    def close(self):
        if self.closing:
            return
        self.closing = self.busy = True
        self.message = '正在停止增强并恢复默认，请稍候…'
        self.commands.put(('quit', None))
        self.draw()

    def tick(self):
        now = time.monotonic()
        while True:
            try:
                operation, result, error = self.results.get_nowait()
            except queue.Empty:
                break
            if operation == 'path':
                self.path, self.origin = result
                continue
            if operation == 'quit':
                if error:
                    messagebox.showwarning(TITLE, error, parent=self.root)
                self.root.destroy()
                return
            if operation == 'poll':
                self.polling = False
            else:
                self.busy = self.closing
            self.error = error is not None
            if error:
                self.data = None
                self.message = error
                self.history.append((now, None))
                self.next_poll = now + 3
            else:
                if self.call_key != result['call_key']:
                    self.history.clear()
                    self.call_key = result['call_key']
                self.data = result
                self.history.append((now, result['target_bps'] / 1000))
                self.next_poll = now + 0.5
                if result['holding']:
                    self.message = '已应用，正在持续保持 128 kbps'
                elif operation == 'restore':
                    self.message = '已恢复默认；微信将按通话情况自动调节。'
                elif operation != 'poll' or result.get('changed'):
                    self.message = ('通话已变化，增强已停止；可重新应用。' if result.get('changed')
                                    else '已读取当前通话，可应用 128 kbps。')
        while self.history and self.history[0][0] < now - 30:
            self.history.popleft()
        if not self.busy and not self.polling and not self.closing and now >= self.next_poll:
            self.request('poll')
        self.draw()
        self.root.after(150, self.tick)

    def text(self, x, y, value, size=12, color=TEXT, weight='normal', anchor='nw', family='Microsoft YaHei UI', width=0):
        return self.canvas.create_text(x*self.scale, y*self.scale, text=value, anchor=anchor,
                    fill=color, font=(family, -round(size*self.scale), weight),
                    width=width*self.scale if width else 0)

    def line(self, coords, color, width=1, **kwargs):
        return self.canvas.create_line(*[a*self.scale for a in coords], fill=color,
                                      width=max(1, width*self.scale), **kwargs)

    def rect(self, x, y, w, h, fill, outline='', radius=7):
        r = radius
        coords = [x+r,y,x+w-r,y,x+w,y,x+w,y+r,x+w,y+h-r,x+w,y+h,x+w-r,y+h,
                  x+r,y+h,x,y+h,x,y+h-r,x,y+r,x,y]
        return self.canvas.create_polygon(*[a*self.scale for a in coords], smooth=True,
                                           fill=fill, outline=outline, width=self.scale)

    def button(self, x, y, w, h, label, action, enabled=True, primary=False, small=False):
        fill = '#8176ed' if primary and enabled else '#1d2433'
        outline = '#8379ed' if primary and enabled else '#343c50'
        self.rect(x,y,w,h,fill,outline)
        self.text(x+w/2,y+h/2,label,10 if small else 12,
                  TEXT if enabled else '#5e687e',anchor='center')
        if enabled:
            self.buttons.append((x,y,x+w,y+h,action))

    def draw(self):
        c = self.canvas
        c.delete('all')
        self.buttons = []
        self.rect(0.5,0.5,583,789,BG,'#2b303d',18)
        self.line([1,68,583,68], '#1c202b')
        c.create_image(24*self.scale,19*self.scale, image=self.avatar, anchor='nw')
        self.text(64,23,TITLE,16,weight='bold')
        self.rect(252,25,47,18,'#1b202d','#292f3f',5)
        self.text(275.5,34,'v'+VERSION,10,'#8892a7',anchor='center',family='Segoe UI')
        self.text(514,32,'—',16,'#9da6b8',anchor='center',family='Segoe UI')
        self.text(554,32,'×',18,'#b4bed0',anchor='center',family='Segoe UI')
        self.buttons.extend([(495,8,532,58,self.minimize),(535,8,580,58,self.close)])
        self.text(26,92,'当前编码目标',12,MUTED)
        number = f"{self.data['target_bps']/1000:.2f}" if self.data else '— —'
        self.text(24,108,number,57,weight='bold',family='Segoe UI')
        self.text(225,157,'kbps',12,'#8b95ab',family='Segoe UI')
        holding = bool(self.data and self.data['holding'])
        self.rect(443,97,114,28,'#122821' if holding else '#1b202d',
                  '#234337' if holding else '#2c3445')
        self.text(455,110,'●',8,'#4cd3a2' if holding else '#8793aa',anchor='w')
        self.text(471,110,'增强保持中' if holding else ('等待应用' if self.data else '等待通话'),
                  11,'#79d9b5' if holding else MUTED,anchor='w')
        self.text(26,204,'最近 30 秒',11,MUTED)
        self.line([474,212,488,212], '#a69dff',2)
        self.text(496,204,'编码目标',11,'#8d96ad')
        self.rect(26,230,532,153,'#10131f',radius=6)
        for y in (250,291,332,373):
            self.line([26,y,558,y],'#252c3a',dash=(2,5))
        for x in (114,202,290,378,466):
            self.line([x,240,x,373],'#252c3a',dash=(2,5))
        now = time.monotonic()
        maximum = max(150, max((v for _,v in self.history if v is not None), default=0)*1.12)
        segments, segment = [], []
        for stamp, value in self.history:
            if value is None:
                if segment:
                    segments.append(segment)
                segment = []
            else:
                segment.extend([26+max(0,min(30,stamp-(now-30)))/30*532,373-value/maximum*123])
        if segment:
            segments.append(segment)
        for points in segments:
            if len(points) >= 4:
                self.line(points,'#a69dff',2.5)
        if self.data and segments:
            px,py = segments[-1][-2:]
            c.create_oval((px-3)*self.scale,(py-3)*self.scale,(px+3)*self.scale,(py+3)*self.scale,
                          fill='#c8bfff',outline='#655aab',width=2*self.scale)
        self.text(26,392,'30 秒前',10,DIM)
        self.text(558,392,'现在',10,DIM,anchor='ne')
        self.line([26,421,558,421],'#222735')
        self.line([26,461,558,461],'#222735')
        d = self.data or {}
        self.text(28,435,'通话状态',11,MUTED)
        self.text(86,435,'通话中' if d else '未就绪',11,'#d0d7e5')
        self.text(200,435,'采样率',11,MUTED)
        self.text(248,435,f"{d['sample_rate']/1000:g} kHz" if d else '—',11,'#d0d7e5')
        self.text(346,435,'声道',11,MUTED)
        self.text(380,435,('单声道' if d.get('channels')==1 else '双声道') if d else '—',11,'#d0d7e5')
        self.text(470,435,'编码',11,MUTED)
        self.text(507,435,d.get('codec','—'),11,'#d0d7e5')
        self.text(28,486,'固定增强码率',13)
        self.text(28,512,'应用后自动保持，无需反复设置',10,'#79849b')
        self.rect(412,481,146,47,'#1c1c32','#383353',8)
        self.text(427,491,'128',21,'#ccc5ff',weight='bold',family='Segoe UI')
        self.text(473,503,'kbps · 上限',10,'#9a93bb')
        self.button(26,550,205,40,'↻   读取当前状态',lambda:self.request('read'),not self.busy)
        self.button(241,550,317,40,'应用 128 kbps',lambda:self.request('apply'),
                    bool(self.data) and not self.busy,True)
        self.text(28,607,self.message,10,'#f0a09e' if self.error else '#91a39d',width=435)
        enabled = not self.busy and holding
        self.text(554,607,'恢复默认',10,'#9ca7bd' if enabled else '#4f5a70',anchor='ne')
        if enabled:
            self.buttons.append((493,601,560,632,lambda:self.request('restore')))
        self.rect(26,650,532,94,'#131822','#272e3d',9)
        self.text(40,663,'微信安装位置',11,'#cbd2e1')
        self.text(544,663,self.origin,10,'#82bda6',anchor='ne')
        shown = self.path or '未找到，请手动选择微信程序'
        if len(shown)>57:
            shown = shown[:14]+'…'+shown[-40:]
        self.text(40,692,shown,11,'#8b97b0',family='Segoe UI')
        self.button(457,684,87,29,'选择微信',self.choose,not self.busy,small=True)
        self.text(40,726,'自动查找；未找到时，可手动选择微信程序。',9,'#647188')
        self.text(26,764,'关闭工具时恢复默认码率',9,'#627088')
        self.text(558,764,'曲线显示编码目标，非网络实测速率',9,'#627088',anchor='ne')

    def on_click(self, event):
        x,y=event.x/self.scale,event.y/self.scale
        for left,top,right,bottom,action in self.buttons:
            if left<=x<=right and top<=y<=bottom:
                action()
                return
        if y < 68:
            self.drag_origin=(event.x_root-self.root.winfo_x(),event.y_root-self.root.winfo_y())

    def on_drag(self,event):
        if self.drag_origin:
            dx,dy=self.drag_origin
            self.root.geometry(f'+{event.x_root-dx}+{event.y_root-dy}')

    def on_motion(self,event):
        x,y=event.x/self.scale,event.y/self.scale
        self.canvas.configure(cursor='hand2' if any(a<=x<=c and b<=y<=d
                    for a,b,c,d,_ in self.buttons) else '')

    def run(self):
        self.root.mainloop()
