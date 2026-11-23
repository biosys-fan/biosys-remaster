"""Machines used with an object in the hand (the audit's sweep clicks with empty hands, so these were never run):
- an extinguisher on a fire (CFire::OnApply dll:10006a00) read the extinguisher's type at +0x40; the object had no such member and the game
  stopped with "no member at object offset 64" (same in CFuseBox::OnApply dll:100272a0). Now the h2ouse animation is played and the fire goes out.
- the poem1 pickup on FPH6MIR_B triggers the milestone that wakes the baby synths (dll:100092d0 pokes +0x2c = 1): the poke of +0x2c crashed.
- goggles used on a charger (hotspot yPower of the charge pages): CFeature::OnApply is empty, the goggles' own handler recharges (dll:1001d3f0)."""
import os, shutil, subprocess, sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
os.chdir(ROOT)
D = 'build/test/machines'
shutil.rmtree(D, ignore_errors=True)
os.makedirs(D + '/save')
failed = 0


def check(name, ok, detail=''):
    global failed
    print(('PASS ' if ok else 'FAIL ') + name + ('' if ok else ' -- ' + str(detail)[:500]))
    failed += 0 if ok else 1


def run(script):
    env = dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy')
    r = subprocess.run(['build/biosys', '--game', '--skip-intro', '--seed', '3', '--mute', '--no-config', '--save-dir', D + '/save', '--log-events', '--log-api',
                        '--script', script], capture_output=True, text=True, env=env)
    return r.returncode, (r.stdout + r.stderr).splitlines()


rc, out = run('advance 1500; page CE7; advance 800; hold fireH2O1; advance 200; clickhs yCE7FIRE1; advance 600')
check('water extinguisher on a fire: no error', rc == 0 and not any('no member' in l for l in out), out[-3:])
check('... the extinguisher animation plays', any('PlayObjectUseAnimation tanFile="anims\\\\obj_use\\\\h2ouse.tan"' in l for l in out))
check('... and the fire hotspot switches (flame off, "fce7fire1" on)', any('EnableHotspot window=-1 hotspot="fce7fire1" enable=1' in l for l in out))
rc, out = run('advance 1500; page CE7; advance 800; hold fireCO22; advance 200; clickhs yCE7FIRE1; advance 600')
check('CO2 extinguisher on a fire plays co2use.tan', rc == 0 and any('anims\\\\obj_use\\\\co2use.tan' in l and 'PlayObjectUseAnimation' in l for l in out), out[-3:])

rc, out = run('advance 1500; page FPH6MIR_B; advance 800; clickhs dropzone1; advance 500; game obj baby1')
check('picking up poem1 does not stop the game', rc == 0 and not any('no member' in l for l in out), out[-3:])
check('... and puts the baby synths in the scene (milestone 0x800)', any(l.strip() == 'obj.baby1.m_bInScene = 1' for l in out))

rc, out = run('game fuses; advance 1500; page FBDCHARGE; advance 800; hold goggles; advance 200; clickhs yPower; advance 300')
check('goggles on a charger: the charge sound plays', rc == 0 and any('pwrrech1.wav' in l for l in out), out[-3:])
print('machines: %s' % ('all checks passed' if failed == 0 else '%d FAILED' % failed))
sys.exit(1 if failed else 0)
