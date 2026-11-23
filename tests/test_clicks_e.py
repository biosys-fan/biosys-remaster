"""Clicks the interaction sweep calls dead with an empty hand (tests/audit/verdicts/e.tsv): a Y hotspot (verb 14, "use an object here") does
something only with its tool in the hand (the hotspot's filter: sb_pick, sb_blow, fishrod, latex). Expectations come from the original's
routines named in the checks and from the API log."""
import os, re, shutil, subprocess, sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
os.chdir(ROOT)
D = 'build/test/clicks_e'
shutil.rmtree(D, ignore_errors=True)
os.makedirs(D + '/save')
failed = 0


def check(name, ok, detail=''):
    global failed
    print(('PASS ' if ok else 'FAIL ') + name + ('' if ok else ' -- ' + str(detail)[:600]))
    failed += 0 if ok else 1


def run(script):
    env = dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy')
    r = subprocess.run(['build/biosys', '--game', '--skip-intro', '--seed', '3', '--mute', '--no-config', '--save-dir', D + '/save', '--log-events', '--log-api',
                        '--script', script], capture_output=True, text=True, env=env)
    out = (r.stdout + r.stderr).splitlines()
    return r.returncode, out


def after(out, mark):
    return out[out.index(mark) + 1:] if mark in out else []


# --- Y hotspot, empty hand: the game logic is told, nothing else happens (dll:1001db50 'Y': no plant, no feature F<name>, no link L<name>)
rc, out = run('advance 1500; page R2; advance 800; scroll 1600,0; echo A; clickhs YR2pa_3; advance 800')
a = after(out, 'A')
check('YR2pa_3 with an empty hand: the hotspot event reaches the game, no object event, no picture change',
      rc == 0 and 'event hotspot R2 YR2pa_3' in a and not [l for l in a if l.startswith('event gameobject') or 'SetImageVisibility' in l], a[:12])

# --- the same hotspot with the blowpipe pick (sb_pick, dll:10006350 CBlowpipePick::OnUsedOnHotspot): the papaya is picked
rc, out = run('advance 1500; page R2; advance 800; scroll 1600,0; hold sb_pick; echo A; clickhs YR2pa_3; advance 1500')
a = after(out, 'A')
check('YR2pa_3 with sb_pick: the use event, the fruit object is picked (gameobject R2pa_3 1) and its picture hidden, RR2PA_3 is acted',
      rc == 0 and 'event gameobject sb_pick 7 YR2pa_3' in a and 'event gameobject R2pa_3 1' in a
      and any('SetImageVisibility' in l and 'r2pa_3' in l and 'visible=0' in l for l in a)
      and any('DoHotspotAction' in l and 'RR2PA_3' in l for l in a), a[:20])

# --- the fishing rod on y_fish (dll:1001b190): the message "no bait"
rc, out = run('advance 1500; page R2; advance 800; scroll 0,300; hold fishrod; echo A; clickhs y_fish; advance 1500')
a = after(out, 'A')
check('y_fish with the fishing rod: the use animation and the message "no bait"',
      rc == 0 and 'event gameobject fishrod 7 y_fish' in a and any('player-message 94 no bait' in l for l in a), a[:14])

# --- yCylinder with the oxygen cylinder (dll:1001e05a): ONE call Eng_Api48_DropHeldObject("dCylinder"); the port once asked first for "any zone"
rc, out = run('advance 1500; page CE7; advance 800; hold oxy_cyl; scroll 200,330; echo A; clickhs yCylinder; advance 300')
a = after(out, 'A')
drops = [l for l in a if 'apicall DropHeldObject' in l]
check('yCylinder with the oxygen cylinder: exactly one DropHeldObject call, for "dCylinder" (not for the nearest free zone)',
      rc == 0 and len(drops) == 1 and 'hotspot="dCylinder"' in drops[0], drops)
# ... and the cylinder really goes into the holder: the engine's drop (exe:0044d030) has no hand-lock test (the port once refused, rc 17)
rc, out = run('advance 1500; page CE7; advance 800; hold oxy_cyl; scroll 200,330; echo A; clickhs yCylinder; advance 2000; dump state; dump objects')
a = after(out, 'A')
check('the hand-locked oxygen cylinder can be put down: the drop succeeds, the hand is empty, the cylinder sits in CE7/dcylinder',
      rc == 0 and any('apicall DropHeldObject hotspot="dCylinder" -> 0' in l for l in a) and 'held -' in a
      and any(l.startswith('obj ') and ' oxy_cyl status=1 zone=CE7/dcylinder' in l for l in a),
      [l for l in a if 'DropHeld' in l or l.startswith('held') or ' oxy_cyl ' in l])
# the D key puts a hand-locked object down too (same routine, nearest free zone that accepts it)
rc, out = run('advance 1500; page PH13; advance 800; hold oxy_cyl; advance 300; echo A; key D; advance 2000; dump state')
a = after(out, 'A')
check('D with the oxygen cylinder in the hand on its home page: the hand is empty afterwards', rc == 0 and 'held -' in a,
      [l for l in a if l.startswith('held') or 'gameobject' in l][:8])

# --- the clock's page with the clock in the hand (heldHotspot): bon switches the alarm on (bell, "on"), bup on a fresh clock shows "off"
rc, out = run('advance 1500; page XCLOCK; advance 800; hold alarm; echo A; clickhs bon; advance 300')
a = after(out, 'A')
rc2, out2 = run('advance 1500; page XCLOCK; advance 800; hold alarm; echo A; clickhs bup; advance 300')
a2 = after(out2, 'A')
check('xclock with the clock: bon shows the bell and "on"; bup (time up) shows "off" on a clock that is off',
      rc == 0 and rc2 == 0 and any('image="bell" visible=1' in l for l in a) and any('image="on" visible=1' in l for l in a)
      and any('image="off" visible=1' in l for l in a2), (a[:6], a2[:6]))

print('clicks_e: %s' % ('all checks passed' if failed == 0 else '%d FAILED' % failed))
sys.exit(1 if failed else 0)
