#!/usr/bin/env python3
"""Hyprland 0.57 Lua regression: isolated compositor, two headless outputs.

Requires hyprctl, Hyprland, kitty, grim and Pillow. Never reloads the outer
plugin or switches outer workspaces. A nested output exists briefly at startup;
it is removed as soon as the two headless outputs are ready.
"""
import argparse
import json
import os
from pathlib import Path
import subprocess
import tempfile
import time
from PIL import Image

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--plugin', type=Path, default=Path(__file__).resolve().parents[1] / 'Debug/hyprscroller.so')
parser.add_argument('--reload-plugin', action='store_true', help='Check plugin unload/reload preserves monitor/window membership')
parser.add_argument('--no-warps', action='store_true', help='Also verify keyboard focus with cursor.no_warps enabled')
args = parser.parse_args()
plugin = args.plugin.resolve()
assert plugin.is_file(), f'Missing plugin: {plugin}'
run = Path(tempfile.mkdtemp(prefix='hyprscroller-shared-workspace.'))
print(f'Artifacts: {run}', flush=True)
config = run / 'hyprland.lua'
bindings = Path(__file__).resolve().parents[1] / 'examples/scroller-workspaces.lua'
config.write_text('hl.plugin.load(' + json.dumps(str(plugin)) + ')\n' + 'hl.config({general={layout="scroller"}, misc={disable_hyprland_logo=true,disable_splash_rendering=true}})\n' + 'dofile(' + json.dumps(str(bindings)) + ')\n' + ('hl.config({cursor={no_warps=true},input={follow_mouse=0}})\n' if args.no_warps else ''))
env = dict(os.environ)
env.pop('HYPRLAND_INSTANCE_SIGNATURE', None)
instance = None
failures = []
focus_result = {}


def ctl(*words):
    assert instance and instance != os.environ.get('HYPRLAND_INSTANCE_SIGNATURE')
    result = subprocess.check_output(['hyprctl', '-i', instance, *words], text=True)
    if result.startswith('error'):
        raise RuntimeError(result)
    return result


def lua(code):
    return ctl('eval', code)


def dispatch(code):
    lua(f'hl.dispatch({code})')


def focus_workspace(ws):
    dispatch('hl.dsp.focus({workspace=' + json.dumps(str(ws)) + '})')
    time.sleep(.4)


def overview():
    lua('hl.plugin.scroller.toggle_overview()')
    time.sleep(1)


def check(condition, description):
    print(('PASS ' if condition else 'FAIL ') + description, flush=True)
    if not condition:
        failures.append(description)


def workspaces():
    return {m['name']: m['activeWorkspace']['id'] for m in json.loads(ctl('monitors', '-j'))}


def launch(name, color):
    command = f'kitty --class {name} --override background={color}'
    dispatch('hl.dsp.exec_cmd(' + json.dumps(command) + ')')
    for _ in range(80):
        clients = json.loads(ctl('clients', '-j'))
        if any(c['class'] == name for c in clients):
            time.sleep(.6)
            return
        time.sleep(.1)
    raise RuntimeError(f'Window failed to map: {name}')


def screenshot(name, output, channel):
    path = run / f'{name}.png'
    subprocess.run(['grim', '-o', output, str(path)], env=dict(env, WAYLAND_DISPLAY=socket), check=True)
    sample = Image.open(path).convert('RGB').resize((160, 90))
    pixels = [sample.getpixel((x, y)) for y in range(90) for x in range(160)]
    fraction = sum(p[channel] > 80 and all(p[channel] > 1.8*p[c] for c in range(3) if c != channel) for p in pixels)/len(pixels)
    check(fraction > .02, f'{name}: window pixels visible ({fraction:.3f})')


with (run / 'hyprland.log').open('w') as log:
    process = subprocess.Popen(['Hyprland', '-c', str(config)], env=env, stdout=log, stderr=subprocess.STDOUT)
    try:
        for _ in range(100):
            instances = json.loads(subprocess.check_output(['hyprctl', 'instances', '-j'], text=True))
            found = next((i for i in instances if i['pid'] == process.pid), None)
            if found:
                instance, socket = found['instance'], found['wl_socket']
                break
            if process.poll() is not None:
                raise RuntimeError('Nested Hyprland exited during startup')
            time.sleep(.1)
        assert instance, 'Nested instance not found'
        print(f'Nested instance: {instance}', flush=True)
        time.sleep(1)
        check(not ctl('configerrors').strip(), 'Lua configuration and workspace bindings load without errors')
        check(any(b.get('description') == 'switch whole canvas workspace' for b in json.loads(ctl('binds', '-j'))), 'canvas keyboard bindings registered')
        ctl('output', 'create', 'headless', 'TEST-1')
        ctl('output', 'create', 'headless', 'TEST-2')
        for output in ('TEST-1', 'TEST-2'):
            lua('hl.monitor({output=' + json.dumps(output) + ',mode="preferred",position="auto",scale=1})')
        time.sleep(.5)
        for monitor in json.loads(ctl('monitors', '-j')):
            if monitor['name'] not in ('TEST-1', 'TEST-2'):
                ctl('output', 'remove', monitor['name'])
        lua('hl.config({general={layout="scroller"}})')
        dispatch('hl.dsp.focus({monitor="TEST-1"})')
        focus_workspace(101)
        launch('shared-a', '#aa2222')
        dispatch('hl.dsp.focus({monitor="TEST-2"})')
        focus_workspace(102)
        launch('shared-b', '#22aa22')
        focus_workspace(101)
        overview()
        screenshot('baseline-secondary', 'TEST-2', 1)
        overview()
        if args.reload_plugin:
            membership = {(c['address'], c['workspace']['id'], c['monitor']) for c in json.loads(ctl('clients', '-j'))}
            visible = workspaces()
            lua('hl.config({general={layout="dwindle"}})')
            ctl('plugin', 'unload', str(plugin))
            ctl('plugin', 'load', str(plugin))
            lua('hl.config({general={layout="scroller"}})')
            time.sleep(.7)
            check(workspaces() == visible, 'plugin reload preserves visible monitor workspaces')
            check({(c['address'], c['workspace']['id'], c['monitor']) for c in json.loads(ctl('clients', '-j'))} == membership,
                  'plugin reload preserves windows and workspace ownership')
        focus_workspace(101)
        # Record a second canvas sharing the secondary workspace, then return.
        focus_workspace(103)
        launch('shared-c', '#2222aa')
        overview()
        overview()
        focus_workspace(101)
        overview()
        screenshot('three-workspaces-primary', 'TEST-1', 0)
        screenshot('three-workspaces-secondary', 'TEST-2', 1)
        overview()
        # Opening from the shared window must not select its older canvas.
        focus_workspace(103)
        focus_workspace(102)
        before = workspaces()
        overview()
        screenshot('shared-origin-primary', 'TEST-1', 2)
        screenshot('shared-origin-secondary', 'TEST-2', 1)
        overview()
        check(workspaces() == before, 'overview accept preserves the current canvas when origin is shared')
        state_path = Path(os.environ.get('XDG_RUNTIME_DIR', '/tmp')) / ('hyprscroller-canvas-' + instance + '.state')
        state_lines = state_path.read_text().splitlines()
        canvas_ids = sorted(int(line.split()[1]) for line in state_lines if line.startswith('CANVAS '))
        active_canvas = next(int(line.split()[1]) for line in state_lines if line.startswith('ACTIVE '))
        before_number = str(canvas_ids.index(active_canvas) + 1)
        members = [int(line.split()[2]) for line in state_lines if line.startswith('MEMBER ') and line.split()[3] == '0']
        check(len(members) == len(set(members)), 'legacy canvases no longer share native workspaces')
        # Explicit canvas creation switches both monitors, then restores both.
        lua('hl.plugin.scroller.switch_workspace("r")')
        time.sleep(.8)
        after = workspaces()
        check(all(after[m] != before[m] for m in before), 'direct canvas switch changes every monitor')
        lua('hl.plugin.scroller.switch_workspace("l")')
        time.sleep(.8)
        check(workspaces() == before, 'direct canvas switch restores every monitor')
        lua('hl.plugin.scroller.switch_workspace("10")')
        time.sleep(.6)
        numbered = workspaces()
        check(all(numbered[m] != before[m] for m in before), 'numbered canvas switches all monitors')
        lua('hl.plugin.scroller.switch_workspace(' + json.dumps(before_number) + ')')
        time.sleep(.6)
        check(workspaces() == before, 'numbered canvas restores its monitor members')
        lua('hl.plugin.scroller.switch_workspace("1")')
        time.sleep(.5)
        legacy_first = workspaces()
        lua('hl.plugin.scroller.switch_workspace("2")')
        time.sleep(.5)
        legacy_second = workspaces()
        check(all(legacy_first[m] != legacy_second[m] for m in legacy_first),
              'switching between migrated legacy canvases changes BOTH monitors')
        lua('hl.plugin.scroller.switch_workspace(' + json.dumps(before_number) + ')')
        time.sleep(.5)
        check(workspaces() == before, 'return from legacy canvases restores original visible members')
        # Directional window moves may change physical monitor but never create
        # another native workspace or move into a hidden canvas.
        known = {w['id'] for w in json.loads(ctl('workspaces', '-j'))}
        for direction in ('l', 'r', 'r', 'r', 'l'):
            lua(f'hl.plugin.scroller.move_window("{direction}")')
            time.sleep(.2)
        clients = json.loads(ctl('clients', '-j'))
        current = workspaces()
        moved = next(c for c in clients if c['class'] == 'shared-b')
        check(moved['workspace']['id'] in current.values(), 'window movement stays in the visible canvas')
        check({w['id'] for w in json.loads(ctl('workspaces', '-j'))} <= known, 'window movement creates no workspace')
        if args.no_warps:
            # A fresh canvas gives the source exactly one window. Plugin focus
            # synchronization must follow it to TEST-2 despite no_warps.
            lua('hl.plugin.scroller.switch_workspace("5")')
            time.sleep(.5)
            dispatch('hl.dsp.focus({monitor="TEST-1"})')
            launch('focus-source', '#aa2222')
            dispatch('hl.dsp.focus({monitor="TEST-2"})')
            launch('focus-peer', '#22aa22')
            created = json.loads(ctl('activewindow', '-j'))
            pointer = json.loads(ctl('cursorpos', '-j'))
            check(created.get('class') == 'focus-peer', 'new window receives keyboard focus')
            check(created['at'][0] <= pointer['x'] < created['at'][0] + created['size'][0]
                  and created['at'][1] <= pointer['y'] < created['at'][1] + created['size'][1],
                  'new window synchronizes pointer despite no_warps')
            dispatch('hl.dsp.focus({monitor="TEST-1"})')
            time.sleep(.3)
            source = next(m for m in json.loads(ctl('monitors', '-j')) if m['name'] == 'TEST-1')
            dispatch('hl.dsp.cursor.move({x=' + str(source['x'] + 100) + ',y=' + str(source['y'] + 100) + '})')
            time.sleep(.2)
            cursor_before = json.loads(ctl('cursorpos', '-j'))
            lua('hl.plugin.scroller.move_window("r")')
            time.sleep(.6)
            active = json.loads(ctl('activewindow', '-j'))
            target = next(m for m in json.loads(ctl('monitors', '-j')) if m['name'] == 'TEST-2')
            check(cursor_before['x'] < target['x'] or cursor_before['x'] >= target['x'] + target['width']/target['scale'],
                  'regression starts with pointer outside destination monitor')
            check(active.get('class') == 'focus-source' and active.get('monitor') == target['id'],
                  'cross-monitor move keeps keyboard focus on transferred window')
            cursor_after = json.loads(ctl('cursorpos', '-j'))
            check(active['at'][0] <= cursor_after['x'] < active['at'][0] + active['size'][0]
                  and active['at'][1] <= cursor_after['y'] < active['at'][1] + active['size'][1],
                  'keyboard navigation synchronizes pointer into destination despite no_warps')
            lua('hl.plugin.scroller.move_focus("d")')
            time.sleep(.4)
            active = json.loads(ctl('activewindow', '-j'))
            check(active.get('monitor') == target['id'], 'next keyboard focus action operates on destination monitor')
            # Isolate navigation from the empty-lane scenario above.
            lua('hl.plugin.scroller.switch_workspace("6")')
            dispatch('hl.dsp.focus({monitor="TEST-2"})')
            launch('rapid-a', '#aa2222')
            launch('rapid-b', '#22aa22')
            lua('hl.plugin.scroller.move_focus("end"); hl.plugin.scroller.move_focus("begin")')
            time.sleep(.4)
            latest = json.loads(ctl('activewindow', '-j'))
            pointer = json.loads(ctl('cursorpos', '-j'))
            print('Rapid focus geometry:', latest.get('at'), latest.get('size'), pointer, flush=True)
            check(latest['at'][0] <= pointer['x'] < latest['at'][0] + latest['size'][0]
                  and latest['at'][1] <= pointer['y'] < latest['at'][1] + latest['size'][1],
                  'rapid navigation synchronizes only the final target')
            px, py = target['x'] + 20, target['y'] + 20
            lua('hl.plugin.scroller.move_focus("u"); hl.dispatch(hl.dsp.cursor.move({x='
                + str(px) + ',y=' + str(py) + '}))')
            time.sleep(.4)
            pointer = json.loads(ctl('cursorpos', '-j'))
            check(pointer['x'] == px and pointer['y'] == py,
                  'pointer movement supersedes pending keyboard synchronization')
            active = json.loads(ctl('activewindow', '-j'))
            focus_result = {'cursorBefore': cursor_before, 'cursorAfter': json.loads(ctl('cursorpos', '-j')), 'activeAfterNavigation': active, 'targetMonitor': target['id']}
            # Reverse direction too, with the pointer deliberately on the right.
            dispatch('hl.dsp.cursor.move({x=' + str(target['x'] + 100) + ',y=' + str(target['y'] + 100) + '})')
            expected_class = active.get('class')
            for _ in range(6):
                lua('hl.plugin.scroller.move_window("l")')
                time.sleep(.2)
                active = json.loads(ctl('activewindow', '-j'))
                if active.get('monitor') == source['id']:
                    break
            check(active.get('class') == expected_class and active.get('monitor') == source['id'],
                  'reverse cross-monitor move follows the same window')
            lua('hl.plugin.scroller.move_focus("u")')
            time.sleep(.3)
            check(json.loads(ctl('activewindow', '-j')).get('monitor') == source['id'],
                  'reverse move subsequent keyboard action stays on destination')
        # Exercise scale changes on live windows, using logical rather than
        # framebuffer bounds for containment.
        for layout, canvas in [('scroller', '8')]:
            lua('hl.plugin.scroller.switch_workspace(' + json.dumps(canvas) + ')')
            lua('hl.config({general={layout=' + json.dumps(layout) + '}})')
            dispatch('hl.dsp.focus({monitor="TEST-2"})')
            launch('scale-' + layout + '-a', '#aa2222')
            launch('scale-' + layout + '-b', '#22aa22')
            for scale in (1, 1.25, 2, 1):
                lua('hl.monitor({output="TEST-2",mode="preferred",position="auto",scale=' + str(scale) + '})')
                time.sleep(.6)
                for direction in ('begin', 'end'):
                    lua('hl.plugin.scroller.move_focus(' + json.dumps(direction) + ')')
                    time.sleep(.4)
                    win = json.loads(ctl('activewindow', '-j'))
                    mon = next(m for m in json.loads(ctl('monitors', '-j')) if m['id'] == win['monitor'])
                    width, height = mon['width'], mon['height']
                    if mon['transform'] % 2:
                        width, height = height, width
                    check(win['at'][0] >= mon['x'] - 1 and win['at'][1] >= mon['y'] - 1
                          and win['at'][0] + win['size'][0] <= mon['x'] + width / mon['scale'] + 1
                          and win['at'][1] + win['size'][1] <= mon['y'] + height / mon['scale'] + 1,
                          f'{layout} scale={scale} {direction}: focused window fits logical output')
        (run / 'result.json').write_text(json.dumps({'failures': failures, 'focus': focus_result, 'before': before, 'after': after, 'clients': clients}, indent=2))
    finally:
        if instance:
            try:
                dispatch('hl.dsp.exit()')
            except (OSError, subprocess.SubprocessError, RuntimeError):
                pass
        try:
            process.wait(timeout=10)
        except subprocess.TimeoutExpired:
            process.terminate()
            process.wait(timeout=10)
if failures:
    raise SystemExit(1)
