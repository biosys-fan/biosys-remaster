"""The water tanks cabinet. The original's CTanksCabinet has four handlers the port lacked until 2026-11-22: vtable +0x30 dll:100056c0 (a click
on the tap "ywater": a drink with the animation anims\\obj_use\\water.tan, unless the player's hydration is above 99: message 0xc1 = 193),
+0x3c dll:100058e0 (the shower page, a name ending in S: the water filter over the page, Eng_BeginPageFilter(0x76, 0x56, 0x57)), +0x34
dll:10004760 and +0x44 dll:10005810. Before, the game showed "left click to drink water" over the tap and the click did nothing. The pages
are reached with a jump; the expectations come from the page file (hotspot "ywater" of page fbrtanks_2 at 483,134..639,345) and the API log."""
import os, shutil, subprocess, sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
os.chdir(ROOT)
D = 'build/test/tanks'
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


def parts(lines):
    out, cur = {}, None
    for l in lines:
        if l.startswith('MARK '):
            cur = l[5:]
            out[cur] = []
        elif cur:
            out[cur].append(l)
    return out


TAP = '560,296'      # the middle of "ywater" (page 561,240; the view starts at row 56)
sc = 'advance 1500; page fbrtanks_2; advance 800'
for i in range(1, 7):
    sc += '; echo MARK %d; click %s; advance 1500' % (i, TAP)
rc, out = run(sc)
p = parts(out)
drinks = [i for i in range(1, 7) if any('PlayObjectUseAnimation' in l and 'water.tan' in l and 'drinking\\\\drink' in l for l in p.get(str(i), []))]
full = [i for i in range(1, 7) if any('player-message 193' in l for l in p.get(str(i), []))]
check('the run ends without error', rc == 0, out[-3:])
check('a click on the tap is a drink (the water animation with one of the four drinking sounds)', 1 in drinks, p.get('1', [])[:8])
check('after some drinks the player cannot drink any more (message 193), and then no animation plays (drinks at clicks %s, refused at %s)' % (drinks, full),
      len(drinks) >= 1 and len(full) >= 1 and min(full) > max(drinks), (drinks, full))
rc, out = run('advance 1500; echo MARK a; page fbrtanks_s; advance 1500')
a = parts(out).get('a', [])
check('entering the shower page puts the water filter over the page and starts the shower sound',
      rc == 0 and any('apicall BeginPageFilter a=118 b=86 c=87' in l for l in a) and any('SHOWER.WAV' in l.upper() and 'OpenSound' in l for l in a),
      [l for l in a if 'Filter' in l or 'SHOWER' in l.upper()][:4])
print('tanks: %s' % ('all checks passed' if failed == 0 else '%d FAILED' % failed))
sys.exit(1 if failed else 0)
