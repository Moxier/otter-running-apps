#!/usr/bin/env python3
"""Black-box C ABI + real Unix-socket test. Does not need niri or Wayland."""
import ctypes as C
import json
import os
from pathlib import Path
import socket
import tempfile
import time

class Rect(C.Structure):
    _fields_ = [('x', C.c_int32), ('y', C.c_int32), ('width', C.c_uint32), ('height', C.c_uint32)]
class Constraints(C.Structure):
    _fields_ = [('area', Rect), ('clip', Rect)]
class Event(C.Structure):
    _fields_ = [('kind', C.c_uint32), ('hit_data', C.c_uint64), ('text', C.c_char_p), ('text_len', C.c_uint32), ('reserved', C.c_uint32)]
class Manifest(C.Structure):
    _fields_ = [('id', C.c_char_p), ('name', C.c_char_p), ('version', C.c_char_p), ('abi', C.c_uint32), ('bar', C.c_uint8), ('launcher', C.c_uint8), ('rows', C.c_uint8), ('settings', C.c_uint8)]
LOG = C.CFUNCTYPE(None, C.c_void_p, C.c_int, C.c_char_p)
FRAME = C.CFUNCTYPE(None, C.c_void_p)
ITEM = C.CFUNCTYPE(C.c_int, C.c_void_p, C.c_char_p, C.POINTER(Rect), C.c_char_p, C.c_uint32, C.c_char_p, C.c_uint32, C.c_uint64)
WIDTH = C.CFUNCTYPE(C.c_uint32, C.c_void_p, C.POINTER(Constraints))
DRAW = C.CFUNCTYPE(C.c_int, C.c_void_p, C.c_void_p, C.POINTER(Constraints))
DESTROY = C.CFUNCTYPE(None, C.c_void_p)
EVENT = C.CFUNCTYPE(C.c_int, C.c_void_p, C.POINTER(Event))
class Host(C.Structure):
    _fields_ = [('abi', C.c_uint32), ('userdata', C.c_void_p), ('log', C.c_void_p), ('request_frame', FRAME), ('rect', C.c_void_p), ('label', C.c_void_p), ('hit', C.c_void_p), ('text_input', C.c_void_p), ('bar_item', ITEM)]
class Vtable(C.Structure):
    _fields_ = [('width', WIDTH), ('draw', DRAW), ('destroy', DESTROY), ('event', EVENT)]
class Instance(C.Structure):
    _fields_ = [('vtable', C.POINTER(Vtable)), ('data', C.c_void_p)]

lib = C.CDLL(str(Path('build/plugins/running-apps/libotter_plugin_running_apps.so').resolve()))
lib.otter_plugin_init.argtypes = [C.POINTER(Host)]
lib.otter_plugin_query.argtypes = [C.POINTER(Manifest)]
lib.otter_plugin_bar_attach.argtypes = [C.POINTER(Host), C.c_char_p]
lib.otter_plugin_bar_attach.restype = C.POINTER(Instance)
items, frames, badges, badge_hits = [], [], [], []
@ITEM
def item(frame, node, rect, text, font, icon, state, hit):
    items.append((node, icon, state, hit, rect.contents.x, rect.contents.width))
    return 0
@FRAME
def request(_):
    frames.append(True)
RECT = C.CFUNCTYPE(C.c_int, C.c_void_p, C.c_char_p, C.POINTER(Rect), C.c_uint32)
LABEL = C.CFUNCTYPE(C.c_int, C.c_void_p, C.c_char_p, C.POINTER(Rect), C.c_char_p, C.c_uint32, C.c_uint32)
HIT = C.CFUNCTYPE(C.c_int, C.c_void_p, C.c_char_p, C.POINTER(Rect), C.c_uint64)
@RECT
def badge_rect(frame, node, rect, color):
    assert node.endswith(b"-count-bg") and rect.contents.width == 14
    return 0
@LABEL
def badge_label(frame, node, rect, text, font, color):
    badges.append(text)
    return 0
@HIT
def badge_hit(frame, node, rect, hit):
    badge_hits.append(hit)
    return 0
logs = []
@LOG
def log(userdata, level, message):
    logs.append((level, message))
host = Host(1, None, C.cast(log, C.c_void_p), request, C.cast(badge_rect, C.c_void_p), C.cast(badge_label, C.c_void_p), C.cast(badge_hit, C.c_void_p), None, item)
constraints = Constraints(Rect(0, 0, 288, 32), Rect(0, 0, 288, 32))

def window(i, app, active=False, workspace=10):
    return {'id': i, 'app_id': app, 'is_focused': active, 'workspace_id': workspace}

with tempfile.TemporaryDirectory() as temp:
    os.environ['OTTER_RUNNING_APPS_ANIMATIONS'] = '1'  # Old setting cannot enable removed animation.
    os.environ['NIRI_SOCKET'] = temp + '/niri.sock'
    os.environ['XDG_DATA_HOME'] = temp
    os.environ['XDG_DATA_DIRS'] = temp
    apps = Path(temp, 'applications'); apps.mkdir()
    (apps / 'firefox.desktop').write_text('[Desktop Entry]\nType=Application\nIcon=firefox\n')
    server = socket.socket(socket.AF_UNIX); server.bind(os.environ['NIRI_SOCKET']); server.listen(); server.settimeout(1)
    assert lib.otter_plugin_abi_version() == 1
    manifest = Manifest(); assert lib.otter_plugin_query(C.byref(manifest)) == 0
    assert manifest.id == b'running-apps' and manifest.bar == 1
    wrong = Host(2, None, None, request, None, None, None, None, item)
    assert lib.otter_plugin_init(C.byref(wrong)) == -1
    no_icons = Host(1, None, None, request, None, None, None, None, ITEM())
    assert lib.otter_plugin_init(C.byref(no_icons)) == -1
    assert lib.otter_plugin_init(C.byref(host)) == 0
    instance = lib.otter_plugin_bar_attach(C.byref(host), b'test')
    assert instance
    instance = instance.contents; vt = instance.vtable.contents
    def draw():
        items.clear(); badges.clear(); badge_hits.clear()
        before = len(frames)
        assert vt.draw(instance.data, None, C.byref(constraints)) == 0
        assert len(frames) == before  # Rendering must never start an animation frame loop.
        return list(items)
    def click(hit):
        assert vt.event(instance.data, C.byref(Event(1, hit, None, 0, 0))) == 0
    def send(obj):
        events.sendall(json.dumps(obj).encode() + b'\n')
    draw(); events, _ = server.accept(); events.settimeout(1)
    assert events.recv(100) == b'"EventStream"\n'
    # Fragmented acknowledgement and snapshot, large IDs retain exact integer precision.
    events.sendall(b'{"Ok":'); assert draw() == []
    events.sendall(b'"Handled"}\n')
    send({'WindowsChanged': {'windows': [window(9007199254740993, 'firefox', True), window(42, 'firefox'), window(7, 'other')]}})
    assert draw() == []  # Never show all workspaces while the workspace snapshot is pending.
    send({'WorkspacesChanged': {'workspaces': [{'id': 10, 'idx': 1, 'output': 'DP-1', 'is_focused': True}, {'id': 20, 'idx': 2, 'output': 'DP-1', 'is_focused': False}]}})
    rendered = draw(); assert len(rendered) == 2
    assert [x[4] for x in rendered] == [108, 144]
    firefox = next(x for x in rendered if x[1] == b'firefox')
    assert firefox[2] == 1
    assert badges == [b'2'] and badge_hits == [firefox[3]]
    click(firefox[3]); action, _ = server.accept(); action.settimeout(1)
    assert json.loads(action.recv(200)) == {'Action': {'FocusWindow': {'id': 42}}}
    action.sendall(b'{"Ok":"Handled"}\n'); action.close(); draw()
    send({'WindowFocusChanged': {'id': 42}})
    rendered = draw(); firefox = next(x for x in rendered if x[1] == b'firefox')
    click(firefox[3]); action, _ = server.accept(); action.settimeout(1)
    assert json.loads(action.recv(200))['Action']['FocusWindow']['id'] == 9007199254740993
    action.sendall(b'{"Ok":"Handled"}\n'); action.close(); draw()
    # Reordering events change rendered icon order without requiring window reopen.
    send({'WindowLayoutsChanged': {'changes': [[7, {'pos_in_scrolling_layout': [1, 1]}], [42, {'pos_in_scrolling_layout': [2, 1]}], [9007199254740993, {'pos_in_scrolling_layout': [3, 1]}]]}})
    assert draw()[0][1] == b'sparkles'
    send({'WindowLayoutsChanged': {'changes': [[7, {'pos_in_scrolling_layout': [3, 1]}], [42, {'pos_in_scrolling_layout': [1, 1]}]]}})
    assert draw()[0][1] == b'firefox'
    # Snapshot is authoritative, unknown events do not destroy state.
    send({'FutureEvent': {'new': True}}); assert len(draw()) == 2
    send({'WindowsChanged': {'windows': [window(i, 'firefox', i == 0) for i in range(12)]}})
    assert len(draw()) == 1 and badges == [b'9+']
    send({'WindowsChanged': {'windows': [window(0, 'firefox', True)]}})
    assert len(draw()) == 1 and badges == []
    send({'WindowsChanged': {'windows': [window(i, f'app{i}', i == 0) for i in range(12)]}})
    rendered = draw(); assert len(rendered) == 8 and rendered[-1][0] == b'more'
    click(rendered[-1][3]); assert len(draw()) == 6
    assert [x[4] for x in items] == [36, 72, 108, 144, 180, 216]
    # Clip must suppress hidden hit targets.
    constraints.clip.width = 72; assert len(draw()) == 1
    constraints.clip.width = 288
    # Workspace switch clears the old page and centers only local groups/counts.
    send({'WindowsChanged': {'windows': [window(1, 'firefox', True), window(2, 'firefox', False, 20), window(3, 'other', False, 20), window(4, 'unknown', False, None)]}})
    rendered = draw(); assert len(rendered) == 1 and rendered[0][4] == 126 and badges == []
    old_hit = rendered[0][3]
    send({'WorkspaceActivated': {'id': 20, 'focused': True}})
    click(old_hit)  # Same app exists there, but the old workspace's hit is stale.
    import select
    assert not select.select([server], [], [], 0.05)[0]
    rendered = draw(); assert len(rendered) == 2 and [x[4] for x in rendered] == [108, 144]
    click(next(x for x in rendered if x[1] == b'firefox')[3])
    action, _ = server.accept(); action.settimeout(1)
    assert json.loads(action.recv(200))['Action']['FocusWindow']['id'] == 2
    action.sendall(b'{"Ok":"Handled"}\n'); action.close(); draw()
    send({'WorkspaceActivated': {'id': 10, 'focused': False}})
    assert len(draw()) == 2  # Activation on a different monitor does not change global focus.
    send({'WorkspaceActivated': {'id': 30, 'focused': True}})
    assert draw() == []  # Empty workspace.
    send({'WorkspaceActivated': {'id': 10, 'focused': True}})
    send({'WindowOpenedOrChanged': {'window': window(2, 'firefox', False, 10)}})
    assert len(draw()) == 1 and badges == [b'2']
    # Closed/disconnected state clears promptly; the old hit cannot focus a new app.
    stale = draw()[0][3]
    events.close(); assert draw() == []
    click(stale)
    time.sleep(1.02); draw(); events, _ = server.accept(); events.settimeout(1)
    assert events.recv(100) == b'"EventStream"\n'
    send({'WorkspacesChanged': {'workspaces': [{'id': 10, 'idx': 1, 'output': 'DP-1', 'is_focused': True}]}})
    send({'WindowsChanged': {'windows': [window(123, 'firefox')]}})
    assert len(draw()) == 1
    assert logs == []
    events.sendall(b'not-json\n'); assert draw() == []
    assert len(logs) == 1 and b'malformed JSON' in logs[-1][1]
    events.close()
    # Protocol errors retain the same delayed reconnect and snapshot recovery.
    time.sleep(1.02); draw(); events, _ = server.accept(); events.settimeout(1)
    assert events.recv(100) == b'"EventStream"\n'
    send({'WorkspacesChanged': {'workspaces': [{'id': 10, 'idx': 1, 'output': 'DP-1', 'is_focused': True}]}})
    send({'WindowsChanged': {'windows': [window(123, 'firefox')]}})
    rendered = draw(); assert len(rendered) == 1
    click(rendered[0][3]); action, _ = server.accept(); action.settimeout(1)
    assert action.recv(200)
    action.sendall(b'{}\n'); draw(); action.close()
    assert b'action reply processing failed: invalid action reply' in logs[-1][1]
    send({'WindowClosed': {'id': -1}}); assert draw() == []
    assert b'event processing failed: invalid window id' in logs[-1][1]
    events.close()
    vt.destroy(instance.data)
    lib.otter_plugin_deinit()
    server.close()
print('C ABI, socket framing, focus, paging, clipping, reconnect and unload tests passed')
