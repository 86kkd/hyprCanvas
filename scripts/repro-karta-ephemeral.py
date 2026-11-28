#!/usr/bin/env python3
"""Check temporary karta creation, promotion and cleanup in an isolated session."""
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
run = Path(tempfile.mkdtemp(prefix='karta-ephemeral-'))
print(f'Artifacts: {run}', flush=True)
config = run / 'hyprland.lua'
config.write_text('hl.plugin.load(' + json.dumps(str(plugin)) + ')\n'
                  'hl.config({general={layout="karta"},input={follow_mouse=0},'
                  'animations={enabled=false},misc={disable_hyprland_logo=true,disable_splash_rendering=true}})\n')
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
    lua('hl.plugin.karta.' + name + '(' + (json.dumps(arg) if arg is not None else '') + ')')


def state():
    path = Path(os.environ.get('XDG_RUNTIME_DIR', '/tmp')) / f'karta-{instance}.state'
    kartas = []
    active = None
    for line in path.read_text().splitlines():
        words = line.split()
        if words[0] == 'ACTIVE':
            active = int(words[1])
        if words[0] == 'KARTA':
            kartas.append(tuple(map(int, words[1:])))
    return active, kartas


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
        time.sleep(.5)
        check(not ctl('configerrors').strip(), 'configuration loads')
        for name, position in [('TEMP-A', '0x0'), ('TEMP-B', '800x0')]:
            ctl('output', 'create', 'headless', name)
            lua(f'hl.monitor({{output="{name}",mode="800x600@60",position="{position}",scale=1}})')
        for monitor in json.loads(ctl('monitors', '-j')):
            if monitor['name'] not in ['TEMP-A', 'TEMP-B']:
                ctl('output', 'remove', monitor['name'])
        lua('hl.dispatch(hl.dsp.focus({monitor="TEMP-A"}))')
        command('switch_workspace', 'r')
        initial = state()
        check(len(initial[1]) == 2 and initial[1][-1][-1] == 1, 'new karta remains temporary')
        for target in ['r', 'r', 'd', '10', '3']:
            command('switch_workspace', target)
            check(state() == initial, f'empty temporary karta cannot extend toward {target}')
        command('switch_workspace', 'l')
        check(len(state()[1]) == 1, 'leaving unused karta removes it')

        command('toggle_overview')
        command('focus_monitor', 'r')
        command('focus_monitor', 'r')
        command('focus_monitor', 'l')
        command('focus_monitor', 'u')
        command('toggle_overview', 'accept')
        active, kartas = state()
        check(len(kartas) == 2 and kartas[-1][1:3] == (0, -1), 'overview discards unused previews instead of committing a chain')
        command('switch_workspace', 'd')
        check(len(state()[1]) == 1, 'temporary upper karta is removed on return')
        command('toggle_overview')
        command('focus_monitor', 'r')
        command('cancel_overview')
        check(len(state()[1]) == 1, 'cancel does not persist an empty preview')

        command('switch_workspace', 'r')
        lua('hl.dispatch(hl.dsp.focus({monitor="TEMP-B"}))')
        lua('hl.dispatch(hl.dsp.exec_cmd("kitty --class karta-temporary-test sh -c \'sleep 120\'", {floating=true}))')
        for _ in range(100):
            clients = json.loads(ctl('clients', '-j'))
            window = next((c for c in clients if c['class'] == 'karta-temporary-test'), None)
            if window:
                break
            time.sleep(.1)
        assert window, 'test terminal did not map'
        check(state()[1][-1][-1] == 0, 'floating window on second monitor promotes whole karta')
        command('switch_workspace', 'r')
        check(len(state()[1]) == 3 and state()[1][-1][-1] == 1, 'populated karta can create its next temporary neighbor')
        # Move the existing window onto the blank karta rather than opening another.
        monitor = next(m for m in json.loads(ctl('monitors', '-j')) if m['name'] == 'TEMP-B')
        lua('hl.dispatch(hl.dsp.window.move({window="class:^karta-temporary-test$", workspace=' +
            json.dumps(str(monitor['activeWorkspace']['id'])) + ',follow=false}))')
        check(state()[1][-1][-1] == 0, 'moving a window into temporary karta promotes it')
        lua('hl.dispatch(hl.dsp.window.close({window="class:^karta-temporary-test$"}))')
        command('switch_workspace', '1')
        check(len(state()[1]) == 3 and all(c[-1] == 0 for c in state()[1]), 'formerly populated kartas remain after windows close')
        check([c[1:3] for c in state()[1]] == [(0, 0), (1, 0), (2, 0)], 'no empty temporary gap remains between retained kartas')
    finally:
        if instance:
            try:
                ctl('dispatch', 'hl.dsp.exit()')
            except subprocess.CalledProcessError:
                pass
        try:
            process.wait(timeout=8)
        except subprocess.TimeoutExpired:
            process.terminate()
            process.wait(timeout=5)
