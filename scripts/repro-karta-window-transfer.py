#!/usr/bin/env python3
"""Exercise karta window transfer and focus history in an isolated compositor.

Requires Hyprland, hyprctl and kitty. Leaves the outer session untouched.
Records IPC focus events so a transient refocus cannot hide behind a correct
final activewindow. Covers four directions, populated/empty targets, no_warps,
follow_mouse, source focus restoration, rapid transfers and invalid requests.
"""
import argparse
import json
import os
from pathlib import Path
import socket
import subprocess
import tempfile
import time

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--plugin', type=Path, default=Path(__file__).resolve().parents[1] / 'Debug/karta.so')
args = parser.parse_args()
plugin = args.plugin.resolve()
assert plugin.is_file()
run = Path(tempfile.mkdtemp(prefix='karta-transfer-'))
print(f'Artifacts: {run}', flush=True)
config = run / 'hyprland.lua'
config.write_text('hl.plugin.load(' + json.dumps(str(plugin)) + ')\n'
                  'hl.config({general={layout="karta"},input={follow_mouse=0},'
                  'misc={disable_hyprland_logo=true,disable_splash_rendering=true}})\n')
env = dict(os.environ)
env.pop('HYPRLAND_INSTANCE_SIGNATURE', None)
instance = None
trace = []


def ctl(*words):
    assert instance and instance != os.environ.get('HYPRLAND_INSTANCE_SIGNATURE')
    return subprocess.check_output(['hyprctl', '-i', instance, *words], text=True)


def lua(code):
    result = ctl('eval', code)
    assert not result.lower().startswith('error'), result


def command(name, direction=None):
    lua('hl.plugin.karta.' + name + '(' + (json.dumps(direction) if direction is not None else '') + ')')
    time.sleep(.25)


def query(name):
    return json.loads(ctl(name, '-j'))


def visible():
    return {m['name']: m['activeWorkspace']['id'] for m in query('monitors')}


def check(value, description):
    assert value, description
    print('PASS ' + description, flush=True)


def focus(address):
    lua('hl.dispatch(hl.dsp.focus({window=' + json.dumps('address:' + address) + '}))')
    time.sleep(.25)


def launch(name):
    lua('hl.dispatch(hl.dsp.exec_cmd(' + json.dumps('kitty --class ' + name) + '))')
    for _ in range(100):
        client = next((c for c in query('clients') if c['class'] == name), None)
        if client:
            time.sleep(.4)
            return client['address']
        time.sleep(.1)
    raise RuntimeError('window did not map: ' + name)


def drain(events):
    data = b''
    while True:
        try:
            chunk = events.recv(65536)
        except BlockingIOError:
            break
        if not chunk:
            break
        data += chunk
    return data.decode().splitlines()


with (run / 'hyprland.log').open('w') as log:
    process = subprocess.Popen(['Hyprland', '-c', str(config)], env=env, stdout=log, stderr=log)
    try:
        for _ in range(100):
            instances = json.loads(subprocess.check_output(['hyprctl', 'instances', '-j'], text=True))
            found = next((i for i in instances if i['pid'] == process.pid), None)
            if found:
                instance = found['instance']
                break
            assert process.poll() is None, 'nested compositor exited'
            time.sleep(.1)
        assert instance
        time.sleep(.5)
        check(not ctl('configerrors').strip(), 'configuration loads')
        for name in ('TRANSFER-1', 'TRANSFER-2'):
            ctl('output', 'create', 'headless', name)
        lua('hl.monitor({output="TRANSFER-1",mode="1280x720",position="0x0",scale=1})')
        lua('hl.monitor({output="TRANSFER-2",mode="1280x720",position="1280x0",scale=1})')
        time.sleep(.5)
        for m in query('monitors'):
            if m['name'] not in ('TRANSFER-1', 'TRANSFER-2'):
                ctl('output', 'remove', m['name'])
        lua('hl.dispatch(hl.dsp.focus({monitor="TRANSFER-2"}))')
        peer_a = launch('transfer-peer-a')
        peer_b = launch('transfer-peer-b')
        focus(peer_a)
        lua('hl.dispatch(hl.dsp.focus({monitor="TRANSFER-1"}))')
        source_a = launch('transfer-source-a')
        source_b = launch('transfer-source-b')
        moving = launch('transfer-moving')
        home = visible()
        monitor_id = query('activewindow')['monitor']
        events = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        events.connect(str(Path(os.environ['XDG_RUNTIME_DIR']) / 'hypr' / instance / '.socket2.sock'))
        events.setblocking(False)
        for direction, back in [('r','l'), ('d','u'), ('l','r'), ('u','d')]:
            # First visit populates the destination and then returns to origin.
            command('switch_workspace', direction)
            target = launch('transfer-target-' + direction)
            destination = visible()
            command('switch_workspace', back)
            check(visible() == home, direction + ': source karta restored')
            for no_warps, follow_mouse in [(False, 0), (True, 0), (True, 1)]:
                lua('hl.config({cursor={no_warps=' + str(no_warps).lower() + '},input={follow_mouse=' + str(follow_mouse) + '}})')
                focus(moving)
                before = {w['id']: w['lastwindow'] for w in query('workspaces')}
                drain(events)
                command('move_window_to_workspace', direction)
                time.sleep(.5)
                emitted = drain(events)
                trace.append({'direction': direction, 'no_warps': no_warps, 'follow_mouse': follow_mouse, 'events': emitted})
                active = query('activewindow')
                check(active.get('address') == moving and active.get('monitor') == monitor_id,
                      direction + ': moved window retains focus and physical monitor')
                check(visible() == destination, direction + ': all monitors follow target karta')
                focus_events = [e.split('>>', 1)[1] for e in emitted if e.startswith('activewindowv2>>')]
                check(len(focus_events) <= 1 and all(e.lower() == moving.removeprefix('0x').lower() for e in focus_events),
                      direction + ': no transient focus on another window: ' + repr(focus_events))
                pointer = query('cursorpos')
                check(active['at'][0] <= pointer['x'] < active['at'][0] + active['size'][0]
                      and active['at'][1] <= pointer['y'] < active['at'][1] + active['size'][1],
                      direction + ': deferred pointer synchronization stays inside moved window')
                history = {w['id']: w['lastwindow'] for w in query('workspaces')}
                check(history[home['TRANSFER-1']] in (source_a, source_b), direction + ': source remembers a remaining window')
                check(history[home['TRANSFER-2']] == before[home['TRANSFER-2']], direction + ': peer monitor focus history unchanged')
                remembered = history[home['TRANSFER-1']]
                command('switch_workspace', back)
                check(query('activewindow').get('address') == remembered, direction + ': returning restores source selection')
                command('switch_workspace', direction)
                check(query('activewindow').get('address') == moving, direction + ': returning to target restores moved window')
                command('move_window_to_workspace', back)
                check(visible() == home and query('activewindow').get('address') == moving,
                      direction + ': reverse transfer follows original window')
        for invalid in ('begin', 'end', 'invalid', '1'):
            before = visible(), query('activewindow')['address']
            command('move_window_to_workspace', invalid)
            check((visible(), query('activewindow')['address']) == before, 'invalid direction is a no-op: ' + invalid)
        command('toggle_overview')
        before = visible(), query('activewindow')['address']
        command('move_window_to_workspace', 'r')
        check((visible(), query('activewindow')['address']) == before, 'transfer disabled during overview')
        command('cancel_overview')
        # Rapid round trip must leave focus and deferred pointer work on the final window.
        drain(events)
        lua('hl.plugin.karta.move_window_to_workspace("r"); hl.plugin.karta.move_window_to_workspace("l")')
        time.sleep(.6)
        check(visible() == home and query('activewindow').get('address') == moving, 'rapid round trip preserves focus')
        # Moving beyond the rightmost neighbor creates a fresh karta. Moving
        # again then exercises removal of the only window from that karta.
        command('move_window_to_workspace', 'r')
        command('move_window_to_workspace', 'r')
        solo_source = visible()
        check(query('activewindow').get('address') == moving, 'transfer follows into newly created karta')
        source_ws = query('activewindow')['workspace']['id']
        check(len([w for w in query('clients') if w['workspace']['id'] == source_ws]) == 1,
              'empty-source regression starts with exactly one window')
        drain(events)
        command('move_window_to_workspace', 'r')
        time.sleep(.5)
        emitted = drain(events)
        trace.append({'scenario': 'last-source-window', 'events': emitted})
        check(query('activewindow').get('address') == moving, 'moving final source window preserves focus')
        check(query('activewindow')['monitor'] == monitor_id, 'new karta retains physical monitor')
        focus_events = [e.split('>>', 1)[1] for e in emitted if e.startswith('activewindowv2>>')]
        check(len(focus_events) <= 1 and all(e == moving.removeprefix('0x') for e in focus_events),
              'last source window causes no empty/background focus transition')
        command('switch_workspace', 'l')
        check(visible() == solo_source and not query('activewindow').get('address'), 'empty source has no stale focused window')
        empty_before = visible()
        command('move_window_to_workspace', 'u')
        check(visible() == empty_before, 'empty source transfer is a no-op')
        command('switch_workspace', 'r')
        check(query('activewindow').get('address') == moving, 'return from empty source restores moved window')
        lua('hl.dispatch(hl.dsp.window.float({action="toggle"}))')
        time.sleep(.2)
        before = visible(), query('activewindow')['workspace']['id']
        command('move_window_to_workspace', 'r')
        check((visible(), query('activewindow')['workspace']['id']) == before, 'floating window is a no-op')
        lua('hl.dispatch(hl.dsp.window.float({action="toggle"}))')
        lua('hl.dispatch(hl.dsp.workspace.toggle_special("transfer-check"))')
        special = launch('transfer-special')
        before = visible(), query('activewindow')['workspace']
        command('move_window_to_workspace', 'r')
        check((visible(), query('activewindow')['workspace']) == before
              and query('activewindow').get('address') == special, 'special workspace window is a no-op')
        print('All karta transfer checks passed', flush=True)
    finally:
        (run / 'focus-events.json').write_text(json.dumps(trace, indent=2))
        if instance and process.poll() is None:
            try:
                lua('hl.dispatch(hl.dsp.exit())')
            except (subprocess.SubprocessError, AssertionError):
                pass
        try:
            process.wait(timeout=10)
        except subprocess.TimeoutExpired:
            process.terminate()
            process.wait(timeout=10)
