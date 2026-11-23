"""The lift after a ride. The player walks from the start to the rainforest lift (R4 -> R5 -> R3 -> call panel -> inside) and presses the
basement button. The original closes the doors, plays the ride's sound and, when it has ended, shows the floor it arrived at: the floor
light, the view through the open doors and the way out (CLift::Display dll:10023860 with redraw = 1, whose last step is Eng_RedrawPage,
API 0x28). Two port errors made the picture stay as it was (closed doors, the old floor light) although the way out was already enabled:
the features' redraw helper called RedrawScreen (API 0x49) instead of RedrawPage, and the lift hid three images that do not exist
("js_1d") instead of the floor lights "js_1" .. "js_3". The input is real input; the expectations come from the page file and the API log."""
import os, shutil, struct, subprocess, sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, 'tests'))
import play_lib

os.chdir(ROOT)
D = 'build/test/lift'
shutil.rmtree(D, ignore_errors=True)
os.makedirs(D + '/save')
failed = 0


def check(name, ok, detail=''):
    global failed
    print(('PASS ' if ok else 'FAIL ') + name + ('' if ok else ' -- ' + str(detail)[:500]))
    failed += 0 if ok else 1


def bmp(path):
    """the 8-bit screenshot of the script command `shot` -> a function (x, y) -> (r, g, b)"""
    d = open(path, 'rb').read()
    off = struct.unpack('<I', d[10:14])[0]
    w, h = struct.unpack('<ii', d[18:26])
    assert struct.unpack('<H', d[28:30])[0] == 8 and h > 0
    stride = (w + 3) & ~3
    pal = [(d[54 + 4 * i + 2], d[54 + 4 * i + 1], d[54 + 4 * i]) for i in range(256)]
    return lambda x, y: pal[d[off + (h - 1 - y) * stride + x]]     # the file is bottom-up


p = play_lib.Play('R4')
p.advance(1000)
p.goto('L_R4R5', 'R5')
p.goto('L_R5R3', 'R3')
p.click_hotspot('frlift_2_c', wait=2500); p.page = 'frlift_2_c'
p.click_hotspot('b_call', wait=300); p.raw('mixdump %s/a.wav 2500' % D); p.advance(1500); p.page = 'frlift_2_o'
p.click_hotspot('L_R3LIFT', wait=4000); p.page = 'brlift_i'
p.raw('game dump', 'echo mark inside', 'framehash', 'shot %s/before.bmp' % D)
p.click_hotspot('B_1', wait=300)
p.raw('echo mark riding', 'framehash', 'shot %s/riding.bmp' % D, 'mixdump %s/b.wav 5000' % D)      # the ride's sound (4.1 s) runs to its end
p.advance(500)
p.raw('echo mark arrived', 'framehash', 'shot %s/arrived.bmp' % D)
r = subprocess.run(['build/biosys', '--game', '--mute', '--no-config', '--skip-intro', '--seed', '3', '--save-dir', D + '/save', '--log-api', '--log-events',
                    '--script', '; '.join(p.cmds)], capture_output=True, text=True, env=dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy'))
out = r.stdout.splitlines()
check('the run ends without an error', r.returncode == 0, r.stderr[-300:])
check('the walk leads into the lift (location BRLIFT_I)', any('location=BRLIFT_I' in l for l in out), [l for l in out if l.startswith('game site')])


def part(a, b):
    i = out.index('mark ' + a) if 'mark ' + a in out else 0
    j = out.index('mark ' + b) if b and 'mark ' + b in out else len(out)
    return out[i:j]


ride, arr = part('inside', 'riding'), part('riding', 'arrived')
check('the basement button closes the doors and starts the ride sound',
      any('DisplayImage' in l and '"doors"' in l for l in ride) and any('OpenSound' in l and 'liftdown.wav' in l for l in ride), ride[-12:])
check('on arrival the floor lights are hidden by their real names (js_1 .. js_3 exist on the page: no "not found")',
      any('SetImageVisibility' in l and '"js_2" visible=0 -> 0' in l for l in arr), [l for l in arr if 'js_' in l])
check('... the light of the basement and the view of the basement are switched on',
      any('"js_1" visible=1 -> 0' in l for l in arr) and any('SetImageVisibility' in l and '"basement' in l and 'visible=1 -> 0' in l for l in arr), [l for l in arr if 'visible=1' in l])
check('... and the page is painted again (RedrawPage, API 0x28)', any(l.startswith('apicall RedrawPage') for l in arr), [l for l in arr if 'Redraw' in l])
h = [l.split()[-1] for l in out if l.startswith('framehash')]
check('the picture changes when the doors close and again on arrival (three different frames)', len(h) == 3 and len(set(h)) == 3, h)
# the page's own rectangles: floor lights JS_1 at 405,160 (24x23) and JS_2 at 405,100 (23x24), view rows start at screen row 56
before, arrived = bmp(D + '/before.bmp'), bmp(D + '/arrived.bmp')


def green(px, x0, y0):
    return sum(1 for y in range(y0 + 56, y0 + 56 + 23) for x in range(x0, x0 + 23) if px(x, y)[1] > 90 and px(x, y)[1] > 1.5 * px(x, y)[0])


b1, b2, a1, a2 = green(before, 405, 160), green(before, 405, 100), green(arrived, 405, 160), green(arrived, 405, 100)
check('before the ride the light of the biome floor is on and the basement light off (green pixels %d / %d)' % (b2, b1), b2 > 10 and b1 == 0, (b1, b2))
check('after the ride the basement light is on and the biome floor light off (green pixels %d / %d)' % (a1, a2), a1 > 10 and a2 == 0, (a1, a2))
diff = sum(1 for y in range(56, 424, 4) for x in range(0, 300, 4) if before(x, y) != arrived(x, y))
check('the view through the doors is another one after the ride (%d of %d sampled points differ)' % (diff, 92 * 75), diff > 3000, diff)
print('lift tests: %d failed' % failed)
sys.exit(1 if failed else 0)
