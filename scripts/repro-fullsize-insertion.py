#!/usr/bin/env python3
"""Regress fullsize join/split in an isolated Hyprland instance.

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
run = Path(tempfile.mkdtemp(prefix='hyprscroller-fullsize-insertion.'))
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
    command = f'kitty --class {name} --override allow_remote_control=yes --listen-on unix:{run}/{name}.sock --override background={color} sh -c \"sleep 120\"'
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


config.write_text('hl.plugin.load(' + json.dumps(str(plugin)) + ')\n' + 'hl.config({general={layout="scroller"},input={follow_mouse=0},animations={enabled=true},misc={disable_hyprland_logo=true,disable_splash_rendering=true}})\n')
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
            lua('hl.plugin.scroller.'+expr)
            time.sleep(.3)
            print(expr, json.loads(ctl('activewindow','-j')).get('class'), [(c['class'],c['at'],c['size']) for c in json.loads(ctl('clients','-j'))],flush=True)
        # In row mode stacks share the x axis; in column mode they share y.
        # Windows inside one stack differ on the opposite axis.
        for mode, back, forward, stack_axis, item_axis in [
            ('row', 'l', 'r', 0, 1),
            ('column', 'u', 'd', 1, 0),
        ]:
            launch('full-target','#aa2222')
            command('set_mode("'+mode+'")')
            launch('incoming','#22aa22')
            focus('full-target')
            original=client('full-target')['size'][stack_axis]
            command('toggle_fullscreen()')
            expanded=client('full-target')['size'][stack_axis]
            check(expanded>original,'fullsize expands target '+mode)
            focus('incoming')
            command('move_window("'+back+'")')
            incoming=client('incoming');target=client('full-target')
            check(abs(incoming['at'][stack_axis]-target['at'][stack_axis])<=2,
                  'incoming joins the fullsize stack on the stack axis '+mode)
            check(abs(incoming['at'][item_axis]-target['at'][item_axis])>2,
                  'joined fullsize deck keeps distinct window cards '+mode)
            check(max(incoming['size'][stack_axis], target['size'][stack_axis])==expanded,
                  'joined deck remains fullsize after admission '+mode)
            focus('incoming')
            command('move_window("'+forward+'")')
            incoming=client('incoming');target=client('full-target')
            check(abs(incoming['at'][stack_axis]-target['at'][stack_axis])>2,
                  'split leaves a separate stack beside the fullsize target '+mode)
            check(target['size'][stack_axis]==expanded,
                  'remaining fullsize stack stays expanded after split '+mode)
            check(incoming['size'][stack_axis]<expanded,
                  'expelled window restores its pre-join non-fullsize state '+mode)
            focus('incoming')
            command('move_window("'+back+'")')
            incoming=client('incoming');target=client('full-target')
            check(abs(incoming['at'][stack_axis]-target['at'][stack_axis])<=2,
                  'rejoin into the remaining fullsize stack '+mode)
            focus('full-target')
            command('toggle_fullscreen()')
            restored_axis = max(client('incoming')['size'][stack_axis], client('full-target')['size'][stack_axis])
            check(restored_axis < expanded,
                  'toggling fullsize off restores ordinary stack geometry '+mode)
            for name in ['incoming','full-target']:
                subprocess.run(['kitty','@','--to','unix:'+str(run/(name+'.sock')),'close-window'],check=True)
                time.sleep(.3)
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
