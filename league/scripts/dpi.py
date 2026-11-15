import ctypes, ctypes.wintypes as w

u32 = ctypes.WinDLL('user32', use_last_error=True)
shc = ctypes.WinDLL('shcore', use_last_error=True)

SM_CXSCREEN = 0
SM_CYSCREEN = 1

class DEVMODE(ctypes.Structure):
    _fields_ = [('dmDeviceName', w.WCHAR * 32), ('dmSpecVersion', w.WORD),
                ('dmDriverVersion', w.WORD), ('dmSize', w.WORD),
                ('dmDriverExtra', w.WORD), ('dmFields', w.DWORD),
                ('dmOrientation', ctypes.c_short), ('dmPaperSize', ctypes.c_short),
                ('dmPaperLength', ctypes.c_short), ('dmPaperWidth', ctypes.c_short),
                ('dmScale', ctypes.c_short), ('dmCopies', ctypes.c_short),
                ('dmDefaultSource', ctypes.c_short), ('dmPrintQuality', ctypes.c_short),
                ('dmColor', ctypes.c_short), ('dmDuplex', ctypes.c_short),
                ('dmYResolution', ctypes.c_short), ('dmTTOption', ctypes.c_short),
                ('dmCollate', ctypes.c_short), ('dmFormName', w.WCHAR * 32),
                ('dmLogPixels', w.WORD), ('dmBitsPerPel', w.DWORD),
                ('dmPelsWidth', w.DWORD), ('dmPelsHeight', w.DWORD),
                ('dmDisplayFlags', w.DWORD), ('dmDisplayFrequency', w.DWORD),
                ('dmICMMethod', w.DWORD), ('dmICMIntent', w.DWORD),
                ('dmMediaType', w.DWORD), ('dmDitherType', w.DWORD),
                ('dmReserved1', w.DWORD), ('dmReserved2', w.DWORD),
                ('dmPanningWidth', w.DWORD), ('dmPanningHeight', w.DWORD)]

print('SM_CXSCREEN/CYSCREEN (as this process sees it): %d x %d' % (
    u32.GetSystemMetrics(SM_CXSCREEN), u32.GetSystemMetrics(SM_CYSCREEN)))

dm = DEVMODE(); dm.dmSize = ctypes.sizeof(dm)
if u32.EnumDisplaySettingsW(None, -1, ctypes.byref(dm)):
    print('EnumDisplaySettings (physical): %d x %d @ %d Hz' % (
        dm.dmPelsWidth, dm.dmPelsHeight, dm.dmDisplayFrequency))
else:
    print('EnumDisplaySettings failed')

try:
    print('GetDpiForSystem: %d' % u32.GetDpiForSystem())
except Exception as e:
    print('GetDpiForSystem n/a', e)

try:
    aware = shc.GetProcessDpiAwareness(None, ctypes.byref(w.DWORD()))
    print('GetProcessDpiAwareness(null) ret', aware)
except Exception as e:
    print('GetProcessDpiAwareness n/a', e)

try:
    ctx = ctypes.c_void_p()
    print('GetThreadDpiAwarenessContext: %s' % u32.GetThreadDpiAwarenessContext())
except Exception as e:
    print('thread ctx n/a', e)

hwnd = u32.FindWindowW(None, 'League of Legends (TM) Client')
print('game hwnd: 0x%X' % (hwnd or 0))
if hwnd:
    try:
        print('GetDpiForWindow: %d' % u32.GetDpiForWindow(hwnd))
    except Exception as e:
        print('GetDpiForWindow n/a', e)
    wr = w.RECT(); cr = w.RECT()
    u32.GetWindowRect(hwnd, ctypes.byref(wr))
    u32.GetClientRect(hwnd, ctypes.byref(cr))
    print('window rect: %d,%d %dx%d' % (wr.left, wr.top, wr.right - wr.left, wr.bottom - wr.top))
    print('client rect: %dx%d' % (cr.right, cr.bottom))
    style = u32.GetWindowLongW(hwnd, -16)
    print('style 0x%08X  maximized=%s' % (style & 0xFFFFFFFF, bool(u32.IsZoomed(hwnd))))
