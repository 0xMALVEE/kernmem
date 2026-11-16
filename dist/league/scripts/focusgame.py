import ctypes, ctypes.wintypes as w, time

u32 = ctypes.WinDLL('user32', use_last_error=True)
k32 = ctypes.WinDLL('kernel32', use_last_error=True)

u32.FindWindowW.restype = w.HWND
u32.FindWindowW.argtypes = [w.LPCWSTR, w.LPCWSTR]
u32.GetForegroundWindow.restype = w.HWND
u32.GetWindowThreadProcessId.argtypes = [w.HWND, ctypes.POINTER(w.DWORD)]
u32.AttachThreadInput.argtypes = [w.DWORD, w.DWORD, w.BOOL]
u32.SetForegroundWindow.argtypes = [w.HWND]
u32.BringWindowToTop.argtypes = [w.HWND]
u32.ShowWindow.argtypes = [w.HWND, ctypes.c_int]
u32.keybd_event.argtypes = [w.BYTE, w.BYTE, w.DWORD, ctypes.c_void_p]
k32.GetCurrentThreadId.restype = w.DWORD

game = u32.FindWindowW(None, 'League of Legends (TM) Client')
if not game:
    raise SystemExit('game window not found')

for attempt in range(5):
    if u32.GetForegroundWindow() == game:
        break
    fg = u32.GetForegroundWindow()
    me = k32.GetCurrentThreadId()
    fgp = w.DWORD(); u32.GetWindowThreadProcessId(fg, ctypes.byref(fgp))
    tp = w.DWORD(); u32.GetWindowThreadProcessId(game, ctypes.byref(tp))
    u32.keybd_event(0x12, 0, 0, None)
    u32.AttachThreadInput(me, tp, True)
    u32.AttachThreadInput(me, fgp.value, True)
    u32.ShowWindow(game, 9)
    u32.BringWindowToTop(game)
    u32.SetForegroundWindow(game)
    u32.AttachThreadInput(me, tp, False)
    u32.AttachThreadInput(me, fgp.value, False)
    u32.keybd_event(0x12, 0, 2, None)
    time.sleep(0.7)

print('focused: %s' % (u32.GetForegroundWindow() == game))
