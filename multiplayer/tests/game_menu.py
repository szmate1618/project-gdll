"""Linux/X11: drive the real game window through modes, failure and close."""
import ctypes as C
import ctypes.util
import os
from pathlib import Path
import shutil
import socket
import subprocess
import sys
import time

client, server, asset, directory = sys.argv[1:]
directory = Path(directory)
directory.mkdir(parents=True, exist_ok=True)
library = ctypes.util.find_library("X11")
if not library:
    sys.exit(77)
x = C.CDLL(library)
Window = C.c_ulong
x.XOpenDisplay.restype = C.c_void_p
x.XOpenDisplay.argtypes = [C.c_char_p]
display = x.XOpenDisplay(None)
if not display:
    sys.exit(77)


class Key(C.Structure):
    _fields_ = [("type", C.c_int), ("serial", C.c_ulong), ("send_event", C.c_int),
                ("display", C.c_void_p), ("window", Window), ("root", Window), ("subwindow", Window),
                ("time", C.c_ulong), ("x", C.c_int), ("y", C.c_int), ("x_root", C.c_int),
                ("y_root", C.c_int), ("state", C.c_uint), ("keycode", C.c_uint), ("same_screen", C.c_int)]


class ClientMessage(C.Structure):
    _fields_ = [("type", C.c_int), ("serial", C.c_ulong), ("send_event", C.c_int),
                ("display", C.c_void_p), ("window", Window), ("message_type", C.c_ulong),
                ("format", C.c_int), ("data", C.c_long * 5)]


class Event(C.Union):
    _fields_ = [("key", Key), ("message", ClientMessage), ("pad", C.c_long * 24)]


class XError(C.Structure):
    _fields_ = [("type", C.c_int), ("display", C.c_void_p),
                ("resourceid", Window), ("serial", C.c_ulong),
                ("error_code", C.c_ubyte), ("request_code", C.c_ubyte), ("minor_code", C.c_ubyte)]


ErrorHandler = C.CFUNCTYPE(C.c_int, C.c_void_p, C.POINTER(XError))
x.XSetErrorHandler.argtypes = [ErrorHandler]
x.XSetErrorHandler.restype = ErrorHandler


@ErrorHandler
def handle_error(display, error):
    # Other desktop windows may disappear while owned_window walks the tree.
    if error.contents.error_code == 3:  # BadWindow
        return 0
    return previous_error_handler(display, error)


previous_error_handler = x.XSetErrorHandler(handle_error)


# Declare pointer arguments explicitly; default ctypes conversions truncate them.
x.XDefaultRootWindow.argtypes = [C.c_void_p]
x.XDefaultRootWindow.restype = Window
x.XQueryTree.argtypes = [C.c_void_p, Window, C.POINTER(Window), C.POINTER(Window),
                         C.POINTER(C.POINTER(Window)), C.POINTER(C.c_uint)]
x.XFetchName.argtypes = [C.c_void_p, Window, C.POINTER(C.c_void_p)]
x.XFree.argtypes = [C.c_void_p]
x.XInternAtom.argtypes = [C.c_void_p, C.c_char_p, C.c_int]
x.XInternAtom.restype = C.c_ulong
x.XGetWindowProperty.argtypes = [C.c_void_p, Window, C.c_ulong, C.c_long, C.c_long, C.c_int,
                                 C.c_ulong, C.POINTER(C.c_ulong), C.POINTER(C.c_int),
                                 C.POINTER(C.c_ulong), C.POINTER(C.c_ulong), C.POINTER(C.c_void_p)]
x.XStringToKeysym.argtypes = [C.c_char_p]
x.XStringToKeysym.restype = C.c_ulong
x.XKeysymToKeycode.argtypes = [C.c_void_p, C.c_ulong]
x.XKeysymToKeycode.restype = C.c_ubyte
x.XkbKeycodeToKeysym.argtypes = [C.c_void_p, C.c_ubyte, C.c_int, C.c_int]
x.XkbKeycodeToKeysym.restype = C.c_ulong
x.XSendEvent.argtypes = [C.c_void_p, Window, C.c_int, C.c_long, C.POINTER(Event)]
x.XFlush.argtypes = [C.c_void_p]
x.XCloseDisplay.argtypes = [C.c_void_p]
root = x.XDefaultRootWindow(display)
pid_atom = x.XInternAtom(display, b"_NET_WM_PID", 0)


def title(window):
    name = C.c_void_p()
    if not x.XFetchName(display, window, C.byref(name)) or not name.value:
        return ""
    result = C.string_at(name).decode("utf-8", errors="replace")
    x.XFree(name)
    return result


def owned_window(pid, window=root):
    actual, size, count, remaining, value = C.c_ulong(), C.c_int(), C.c_ulong(), C.c_ulong(), C.c_void_p()
    x.XGetWindowProperty(display, window, pid_atom, 0, 1, 0, 0, C.byref(actual), C.byref(size),
                         C.byref(count), C.byref(remaining), C.byref(value))
    if value.value:
        owner = C.cast(value, C.POINTER(C.c_ulong))[0] if count.value else 0
        x.XFree(value)
        if owner == pid and title(window).startswith("Godollo"):
            return window
    parent, tree_root, children, child_count = Window(), Window(), C.POINTER(Window)(), C.c_uint()
    if not x.XQueryTree(display, window, C.byref(tree_root), C.byref(parent), C.byref(children), C.byref(child_count)):
        return None
    descendants = list(children[:child_count.value])
    if children:
        x.XFree(children)
    for child in descendants:
        found = owned_window(pid, child)
        if found:
            return found
    return None


def wait_for(predicate, message, timeout=12):
    deadline = time.monotonic() + timeout
    while True:
        result = predicate()
        if result:
            return result
        if game.poll() is not None or time.monotonic() > deadline:
            raise RuntimeError(message + "\n" + log.read_text())
        time.sleep(.025)


def key(name):
    symbol = x.XStringToKeysym(name.encode())
    code = x.XKeysymToKeycode(display, symbol)
    if not code:
        raise RuntimeError("Cannot map X11 key: " + name)
    shift = int(x.XkbKeycodeToKeysym(display, code, 0, 0) != symbol)
    # GLFW/XIM uses timestamps to suppress duplicate key presses. Real X11
    # events never have time zero; use a nonzero timestamp for synthetic input.
    timestamp = int(time.monotonic() * 1000) & 0xffffffff
    for kind, mask in ((2, 1), (3, 2)):
        event = Event()
        event.key = Key(kind, 0, 1, display, window, root, 0, timestamp, 10, 10, 10, 10, shift, code, 1)
        x.XSendEvent(display, window, 0, mask, C.byref(event))
    x.XFlush(display)
    time.sleep(.04)


def close_window():
    event = Event()
    event.message.type = 33
    event.message.display = display
    event.message.window = window
    event.message.message_type = x.XInternAtom(display, b"WM_PROTOCOLS", 0)
    event.message.format = 32
    event.message.data[0] = x.XInternAtom(display, b"WM_DELETE_WINDOW", 0)
    x.XSendEvent(display, window, 0, 0, C.byref(event))
    x.XFlush(display)


def capture(name):
    if shutil.which("import"):
        subprocess.run(["import", "-window", str(window), str(directory / (name + ".png"))],
                       check=True, timeout=10)


with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as sock:
    sock.bind(("127.0.0.1", 0))
    port = sock.getsockname()[1]
base = [asset, "--no-trees", "--zombie-count", "0"]
log = directory / "game.log"
children = []
with log.open("w") as output:
    # Desktop input methods may consume synthetic XSendEvent text without
    # delivering GLFW character callbacks. Use local XIM for this ASCII test.
    game = subprocess.Popen([client, *base, "--port", str(port), "--no-audio", "--look", "0", "-10"],
                            stdout=output, stderr=subprocess.STDOUT,
                            env=dict(os.environ, XMODIFIERS="@im=none"))
    children.append(game)
    try:
        window = wait_for(lambda: owned_window(game.pid), "Game window not found")
        wait_for(lambda: "Choose a game mode" in title(window), "Initial menu missing")
        capture("menu")
        key("4")
        wait_for(lambda: "| Character creation" in title(window), "Character creation screen did not open")
        time.sleep(.15)
        capture("character-creation")
        key("Escape")
        wait_for(lambda: "Choose a game mode" in title(window), "Character creation did not restore menu")
        for cycle in range(2):
            key("1")
            wait_for(lambda: "| Player" in title(window), "Single-player did not become playable")
            if cycle == 0:
                time.sleep(.15)
                capture("single-player")
            key("Escape")
            wait_for(lambda: "Choose a game mode" in title(window), "Leaving single-player did not restore menu")
        key("4")
        wait_for(lambda: "| Character creation" in title(window), "Character preview could not reopen after gameplay")
        time.sleep(.15)
        capture("character-after-session")
        key("Escape")
        wait_for(lambda: "Choose a game mode" in title(window), "Reopened character preview did not restore menu")
        key("2")
        wait_for(lambda: "| Player" in title(window), "Hosting did not become playable")
        remote = subprocess.Popen([client, *base, "--connect", f"127.0.0.1:{port}", "--headless", "--seconds", "10"],
                                  stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
        children.append(remote)
        wait_for(lambda: "Joined player 2" in log.read_text(), "Remote client did not join integrated host")
        key("Escape")
        wait_for(lambda: "Choose a game mode" in title(window), "Host leave did not restore menu")
        text, _ = remote.communicate(timeout=8)
        if remote.returncode == 0 or "Host ended the session" not in text:
            raise RuntimeError("Remote missed host shutdown:\n" + text)
        server_log = directory / "server.log"
        with server_log.open("w") as server_output:
            dedicated = subprocess.Popen([server, *base, "--port", str(port)], stdout=server_output, stderr=subprocess.STDOUT)
            children.append(dedicated)
            wait_for(lambda: "Listening on UDP" in server_log.read_text(), "Dedicated server failed")
            # Port-in-use startup must recover without losing the menu/window.
            key("2")
            wait_for(lambda: "Cannot listen on UDP" in log.read_text(), "Host startup error was not reported")
            wait_for(lambda: "Choose a game mode" in title(window), "Startup failure lost menu")
            capture("startup-error")
            # Edit the join address through the real menu field.
            key("Tab")
            key("Tab")
            for _ in "127.0.0.1:27020":
                key("BackSpace")
            for char in f"127.0.0.1:{port}":
                key({".": "period", ":": "colon"}.get(char, char))
            key("Return")
            key("3")
            wait_for(lambda: "| Player" in title(window), "Join mode did not become playable")
            wait_for(lambda: "Joined player 1" in server_log.read_text(), "Join did not reach dedicated server")
            dedicated.kill()
            dedicated.wait()
            wait_for(lambda: "Connection lost: server stopped responding" in log.read_text(), "Host loss did not report timeout")
            wait_for(lambda: "Choose a game mode" in title(window), "Host loss did not restore menu")
            capture("disconnect")
        # A fresh hosted session must still work; close the actual window in play.
        key("2")
        wait_for(lambda: "| Player" in title(window), "Retry after disconnect failed")
        close_window()
        if game.wait(timeout=10):
            raise RuntimeError("Closing the application failed:\n" + log.read_text())
        released = subprocess.run([server, *base, "--port", str(port), "--seconds", ".1"],
                                  capture_output=True, text=True, timeout=8)
        if released.returncode:
            raise RuntimeError("Closed game retained its listener:\n" + released.stdout + released.stderr)
        print("Menu, repeated sessions, host/join, startup failure, disconnect recovery and window close passed.")
    finally:
        for process in children:
            if process.poll() is None:
                process.kill()
                process.wait()
        x.XCloseDisplay(display)
