#!/usr/bin/env python3
"""Verify karta-map lifetime and overview rendering in isolated two-output Hyprland.

Requires Hyprland, hyprctl, grim, Pillow, cc and wayland-client headers. Saves screenshots and never reloads
or dispatches into the user's current desktop.
"""
import argparse
import json
import os
from pathlib import Path
import subprocess
import tempfile
import time
from PIL import Image, ImageChops

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--plugin', type=Path, default=Path(__file__).resolve().parents[1] / 'Debug/karta.so')
args = parser.parse_args()
plugin = args.plugin.resolve()
assert plugin.is_file(), plugin
run = Path(tempfile.mkdtemp(prefix='karta-map-'))
print(f'Artifacts: {run}', flush=True)
pointer = run / 'virtual-pointer-click'
subprocess.run(['cc', '-Wall', '-Wextra', str(Path(__file__).parent / 'lib/virtual-pointer-click.c'),
                '-o', str(pointer), *subprocess.check_output(['pkg-config', '--cflags', '--libs', 'wayland-client'], text=True).split()], check=True)
config = run / 'hyprland.lua'
config.write_text('hl.plugin.load(' + json.dumps(str(plugin)) + ')\n'
                  'hl.config({general={layout="karta"},input={follow_mouse=0},'
                  'animations={enabled=false},misc={disable_hyprland_logo=true,disable_splash_rendering=true}})\n')
env = dict(os.environ)
env.pop('HYPRLAND_INSTANCE_SIGNATURE', None)
instance = socket = None


def ctl(*args):
    assert instance and instance != os.environ.get('HYPRLAND_INSTANCE_SIGNATURE')
    return subprocess.check_output(['hyprctl', '-i', instance, *args], text=True)


def lua(code, delay=.15):
    result = ctl('eval', code)
    assert not result.lower().startswith('error'), result
    time.sleep(delay)


def command(name, argument=None):
    lua('hl.plugin.karta.' + name + '(' + (json.dumps(argument) if argument is not None else '') + ')')


def screenshot(name, output='MAP-A'):
    path = run / (name + '.png')
    subprocess.run(['grim', '-o', output, str(path)], env=dict(env, WAYLAND_DISPLAY=socket), check=True)
    return Image.open(path).convert('RGB')


def gold_count(image):
    # Selected cell's gold border is unique to this small top-right map region.
    w, h = image.size
    return sum(r > 130 and g > 100 and b < 110 and r > b * 1.5
               for r, g, b in image.crop((max(0, w - 300), 0, w, min(h, 310))).get_flattened_data())


def check(condition, description):
    assert condition, description
    print('PASS ' + description, flush=True)


def repository():
    path = Path(os.environ.get('XDG_RUNTIME_DIR', '/tmp')) / f'karta-{instance}.state'
    return path.read_text()


with (run / 'hyprland.log').open('w') as log:
    process = subprocess.Popen(['Hyprland', '-c', str(config)], env=env, stdout=log, stderr=log)
    try:
        for _ in range(100):
            instances = json.loads(subprocess.check_output(['hyprctl', 'instances', '-j'], text=True))
            found = next((i for i in instances if i['pid'] == process.pid), None)
            if found:
                instance, socket = found['instance'], found['wl_socket']
                break
            assert process.poll() is None, 'Nested compositor exited; inspect hyprland.log'
            time.sleep(.1)
        assert instance
        print(f'Nested instance: {instance}', flush=True)
        time.sleep(.5)
        for name, mode, position, scale in [('MAP-A', '1000x700@60', '0x0', 1),
                                             ('MAP-B', '900x1200@60', '1000x0', 1.5)]:
            ctl('output', 'create', 'headless', name)
            lua(f'hl.monitor({{output="{name}",mode="{mode}",position="{position}",scale={scale}}})')
        for monitor in json.loads(ctl('monitors', '-j')):
            if monitor['name'] not in ['MAP-A', 'MAP-B']:
                ctl('output', 'remove', monitor['name'])
        lua('hl.dispatch(hl.dsp.focus({monitor="MAP-A"}))')
        check(not ctl('configerrors').strip(), 'configuration loads')
        command('switch_workspace', 'r')
        check(gold_count(screenshot('desktop-map')) > 60, 'switch shows desktop map')
        check(gold_count(screenshot('desktop-other', 'MAP-B')) == 0, 'HUD appears only on action monitor')
        time.sleep(1.7)
        check(gold_count(screenshot('desktop-expired')) == 0, 'HUD disappears without input or stale pixels')
        command('switch_workspace', 'l')
        time.sleep(1.7)
        initial = repository()
        command('toggle_overview')
        time.sleep(1.7)
        check(gold_count(screenshot('overview-persistent')) > 60, 'overview map survives HUD deadline')
        # Initial single-karta map has its upper creation slot at logical
        # (903, 69). A real virtual pointer tests the cancellable input hook.
        subprocess.run([str(pointer), socket, '903', '69', '1600', '800'], check=True)
        time.sleep(.2)
        clicked = screenshot('overview-click-new')
        command('cancel_overview')
        command('toggle_overview')
        command('focus_monitor', 'u')
        keyboard = screenshot('overview-preview-up')
        difference = ImageChops.difference(clicked.crop((828, 16, 984, 174)),
                                           keyboard.crop((828, 16, 984, 174)))
        # Small compositor dithering / cursor-shadow differences are harmless;
        # a different cell position, label, or selection outline is not.
        check(sum(max(pixel) > 16 for pixel in difference.get_flattened_data()) < 10,
              'clicking new slot matches keyboard karta preview')
        # The current page is the lower cell of the two-row preview map.
        subprocess.run([str(pointer), socket, '903', '113', '1600', '800'], check=True)
        time.sleep(.2)
        command('toggle_overview', 'accept')
        check(repository() == initial, 'clicking current karta returns preview without creating a page')
        command('toggle_overview')
        command('focus_monitor', 'u')
        check(repository() == initial, 'preview does not switch or persist a karta')
        command('cancel_overview')
        check(gold_count(screenshot('overview-cancelled')) == 0, 'cancel removes map without showing a switch HUD')
        check(repository() == initial, 'cancel retains original karta')
        command('toggle_overview')
        command('focus_monitor', 'd')
        command('toggle_overview', 'accept')
        check(gold_count(screenshot('overview-accepted')) == 0, 'accept dismisses overview map without starting a HUD')
        command('switch_workspace', '1')
        check(gold_count(screenshot('direct-switch-after-overview')) > 60, 'direct switch still shows desktop HUD')
        command('toggle_overview')
        command('cancel_overview')
        check(gold_count(screenshot('cancel-during-hud')) == 0, 'cancel clears a pre-existing desktop HUD timer')
        command('switch_workspace', 'r')
        command('toggle_overview')
        command('toggle_overview', 'accept')
        check(gold_count(screenshot('accept-during-hud')) == 0, 'accept clears a pre-existing desktop HUD timer')
        command('switch_workspace', '1')
        time.sleep(1.7)
        lua('hl.dispatch(hl.dsp.focus({monitor="MAP-B"}))')
        command('toggle_overview')
        portrait = screenshot('overview-scaled-portrait', 'MAP-B')
        # Geometry is monitor logical space, rendered into scaled physical pixels.
        check(gold_count(portrait) > 60, 'map renders on scaled portrait output')
        lua('hl.config({plugin={karta={karta_map=0}}})')
        check(gold_count(screenshot('overview-disabled', 'MAP-B')) == 0, 'configuration disables map during overview')
        lua('hl.config({plugin={karta={karta_map=1}}})')
        check(gold_count(screenshot('overview-enabled', 'MAP-B')) > 60, 'configuration restores overview map')
        command('cancel_overview')
        lua('hl.config({general={layout="dwindle"}})')
        ctl('plugin', 'unload', str(plugin))
        check(gold_count(screenshot('unloaded', 'MAP-B')) == 0, 'unload clears overlay')
        check(process.poll() is None, 'isolated compositor remains healthy')
    finally:
        if instance and process.poll() is None:
            try:
                ctl('eval', 'hl.dispatch(hl.dsp.exit())')
            except subprocess.CalledProcessError:
                pass
        try:
            process.wait(timeout=8)
        except subprocess.TimeoutExpired:
            process.terminate()
            process.wait(timeout=5)
