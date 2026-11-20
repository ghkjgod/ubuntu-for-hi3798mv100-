import os, subprocess, time, ctypes as c
from pathlib import Path

pid = subprocess.check_output(["pidof", "xfce4-session"], text=True).split()[0]
for item in Path("/proc/" + pid + "/environ").read_bytes().split(b"\0"):
    name, _, val = item.partition(b"=")
    if name in (
        b"DISPLAY",
        b"XAUTHORITY",
        b"GTK_IM_MODULE",
        b"XMODIFIERS",
        b"DBUS_SESSION_BUS_ADDRESS",
    ):
        os.environ[name.decode()] = val.decode()
os.environ.update(DISPLAY=":0", XAUTHORITY="/root/.Xauthority")
import gi

gi.require_version("Gtk", "3.0")
from gi.repository import Gtk, GLib

x = c.CDLL("libX11.so.6")
xt = c.CDLL("libXtst.so.6")
x.XOpenDisplay.argtypes = [c.c_char_p]
x.XOpenDisplay.restype = c.c_void_p
dp = c.c_void_p(x.XOpenDisplay(None))
x.XKeysymToKeycode.argtypes = [c.c_void_p, c.c_ulong]
x.XKeysymToKeycode.restype = c.c_uint
xt.XTestFakeKeyEvent.argtypes = [c.c_void_p, c.c_uint, c.c_int, c.c_ulong]
x.XSync.argtypes = [c.c_void_p, c.c_int]
w = Gtk.Window()
w.set_title("Pinyin input test")
e = Gtk.Entry()
w.add(e)
w.show_all()
w.present()
e.grab_focus()


def remote(*args):
    return subprocess.run(
        ["fcitx-remote", *args], capture_output=True, text=True
    ).stdout.strip()


previous = remote()
im = remote("-n")
start = None
keys = iter("zhongwen ")


def press():
    ch = next(keys, None)
    if ch is None:
        return False
    code = x.XKeysymToKeycode(dp, ord(ch))
    for down in (1, 0):
        xt.XTestFakeKeyEvent(dp, code, down, 0)
    x.XSync(dp, 0)
    return True


def begin():
    global start
    remote("-o")
    remote("-s", "googlepinyin")
    start = time.monotonic()
    GLib.timeout_add(90, press)
    return False


def check():
    if e.get_text() == "中文":
        print(
            "Gtk.Entry committed 中文 via Fcitx googlepinyin",
            "seconds including synthetic typing",
            round(time.monotonic() - start, 3),
            flush=True,
        )
        Gtk.main_quit()
        return False
    return True


GLib.timeout_add(1500, begin)
GLib.timeout_add(50, check)
GLib.timeout_add_seconds(10, lambda: (Gtk.main_quit(), False)[1])
Gtk.main()
if im:
    remote("-s", im)
if previous != "2":
    remote("-c")
print("committed:", repr(e.get_text()))
raise SystemExit(e.get_text() != "中文")
