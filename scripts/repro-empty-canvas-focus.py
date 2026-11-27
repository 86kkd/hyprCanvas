#!/usr/bin/env python3
"""Regress kitty placement after switching from a populated canvas to an empty one."""
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
run = Path(tempfile.mkdtemp(prefix='hyprcanvas-empty-canvas-'))
print(f'Artifacts: {run}', flush=True)
config = run / 'hyprland.lua'
config.write_text('hl.plugin.load(' + json.dumps(str(plugin)) + ')\n'
                  'hl.config({general={layout="canvas"},input={follow_mouse=0},'
                  'animations={enabled=true},misc={disable_hyprland_logo=true,disable_splash_rendering=true}})\n')
env = dict(os.environ)
env.pop('HYPRLAND_INSTANCE_SIGNATURE', None)
instance = None


def ctl(*words):
    assert instance and instance != os.environ.get('HYPRLAND_INSTANCE_SIGNATURE')
    return subprocess.check_output(['hyprctl', '-i', instance, *words], text=True)


def lua(code):
    result = ctl('eval', code)
    assert not result.lower().startswith('error'), result
    time.sleep(.15)


def command(name, arg=None):
    lua('hl.plugin.canvas.' + name + '(' + (json.dumps(arg) if arg is not None else '') + ')')


def state():
    path = Path(os.environ.get('XDG_RUNTIME_DIR', '/tmp')) / f'hyprcanvas-{instance}.state'
    canvases = []
    active = None
    for line in path.read_text().splitlines():
        words = line.split()
        if words[0] == 'ACTIVE':
            active = int(words[1])
        if words[0] == 'CANVAS':
            canvases.append(tuple(map(int, words[1:])))
    return active, canvases


def check(condition, description):
    assert condition, description
    print('PASS ' + description, flush=True)


with (run / 'hyprland.log').open('w') as log:
    process = subprocess.Popen(['Hyprland', '-c', str(config)], env=env, stdout=log, stderr=log)
    try:
        for _ in range(100):
            instances = json.loads(subprocess.check_output(['hyprctl', 'instances', '-j'], text=True))
            found = next((i for i in instances if i['pid'] == process.pid), None)
            if found:
                instance = found['instance']
                break
            assert process.poll() is None, 'Nested Hyprland failed to start'
            time.sleep(.1)
        assert instance
        print(f'Nested instance: {instance}', flush=True)
        time.sleep(.5)
        check(not ctl('configerrors').strip(), 'configuration loads')
        # Keep the real Wayland output: unlike a headless output it can stop
        # producing frames while idle, exposing HUD scheduling bugs.
        monitor = json.loads(ctl('monitors', '-j'))[0]
        lua('hl.dispatch(hl.dsp.focus({monitor=' + json.dumps(monitor['name']) + '}))')
        def launch(name):
            lua('hl.dispatch(hl.dsp.exec_cmd(' + json.dumps('kitty --class ' + name + ' sh -c "sleep 120"') + '))')
            for _ in range(100):
                window = next((c for c in json.loads(ctl('clients', '-j')) if c['class'] == name), None)
                if window:
                    return window
                time.sleep(.1)
            raise AssertionError('kitty did not map: ' + name)
        first = launch('canvas-origin-kitty')
        time.sleep(.6)
        hsctl = Path(__file__).resolve().parent / 'hsctl'
        subprocess.run([str(hsctl), '-i', instance, 'workspace', 'd'], check=True)
        # Do not screenshot or inject input while waiting: either can wake
        # rendering and conceal a stale HUD on an otherwise idle output.
        time.sleep(2)
        plugin_log = Path.home() / '.hyprland/plugins/hyprCanvas/hyprCanvas.log'
        check('canvas_minimap: expired' in plugin_log.read_text(),
              'HUD timer expires without input, screenshots, or client activity')
        current = next(m for m in json.loads(ctl('monitors', '-j')) if m['focused'])
        target = current['activeWorkspace']['id']
        focus = json.loads(ctl('activewindow', '-j'))
        print('Source workspace:', first['workspace']['id'], 'Target workspace:', target, 'Focus:', focus.get('workspace'), flush=True)
        check(target != first['workspace']['id'], 'desktop switched to a new canvas workspace')
        check(not focus or focus.get('workspace', {}).get('id') == target,
              'empty canvas does not retain keyboard focus on an off-screen window')
        subprocess.run([str(hsctl), '-i', instance, 'kitty'], check=True)
        second = None
        for _ in range(100):
            second = next((c for c in json.loads(ctl('clients', '-j')) if c['pid'] != first['pid']), None)
            if second:
                break
            time.sleep(.1)
        check(second is not None, 'hsctl kitty creates a client after hsctl workspace d')
        check(second['workspace']['id'] == target, 'new kitty maps on the new temporary workspace')
        check(state()[1][-1][-1] == 0, 'new tiled kitty promotes the temporary canvas')
    finally:
        if instance:
            try:
                ctl('eval', 'hl.dispatch(hl.dsp.exit())')
            except subprocess.CalledProcessError:
                pass
        try:
            process.wait(timeout=8)
        except subprocess.TimeoutExpired:
            process.terminate()
            process.wait(timeout=5)
