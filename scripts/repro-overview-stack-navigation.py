#!/usr/bin/env python3
"""Regress overview stack navigation in an isolated Hyprland instance.

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
parser.add_argument('--plugin', type=Path, default=Path(__file__).resolve().parents[1] / 'Debug/karta.so')
args = parser.parse_args()
plugin = args.plugin.resolve()
assert plugin.is_file(), f'Missing plugin: {plugin}'
run = Path(tempfile.mkdtemp(prefix='karta-overview-navigation.'))
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


config.write_text('hl.plugin.load(' + json.dumps(str(plugin)) + ')\n' + 'hl.config({general={layout="karta"},input={follow_mouse=0},animations={enabled=true},misc={disable_hyprland_logo=true,disable_splash_rendering=true}})\n')
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
            lua('hl.plugin.karta.'+expr)
            time.sleep(.3)
            print(expr, json.loads(ctl('activewindow','-j')).get('class'), [(c['class'],c['at'],c['size']) for c in json.loads(ctl('clients','-j'))],flush=True)
        for mode,back,forward in [('row','l','r'),('column','u','d')]:
            launch('neighbor-before','#aa2222')
            command('set_mode("'+mode+'")')
            for index in range(3):
                launch('card-'+str(index),'#2222aa')
                if index:
                    command('move_window("'+back+'")')
            launch('neighbor-after','#22aa22')
            # Enter/re-enter the stack through overview, then leave on the first
            # orthogonal direction. Every sibling must route to the neighbor.
            for index in range(3):
                for direction,expected in [(back,'neighbor-before'),(forward,'neighbor-after')]:
                    focus('card-'+str(index))
                    command('toggle_overview()')
                    command('move_focus("'+direction+'")')
                    command('toggle_overview()')
                    selected=json.loads(ctl('activewindow','-j')).get('class')
                    check(selected==expected,mode+' first '+direction+' from card-'+str(index)+' selects '+expected+' (got '+str(selected)+')')
            # The stack-item axis must never select another card in overview.
            within = ('u', 'd') if mode == 'row' else ('l', 'r')
            for index in range(3):
                for direction in within:
                    name = 'card-' + str(index)
                    focus(name)
                    command('toggle_overview()')
                    command('move_focus("'+direction+'")')
                    command('toggle_overview()')
                    selected = json.loads(ctl('activewindow','-j')).get('class')
                    check(selected == name, mode+' '+direction+' stays on active card '+name)
                focus('neighbor-before')
                command('toggle_overview()')
                command('move_focus("'+forward+'")')
                command('toggle_overview()')
                selected = json.loads(ctl('activewindow','-j')).get('class')
                check(selected == name, mode+' entering stack restores '+name)
            for name in ['card-0','card-1','card-2','neighbor-before','neighbor-after']:
                subprocess.run(['kitty','@','--to','unix:'+str(run/(name+'.sock')),'close-window'],check=True)
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
