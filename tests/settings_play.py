"""Runs the scripted playthrough of tests/test_play.py with other settings (tests/test_settings.sh): the same script, the same
checks, the options `--realism R --difficulty D` added to the `build/biosys --game` run. usage: settings_play.py REALISM DIFFICULTY"""
import os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import play_lib
realism, difficulty = sys.argv[1], sys.argv[2]
orig = play_lib.Play.run
def run(self, seed=1, extra=(), **kw):
    return orig(self, seed, list(extra) + ['--realism', realism, '--difficulty', difficulty], **kw)
play_lib.Play.run = run
src = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'test_play.py')
sys.argv = [src]
exec(compile(open(src).read(), src, 'exec'), {'__name__': '__main__', '__file__': src})
