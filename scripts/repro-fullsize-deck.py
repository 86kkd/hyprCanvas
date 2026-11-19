#!/usr/bin/env python3
"""Regress focused-window fullsize decks in an isolated Hyprland instance.

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
parser.add_argument('--plugin', type=Path, default=Path(__file__).resolve().parents[1] / 'Debug/hyprscroller.so')
args = parser.parse_args()
plugin = args.plugin.resolve()
assert plugin.is_file(), f'Missing plugin: {plugin}'
run = Path(tempfile.mkdtemp(prefix='hyprscroller-fullsize-deck.'))
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
            launch('card-0','#aa2222')
            command('set_mode("'+mode+'")')
            count=1
            for target_count in [2,3,5]:
                while count<target_count:
                    launch('card-'+str(count),'#2222aa')
                    command('move_window("'+back+'")')
                    count+=1
                # Save actual ordinary geometry to check fullsize round trips.
                before={c['class']:c['size'] for c in json.loads(ctl('clients','-j'))}
                command('toggle_fullscreen()')
                for index in range(count):
                    focus('card-'+str(index))
                    clients=json.loads(ctl('clients','-j'))
                    main=client('card-'+str(index))
                    check(abs(main['size'][axis]+4-span*.9)<=2, 'main occupies 90% '+mode+' count='+str(count)+' focus='+str(index))
                    # The main has no inset and spans the complete other axis.
                    cross=1200 if mode=='row' else 900
                    check(abs(main['size'][1-axis]+4-cross)<=2,'main spans monitor cross axis '+mode)
                    check(min(c['at'][axis] for c in clients)>=0 and max(c['at'][axis]+c['size'][axis] for c in clients)<=span,'all backgrounds remain inside monitor '+mode)
                subprocess.run(['grim','-o','STACK-TEST',str(run/(mode+'-'+str(count)+'.png'))],env=dict(env,WAYLAND_DISPLAY=socket),check=True)
                if count == 3:
                    other_mode = 'column' if mode == 'row' else 'row'
                    other_span = 1200 if mode == 'row' else 900
                    command('set_mode("'+other_mode+'")')
                    main=client('card-'+str(count-1))
                    check(abs(main['size'][1-axis]+4-other_span*.9)<=2,'fullsize survives direction change '+mode)
                    command('set_mode("'+mode+'")')
                focus('card-'+str(count-1))
                command('toggle_fullscreen()')
                after={c['class']:c['size'] for c in json.loads(ctl('clients','-j'))}
                check(all(abs(after[name][axis]-size[axis])<=2 for name,size in before.items()),'exiting fullsize restores normal window sizes '+mode+' count='+str(count))
            # Closing down to one member must leave a usable fullsize window.
            command('toggle_fullscreen()')
            for index in range(count-1):
                subprocess.run(['kitty','@','--to','unix:'+str(run/('card-'+str(index)+'.sock')),'close-window'],check=True)
                time.sleep(.2)
            last=client('card-'+str(count-1))
            check(abs(last['size'][axis]+4-span)<=2,'last remaining fullsize member fills screen '+mode)
            subprocess.run(['kitty','@','--to','unix:'+str(run/('card-'+str(count-1)+'.sock')),'close-window'],check=True)
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
