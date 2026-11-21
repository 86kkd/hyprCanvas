#!/usr/bin/env python3
"""Regress stack relayout after window removal in an isolated Hyprland instance.

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
run = Path(tempfile.mkdtemp(prefix='hyprscroller-stack-removal.'))
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
        for mode,back,axis,span in [('row','l',1,900),('column','u',0,1200)]:
            launch('neighbor','#aa2222')
            command('set_mode("'+mode+'")')
            for index in range(3):
                launch('card-'+str(index),'#2222aa')
                if index:
                    command('move_window("'+back+'")')
            focus('card-0')
            subprocess.run(['kitty','@','--to','unix:'+str(run/'card-2.sock'),'close-window'],check=True)
            time.sleep(1)
            before = {name: (client(name)['at'], client(name)['size']) for name in ['card-0','card-1']}
            print('AFTER CLOSE', mode, before, flush=True)
            check(all(abs(size[axis]+4-span/2)<=2 for at,size in before.values()),mode+' deletion immediately restores two half-size windows')
            command('set_mode("'+mode+'")')
            after = {name: (client(name)['at'], client(name)['size']) for name in ['card-0','card-1']}
            check(before == after,mode+' forced relayout does not change remaining stack geometry')
            # Returning from a temporary lane must undo its page offset for
            # every stack, including the unfocused multi-window stack.
            focus('neighbor')
            lane_before = {name: (client(name)['at'], client(name)['size'])
                           for name in ['neighbor', 'card-0', 'card-1']}
            leave, back_to_lane = ('u', 'd') if mode == 'row' else ('l', 'r')
            command('move_focus("'+leave+'")')
            command('move_focus("'+back_to_lane+'")')
            lane_after = {name: (client(name)['at'], client(name)['size'])
                          for name in ['neighbor', 'card-0', 'card-1']}
            check(lane_before == lane_after,
                  mode+' returning from empty lane restores every stack: '+str(lane_after))
            focus('card-0')
            # Remove the neighbor first, then close an unfocused sibling. No
            # focus event should be needed to expand the last remaining window.
            subprocess.run(['kitty','@','--to','unix:'+str(run/'neighbor.sock'),'close-window'],check=True)
            time.sleep(.3)
            subprocess.run(['kitty','@','--to','unix:'+str(run/'card-1.sock'),'close-window'],check=True)
            time.sleep(.7)
            survivor = client('card-0')
            check(survivor['at'] == [2, 2] and survivor['size'] == [1196, 896],
                  mode+' last window fills the lane without a focus change: '+str((survivor['at'], survivor['size'])))
            for name in ['card-0']:
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
