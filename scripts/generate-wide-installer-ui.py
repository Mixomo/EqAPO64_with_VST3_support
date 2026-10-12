"""Widen NSIS's original Modern UI resources; no external Python packages."""
import ctypes as c
from ctypes import wintypes as w
from pathlib import Path
import shutil
import struct

root = Path(__file__).resolve().parents[1]
source = root / 'third_party/nsis-3.11/Contrib/UIs/modern.exe'
target = root / 'Setup/WideModern.exe'
kernel = c.WinDLL('kernel32', use_last_error=True)
kernel.LoadLibraryExW.argtypes = [w.LPCWSTR, w.HANDLE, w.DWORD]
kernel.LoadLibraryExW.restype = w.HMODULE
kernel.FindResourceW.argtypes = [w.HMODULE, c.c_void_p, c.c_void_p]
kernel.FindResourceW.restype = w.HANDLE
kernel.LoadResource.argtypes = [w.HMODULE, w.HANDLE]
kernel.LoadResource.restype = w.HANDLE
kernel.LockResource.argtypes = [w.HANDLE]
kernel.LockResource.restype = c.c_void_p
kernel.SizeofResource.argtypes = [w.HMODULE, w.HANDLE]
kernel.FreeLibrary.argtypes = [w.HMODULE]
kernel.BeginUpdateResourceW.argtypes = [w.LPCWSTR, w.BOOL]
kernel.BeginUpdateResourceW.restype = w.HANDLE
kernel.UpdateResourceW.argtypes = [w.HANDLE, c.c_void_p, c.c_void_p, w.WORD, c.c_void_p, w.DWORD]
kernel.EndUpdateResourceW.argtypes = [w.HANDLE, w.BOOL]
CALLBACK = c.WINFUNCTYPE(w.BOOL, w.HMODULE, c.c_void_p, c.c_void_p, c.c_ssize_t)
kernel.EnumResourceNamesW.argtypes = [w.HMODULE, c.c_void_p, CALLBACK, c.c_ssize_t]

def skip_string(data, offset):
    value, = struct.unpack_from('<H', data, offset)
    if value == 0xffff:
        return offset + 4
    while struct.unpack_from('<H', data, offset)[0]:
        offset += 2
    return offset + 2

def widen(data):
    data = bytearray(data)
    extended = data[:4] == b'\x01\x00\xff\xff'
    if extended:
        style, = struct.unpack_from('<I', data, 12)
        count, = struct.unpack_from('<H', data, 16)
        position, header = 18, 26
    else:
        style, = struct.unpack_from('<I', data, 0)
        count, = struct.unpack_from('<H', data, 8)
        position, header = 10, 18
    width, = struct.unpack_from('<h', data, position + 4)
    extra = 120
    struct.pack_into('<h', data, position + 4, width + extra)
    offset = skip_string(data, header)  # menu
    offset = skip_string(data, offset)  # class
    offset = skip_string(data, offset)  # title
    if style & 0x40:  # DS_SETFONT
        offset += 6 if extended else 2
        offset = skip_string(data, offset)
    for _ in range(count):
        offset = (offset + 3) & ~3
        position = offset + (12 if extended else 8)
        x, y, cx, cy = struct.unpack_from('<hhhh', data, position)
        control_id = struct.unpack_from('<I' if extended else '<H', data, position + 8)[0]
        # Stretch wide content; move compact right-anchored buttons/labels.
        if control_id == 1032 or cx > width // 3:
            cx += extra
        elif x > width // 2:
            x += extra
        struct.pack_into('<hhhh', data, position, x, y, cx, cy)
        offset += 24 if extended else 18
        offset = skip_string(data, offset)
        offset = skip_string(data, offset)
        creation, = struct.unpack_from('<H', data, offset)
        offset += creation if creation else 2
    return bytes(data)

resources = []
module = kernel.LoadLibraryExW(str(source), None, 0x22)
if not module:
    raise c.WinError(c.get_last_error())

@CALLBACK
def collect(handle, kind, name, unused):
    resource = kernel.FindResourceW(handle, name, kind)
    loaded = kernel.LoadResource(handle, resource)
    resources.append((int(name), widen(c.string_at(kernel.LockResource(loaded), kernel.SizeofResource(handle, resource)))))
    return True

if not kernel.EnumResourceNamesW(module, 5, collect, 0):
    raise c.WinError(c.get_last_error())
kernel.FreeLibrary(module)
shutil.copyfile(source, target)
update = kernel.BeginUpdateResourceW(str(target), False)
if not update:
    raise c.WinError(c.get_last_error())
try:
    for name, data in resources:
        buffer = c.create_string_buffer(data)
        if not kernel.UpdateResourceW(update, 5, name, 1033, buffer, len(data)):
            raise c.WinError(c.get_last_error())
except BaseException:
    kernel.EndUpdateResourceW(update, True)
    raise
if not kernel.EndUpdateResourceW(update, False):
    raise c.WinError(c.get_last_error())
print(f'Widened {len(resources)} dialog resources by 120 dialog units: {target}')
