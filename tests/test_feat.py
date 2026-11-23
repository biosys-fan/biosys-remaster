#!/usr/bin/env python3
"""Checks of the machines' parameter interface and reproducibility (src/game feat_*.cpp, build/biosys-feattest).
  * every FEATURE_* / SIM_* / WORLD_* name the engine's BS4 panels read or write (the error strings of BioSys.exe) is answered by
    at least one feature class in the world, with the id the notes (analysis/notes/simulation.md section 10) give it;
  * the answers do not depend on anything but the seed: two runs with the same seed print the same trace, different seeds differ.
"""
import os as _os
# the developer's folder above the repository (original discs, analysis notes); see WORKERS.md
_DEV = _os.environ.get('BIOSYS_DEV_ROOT') or _os.path.abspath(_os.path.join(_os.path.dirname(_os.path.abspath(__file__)), '..', '..'))
import os
import re
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
TOOL = os.path.join(ROOT, 'build', 'biosys-feattest')
STRINGS = (_DEV + '/analysis/strings/BioSys.exe.txt')

# name -> (id, "world" for a NULL-feature parameter, or the classes the notes say answer it); simulation.md section 10
NOTES = {
    'FEATURE_ON': (1, 'all'), 'FEATURE_ACTIVE': (2, 'all'), 'FEATURE_TIME_ACTIVATE': (3, 'all'), 'FEATURE_TIME_LIMIT': (5, 'all'),
    'FEATURE_DAILY_TIME_LIMIT': (6, 'all'), 'FEATURE_MALFUNCTION': (7, 'all'), 'FEATURE_OPEN': (10, ['CHatch', 'CSeal']),
    'FEATURE_DRAW_AIR_FROM_BIOME': (0x11, ['CLung']), 'FEATURE_DESTINATION': (0x14, ['CLung', 'CWaterPump', 'CPodFeature']),
    'FEATURE_DESTINATION_AIR_VOLUME': (0x1b, ['CLung']), 'FEATURE_AIR_VOLUME': (0x1c, ['CLung']),
    'FEATURE_AIR_TEMPERATURE': (0x1e, ['CBiomeSensor']), 'FEATURE_SOIL_TEMPERATURE': (0x1f, ['CBiomeSensor']),
    'FEATURE_PH': (0x20, ['CBiomeSensor', 'COceanProbe']), 'FEATURE_SOIL_WATER': (0x21, ['CBiomeSensor']),
    'FEATURE_OXYGEN': (0x22, ['CBiomeSensor', 'COceanProbe', 'COxygenStore']), 'FEATURE_CO2': (0x23, ['CBiomeSensor']),
    'FEATURE_LIGHT': (0x24, ['CBiomeSensor']), 'FEATURE_WINDSPEED': (0x25, ['CBiomeSensor']), 'FEATURE_POLLUTION': (0x26, ['CBiomeSensor']),
    'FEATURE_HUMIDITY': (0x27, ['CBiomeSensor']), 'FEATURE_WATER_TEMPERATURE': (0x28, ['CTankSensor', 'COceanProbe']),
    'FEATURE_WATER_CLARITY': (0x29, ['COceanProbe']), 'FEATURE_WATER_SALINITY': (0x2a, ['COceanProbe']),
    'FEATURE_WATER_LEVEL': (0x2b, ['CTankSensor', 'CWaterPump', 'CPurifier', 'CTideMachine', 'COceanProbe']),
    'FEATURE_WATER_PURITY': (0x2c, ['CTankSensor', 'CPurifier']), 'FEATURE_ALARMS': (0x32, ['CBiomeSensor', 'CTankSensor', 'CPurifier']),
    'FEATURE_AIR_FLOW': (0x50, ['CFan', 'CVent']), 'FEATURE_WATER_FLOW': (0x51, ['CDrainGauge']), 'FEATURE_SILT_LEVEL': (0x52, ['CDrainGauge']),
    'FEATURE_GAS_SOURCE': (0x5a, ['CBubbler']), 'FEATURE_WATER_SOURCE': (0x5f, ['CRainMachine', 'CMister']),
    'FEATURE_DAY_ACTIVATE_TEMP': (100, ['CHeater']), 'FEATURE_DAY_DEACTIVATE_TEMP': (0x65, ['CHeater']),
    'FEATURE_NIGHT_ACTIVATE_TEMP': (0x66, ['CHeater']), 'FEATURE_NIGHT_DEACTIVATE_TEMP': (0x67, ['CHeater']),
    'FEATURE_ACTIVATE_HUMIDITY': (0x6e, ['CMister']), 'FEATURE_DEACTIVATE_HUMIDITY': (0x6f, ['CMister']),
    'FEATURE_AMOUNT_OXYGEN_STORED': (0x78, ['COxygenStore', 'CBubbler']), 'FEATURE_POD_POSITION': (0x85, ['CPodTransport']),
    'FEATURE_START_CHARGING_POD': (0x86, ['CPodTransport']), 'FEATURE_POWER_GENERATION': (0x8c, ['CSolarGen', 'CWindmill']),
    'FEATURE_POWER_AVAILABLE': (0x8d, ['CPodTransport']), 'FEATURE_POWER_USAGE': (0x8e, 'all'),
    'WORLD_POWER_GENERATION': (0x3f2, 'world'), 'WORLD_POWER_AVAILABLE': (0x3f3, 'world'), 'WORLD_POWER_USAGE': (0x3f4, 'world'),
    'WORLD_TEMPERATURE': (0x3fd, 'world'), 'WORLD_WINDSPEED': (0x3fe, 'world'), 'WORLD_CLOUD_COVER': (0x3ff, 'world'),
    'WORLD_ALARMS': (0x41a, 'world'), 'WORLD_AEB_POWER_DISCONNECTED': (0x424, 'world'),
    'SIM_RAINFOREST_BIOME_HEALTH': (0x834, 'world'), 'SIM_SAVANNAH_BIOME_HEALTH': (0x835, 'world'), 'SIM_OCEAN_BIOME_HEALTH': (0x836, 'world'),
    'SIM_DEAD_BIOME_HEALTH': (0x837, 'world'), 'SIM_HEALTH_STATE': (2000, 'cel'),
}

bad = 0


def fail(msg):
    global bad
    print('FAIL:', msg)
    bad += 1


def run(script, seed=1):
    p = subprocess.run([TOOL, '--seed', str(seed)], input=script, capture_output=True, text=True)
    if p.returncode != 0:
        print(p.stdout[-1500:], p.stderr[-1500:])
        raise SystemExit('feattest status %d' % p.returncode)
    return p.stdout.split('\n')


def main():
    names = set()
    for l in open(STRINGS, errors='replace'):
        for n in re.findall(r'parameter ((?:FEATURE|SIM|WORLD)_[A-Z0-9_]+)', l):
            names.add(n)
    out = run('set fbr2lung 20 22 0\nmembers\ngetall\nget - 0x3f2\nget - 0x3f3\nget - 0x3f4\nget - 0x3fd\nget - 0x3fe\nget - 0x3ff\nget - 0x41a\nget - 0x424\n'
              'get - 0x834\nget - 0x835\nget - 0x836\nget - 0x837\nget R1 2000\nget ocean 2000\n')
    classes = {}
    for l in out:
        if l.startswith('member ') and '.class = ' in l:
            f = l[7:].split('.class = ')
            classes[f[0]] = f[1]
    answered = {}      # id -> set of classes
    for l in out:
        if l.startswith('getall '):
            p = l.split()
            answered.setdefault(int(p[3]), set()).add(p[2])
    world_ok = {}
    for l in out:
        m = re.match(r'get (\S*) (\d+) = (ok|no)', l)
        if m:
            world_ok[(m.group(1), int(m.group(2)))] = m.group(3) == 'ok'
    unknown = sorted(n for n in names if n not in NOTES)
    if unknown:
        fail('names the engine uses that the notes do not list: %s' % ', '.join(unknown))
    for n in sorted(names):
        if n not in NOTES:
            continue
        pid, who = NOTES[n]
        if who == 'world':
            ok = world_ok.get(('', pid)) or world_ok.get(('-', pid))
            if not ok:
                fail('%s (0x%x) is not answered as a world parameter' % (n, pid))
        elif who == 'cel':
            if not (world_ok.get(('R1', pid)) and world_ok.get(('ocean', pid))):
                fail('%s (%d) is not answered for a cel and the ocean' % (n, pid))
        elif who == 'all':
            allc = set(classes.values())
            if answered.get(pid, set()) != allc:
                fail('%s (0x%x) is not answered by every class: missing %s' % (n, pid, sorted(allc - answered.get(pid, set()))))
        else:
            got = answered.get(pid, set())
            missing = [c for c in who if c not in got and any(v == c for v in classes.values())]
            if missing:
                fail('%s (0x%x) is not answered by %s' % (n, pid, ', '.join(missing)))
    print('%s: %d BS4 parameter names of the engine, every one answered by the classes the notes list' % ('FAIL' if bad else 'PASS', len(names)))
    # reproducibility
    a = run('trace 900\n', 5)
    b = run('trace 900\n', 5)
    c = run('seed 6\ntrace 900\n', 5)
    if a != b:
        fail('two runs with the same seed print different traces')
    if a == c:
        fail('a different generator seed prints the same trace')
    print('%s: the trace depends on the seed only' % ('FAIL' if bad else 'PASS'))
    return 1 if bad else 0


if __name__ == '__main__':
    sys.exit(main())
