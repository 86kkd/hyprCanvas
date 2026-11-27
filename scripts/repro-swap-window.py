#!/usr/bin/env python3
"""Regress within-stack moves and adjacent-stack swaps in isolated Hyprland.

Requires Hyprland, hyprctl and kitty. Does not reload the desktop plugin.
"""
import argparse
import json
import os
from pathlib import Path
import subprocess
import tempfile
import time

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--plugin', type=Path, default=Path(__file__).resolve().parents[1] / 'Debug/hyprCanvas.so')
args = parser.parse_args()
plugin = args.plugin.resolve()
assert plugin.is_file(), f'Missing plugin: {plugin}'
run = Path(tempfile.mkdtemp(prefix='hyprcanvas-swap-window.'))
print(f'Artifacts: {run}', flush=True)
config = run / 'hyprland.lua'
env = dict(os.environ)
env.pop('HYPRLAND_INSTANCE_SIGNATURE', None)
instance = None
failures = []


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


def check(condition, description):
    print(('PASS ' if condition else 'FAIL ') + description, flush=True)
    if not condition:
        failures.append(description)


def launch(name, color):
    command = f'kitty --class {name} --override allow_remote_control=yes --listen-on unix:{run}/{name}.sock --override background={color} sh -c \"sleep 300\"'
    dispatch('hl.dsp.exec_cmd(' + json.dumps(command) + ')')
    for _ in range(80):
        clients = json.loads(ctl('clients', '-j'))
        if any(c['class'] == name for c in clients):
            time.sleep(.6)
            subprocess.run(['kitty','@','--to','unix:'+str(run/(name+'.sock')),'set-window-title','--match','all',name],check=True)
            time.sleep(.6)
            return
        time.sleep(.1)
    raise RuntimeError(f'Window failed to map: {name}')


config.write_text('hl.plugin.load(' + json.dumps(str(plugin)) + ')\n' + 'hl.config({general={layout="canvas"},input={follow_mouse=0},animations={enabled=true},misc={disable_hyprland_logo=true,disable_splash_rendering=true}})\n')
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
        assert instance
        time.sleep(1)
        check(not ctl('configerrors').strip(), 'configuration loads')
        ctl('output', 'create', 'headless', 'STACK-TEST')
        lua('hl.monitor({output="STACK-TEST",mode="1200x900@60",position="0x0",scale=1})')
        time.sleep(.5)
        for mon in json.loads(ctl('monitors', '-j')):
            if mon['name'] != 'STACK-TEST':
                ctl('output', 'remove', mon['name'])
        dispatch('hl.dsp.focus({monitor="STACK-TEST"})')
        lua('hl.config({animations={enabled=false},general={gaps_in=0,gaps_out=0}})')
        def focus(name):
            dispatch('hl.dsp.focus({window="class:^'+name+'$"})')
            time.sleep(.2)
        def client(name):
            return next(c for c in json.loads(ctl('clients','-j')) if c['class']==name)
        def command(expr):
            lua('hl.plugin.canvas.'+expr)
            time.sleep(.3)
            print(expr, json.loads(ctl('activewindow','-j')).get('class'), [(c['class'],c['at'],c['size']) for c in json.loads(ctl('clients','-j'))],flush=True)
        def geometry():
            return {c['class']: (c['at'], c['size']) for c in json.loads(ctl('clients', '-j'))}

        def active_name():
            return json.loads(ctl('activewindow', '-j')).get('class')

        def assert_swap(before, source, target, label):
            after = geometry()
            check(after[source] == before[target] and after[target] == before[source],
                  label + ' exchanges slot geometry')
            check(all(after[name] == box for name, box in before.items() if name not in (source, target)),
                  label + ' leaves siblings unchanged')
            check(active_name() == source, label + ' keeps focus on the moved window')

        for mode, back, forward, within in [('row', 'l', 'r', 'd'), ('column', 'u', 'd', 'r')]:
            launch('left-0', '#aa2222')
            command('set_mode("'+mode+'")')
            for name in ['left-1', 'left-2', 'right-0', 'right-1']:
                launch(name, '#2222aa')
                if name != 'right-0':
                    command('move_window("'+back+'")')
            # In-stack moves must promote the target rather than retain an
            # unrelated secondary from focus history. Reverse each move to
            # restore list order before testing the other direction.
            cross_axis = 0 if mode == 'row' else 1
            within_back = 'u' if mode == 'row' else 'l'
            for expanded in [False, True]:
                focus('left-1')
                if expanded:
                    command('toggle_fullscreen()')
                for direction, reverse, target, old_secondary in [
                    (within_back, within, 'left-0', 'left-2'),
                    (within, within_back, 'left-2', 'left-0'),
                ]:
                    focus(old_secondary)
                    focus('left-1')
                    secondary_size = client(old_secondary)['size'][cross_axis]
                    check(client(target)['size'][cross_axis] < secondary_size,
                          mode + ' move target starts as a non-secondary card')
                    command('move_window("'+direction+'")')
                    check(active_name() == 'left-1', mode + ' in-stack move preserves primary focus')
                    check(client(target)['size'][cross_axis] == secondary_size and
                          client(old_secondary)['size'][cross_axis] < secondary_size,
                          mode + ' ' + direction + ' promotes move target to secondary expanded=' + str(expanded))
                    command('move_window("'+reverse+'")')
                    check(client(target)['size'][cross_axis] == secondary_size,
                          mode + ' reverse move retains target as secondary')
                if expanded:
                    command('toggle_fullscreen()')

            # Remember a non-last target window, then return to the source.
            # Reset the earlier scenario's secondary history to the predecessor:
            # layout snapshots restore that fallback, not previousActive.
            focus('left-0')
            focus('right-0')
            focus('left-1')
            before = geometry()
            command('swap_window("'+forward+'")')
            assert_swap(before, 'left-1', 'right-0', mode)
            command('swap_window("'+back+'")')
            check(geometry() == before, mode + ' reverse swap restores all slots')

            for argument in [within, back, 'begin', 'end', 'invalid']:
                command('swap_window("'+argument+'")')
                check(geometry() == before and active_name() == 'left-1',
                      mode + ' no-op for ' + argument)
            command('swap_window()')
            check(geometry() == before, mode + ' missing argument is a no-op')
            command('toggle_overview()')
            command('swap_window("'+forward+'")')
            command('cancel_overview()')
            check(geometry() == before, mode + ' overview does not swap real windows')

            # Expanded state stays on the stack; slot sizes and restore geometry
            # must survive swapping a normal window into an expanded deck.
            command('toggle_fullscreen()')
            expanded = geometry()
            command('swap_window("'+forward+'")')
            after = geometry()
            check(after['left-1'][1] == expanded['right-0'][1] and
                  after['right-0'][1] == expanded['left-1'][1],
                  mode + ' expanded and ordinary slot sizes are inherited')
            command('swap_window("'+back+'")')
            check(geometry() == expanded, mode + ' expanded reverse swap restores slots')
            command('toggle_fullscreen()')
            check(geometry() == before, mode + ' fullsize restore geometry survives exchange')

            # Native fullscreen/maximized targets are owned by Hyprland.
            for native_mode in ['fullscreen', 'maximized']:
                toggle = 'hl.dsp.window.fullscreen({mode="'+native_mode+'",action="toggle"})'
                dispatch(toggle)
                time.sleep(.3)
                check(client('left-1')['fullscreen'] != 0, mode + ' enters native ' + native_mode)
                native = geometry()
                command('swap_window("'+forward+'")')
                check(geometry() == native and active_name() == 'left-1',
                      mode + ' native ' + native_mode + ' blocks exchange')
                dispatch(toggle)
                time.sleep(.3)

            # Exercise ownership caches after swapping, then expel the moved
            # window and close every window to catch stale membership pointers.
            command('swap_window("'+forward+'")')
            exchanged = geometry()
            lua('hl.config({general={layout="dwindle"}})')
            time.sleep(.3)
            lua('hl.config({general={layout="canvas"}})')
            time.sleep(.5)
            check(geometry() == exchanged and active_name() == 'left-1',
                  mode + ' layout snapshot restores exchanged membership and focus')
            command('move_window("'+forward+'")')
            check(active_name() == 'left-1' and len(geometry()) == 5,
                  mode + ' swapped window can split out without losing members')
            for name in ['left-1', 'right-0', 'left-2', 'left-0', 'right-1']:
                subprocess.run(['kitty', '@', '--to', 'unix:'+str(run/(name+'.sock')), 'close-window'], check=True)
                time.sleep(.2)
            check(not geometry(), mode + ' closing exchanged windows clears the lane')

            launch('single-left', '#aa2222')
            command('set_mode("'+mode+'")')
            launch('single-right', '#2222aa')
            singles = geometry()
            command('swap_window("'+back+'")')
            assert_swap(singles, 'single-right', 'single-left', mode + ' single members')
            for name in ['single-left', 'single-right']:
                subprocess.run(['kitty', '@', '--to', 'unix:'+str(run/(name+'.sock')), 'close-window'], check=True)
                time.sleep(.2)
        lua('hl.config({general={layout="dwindle"}})')
        ctl('plugin','unload',str(plugin))
        check(process.poll() is None,'plugin unload succeeds')
        (run/'result.json').write_text(json.dumps({'failures':failures},indent=2))
    finally:
        if instance:
            try:
                dispatch('hl.dsp.exit()')
            except Exception:
                pass
        try:
            process.wait(timeout=10)
        except subprocess.TimeoutExpired:
            process.terminate()
            process.wait(timeout=10)
if failures:
    raise SystemExit(1)
