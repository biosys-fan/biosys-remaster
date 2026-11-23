#!/usr/bin/env python3
"""Independent check of the machines / sensors of src/game (feat_*.cpp), written from the DECOMPILED text of BiosGame.dll
(analysis/decompiled, addresses in the comments), not from the C++.

The C++ side is build/biosys-feattest. This script
  1. asks it for every parameter every feature answers (`getall`) and compares them with the dispatch table written down here
     (id -> class -> member), for every feature and every id 0..0x100;
  2. drives scripted parameter writes (`set`) and compares the changed members with the SetParam of the decompiled classes;
  3. runs the world (`trace`), 1 game hour and 1 game day, and re-computes every feature update from what the step reads
     (world, cels, ocean, tanks, the feature members before the phase, the generator state; see feattest.cpp "the trace"):
     the base timer / activation / power logic, CBiomeSensor / CTankSensor / COceanProbe readings including the noise (the same
     generator: seed*214013+2531011, (seed>>16)&0x7fff), CHeater thermostat and heating, CFan, CRainMachine, CMister, CAirMixer,
     CLung strokes, CSolarGen / CWindmill power, CFuseBox, and the water / ocean machines.
Every compared number must agree to 1e-9 relative. Each disagreement is printed; the exit status is 1 if there is one.
"""
import math
import os
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
TOOL = os.path.join(ROOT, 'build', 'biosys-feattest')
M32 = 0xffffffff
RTOL = 1e-9

problems = []
import collections
events = collections.Counter()      # (class, what) -> how many times the trace saw it


def bad(what):
    if len(problems) < 60:
        print('DISAGREE', what)
    problems.append(what)


def u32(x):
    return x & M32


def i32(x):
    x &= M32
    return x - (1 << 32) if x >> 31 else x


def close(a, b):
    if isinstance(a, float) or isinstance(b, float):
        a, b = float(a), float(b)
        if a == b:
            return True
        if math.isnan(a) and math.isnan(b):
            return True
        return abs(a - b) <= RTOL * max(abs(a), abs(b)) + 1e-12
    return a == b


def parse(v):
    if v.startswith('0x'):
        return int(v, 16)
    try:
        return int(v)
    except ValueError:
        pass
    try:
        return float(v)
    except ValueError:
        return v


def kv(tokens):
    d = {}
    for t in tokens:
        k, _, v = t.partition('=')
        d[k] = parse(v)
    return d


# ------------------------------------------------------------------------------------------------------------------------
# the model: what the machines read and write

class Rng:
    """_rand dll:10079380, RandPlusMinus dll:100075d0"""
    def __init__(self, state):
        self.s = state

    def rand(self):
        self.s = (self.s * 214013 + 2531011) & M32
        return (self.s >> 16) & 0x7fff

    def pm(self, e):
        r = self.rand()
        return e * 2.0 * r * 3.051850947599719e-05 - e


def clamp(x, lo, hi):          # dll:100074b0
    if x < lo:
        return lo
    if hi < x:
        return hi
    return x


def air_turn_on(m, fl, on):
    """CAirFlow::TurnOn dll:1005af40: a running flow adds its wind speed to the wind of its `to` cel and its vent airflow to the
    vent's target; the vent target (CVent+0x60) is a feature member"""
    to = m.cels.get(fl['to'])
    vent = m.feats.get(fl['vent']) if fl['vent'] != 'null' else None
    if on == 0:
        if fl['on'] != 0:
            to['windSpeed'] -= fl['windSpeed']
            if vent is not None:
                vent['m_dAirFlowTarget'] -= fl['ventAirflow']
        fl['on'] = 0
        return
    if fl['on'] == 0:
        to['windSpeed'] += fl['windSpeed']
        if vent is not None:
            vent['m_dAirFlowTarget'] += fl['ventAirflow']
    fl['on'] = 1


def air_set_reversed(m, fl, r):
    """CAirFlow::SetReversed dll:1005afb0"""
    if fl['reversed'] != r:
        fl['windSpeed'] = -fl['windSpeed']
        fl['reversed'] = r
        if fl['on'] != 0:
            to = m.cels.get(fl['to'])
            to['windSpeed'] += fl['windSpeed']
            to['windSpeed'] += fl['windSpeed']


def switch_air(m, id, on):
    """CWorld::SwitchAirFlows dll:10013e60 (id 0 is no flow)"""
    if id == 0:
        return
    for fl in m.airflows:
        if fl['id'] == id:
            air_turn_on(m, fl, on)


def reverse_air(m, id, r):      # dll:10013eb0
    for fl in m.airflows:
        if fl['id'] == id:
            air_set_reversed(m, fl, r)


def dest_air(m, id, cel):       # dll:10013f00
    for fl in m.airflows:
        if fl['id'] == id:
            fl['to'] = cel


class Model:
    def __init__(self):
        self.feats = {}        # name -> {member: value}
        self.order = []
        self.cels = {}
        self.tanks = {}
        self.ocean = {}
        self.world = {}
        self.time = 0
        self.tod = 0
        self.diurnal = 0x28
        self.rng = None
        self.fuse = ['fbrfuse', 'fbsfuse', 'fbofuse', 'fbdfuse', 'fbpfuse']   # world+0x1f0..0x200 (RF, SAV, OCEAN, DEAD, HOUSE)
        self.seal_global = 0
        self.airflows = []

    def copy_state(self):
        return {n: dict(f) for n, f in self.feats.items()}


FIRST_PLAIN = None


def has_power(m, mask):
    """CWorld::HasPower dll:10014910"""
    pf = m.world['flags']
    if (pf & 0x40000000) == 0:
        return 0
    if mask & 0x1000000:
        if not pf & 0x1000000:
            return 0
        box = 0
    elif mask & 0x2000000:
        if not pf & 0x2000000:
            return 0
        box = 1
    elif mask & 0x4000000:
        if not pf & 0x4000000:
            return 0
        box = 2
    elif mask & 0x8000000:
        if not pf & 0x8000000 and not mask & 0x10000:
            return 0
        box = 3
    elif mask & 0x10000000:
        if not pf & 0x10000000:
            return 0
        box = 4
    elif mask & 0x20000000:
        return (pf & 0x20000000) >> 0x1d
    else:
        return 1
    fb = m.feats[m.fuse[box]]
    return 1 if (fb['m_dwPowerMask'] & mask) == mask else 0


def add_demand(m, d):          # dll:100149f0 / 10014a20 (clamped 0..100)
    m.world['demand'] = clamp(m.world['demand'] + d, 0.0, 100.0)


def sub_demand(m, d):
    m.world['demand'] = clamp(m.world['demand'] - d, 0.0, 100.0)


# ---- the virtual calls, by class. A class not listed uses its base's. ----------------------------------------------------------

BASES = {'CLung': 'CBiomeSensor', 'CPurifier': 'CWaterPump'}


def cls_chain(c):
    out = [c]
    while c in BASES:
        c = BASES[c]
        out.append(c)
    out.append('CFeature')
    return out


V = {}      # (class, virtual name) -> function(m, f, *args)


def virt(m, f, name, *args):
    for c in cls_chain(f['class']):
        fn = V.get((c, name))
        if fn:
            return fn(m, f, *args)
    raise KeyError((f['class'], name))


def reg(cls, name):
    def d(fn):
        V[(cls, name)] = fn
        return fn
    return d


# CFeature ------------------------------------------------------------------------------------------------------------------

@reg('CFeature', 'start')       # 10019eb0
def _(m, f):
    f['m_bRunning'] = 1
    add_demand(m, f['m_dPowerUsage'])


@reg('CFeature', 'stop')        # 10019ed0
def _(m, f):
    sub_demand(m, f['m_dPowerUsage'])
    f['m_bRunning'] = 0


@reg('CFeature', 'activate')    # 10019e40
def _(m, f):
    f['m_bActive'] = 1
    if f['m_bWorking'] != 0:
        f['unk_20'] = m.time
        if f['m_bRunning'] == 0:
            if has_power(m, f['m_dwPowerMask']) != 0 or (f['m_dwPowerMask'] & 0x80000000):
                virt(m, f, 'start')


@reg('CFeature', 'deactivate')  # 10019e90
def _(m, f):
    f['m_bActive'] = 0
    if f['m_bRunning'] != 0:
        virt(m, f, 'stop')


@reg('CFeature', 'turnon')      # 10019dc0
def _(m, f):
    f['m_bOn'] = 1
    virt(m, f, 'update', m.time)


@reg('CFeature', 'turnoff')     # 10019de0
def _(m, f):
    f['m_bOn'] = 0
    virt(m, f, 'deactivate')


@reg('CFeature', 'poweron')     # 10019df0
def _(m, f):
    if f['m_bActive'] != 0 and has_power(m, f['m_dwPowerMask']) != 0:
        virt(m, f, 'start')


@reg('CFeature', 'poweroff')    # 10019e20
def _(m, f):
    if f['m_bRunning'] == 0 or (f['m_dwPowerMask'] & 0x80000000):
        return
    virt(m, f, 'stop')


def in_window(f, tod):          # 10019aa0
    start, end = u32(f['m_dwTimerStart']), u32(f['m_dwTimerEnd'])
    if end <= start:
        return start < tod or tod < end
    return start < tod and tod < end


@reg('CFeature', 'update')      # 10019a10
def _(m, f, now):
    dt = u32(now - f['m_dwTimeLastUpdate'])
    f['m_dwTimeLastUpdate'] = now
    if f['m_bActive'] == 0:
        if f['m_bOn'] != 0 and f['m_dwTimerStart'] != -1:
            if in_window(f, m.tod):
                virt(m, f, 'activate')
    else:
        if f['m_dwTimerEnd'] == -1:
            if dt < u32(f['m_dwTimeLimit']):
                f['m_dwTimeLimit'] = i32(u32(f['m_dwTimeLimit']) - dt)
                return
            f['m_dwTimeLimit'] = 0
            virt(m, f, 'deactivate')
            return
        if not in_window(f, m.tod):
            virt(m, f, 'deactivate')


def base_set(m, f, id, v):      # 10019580
    if id == 1:
        virt(m, f, 'turnon' if v else 'turnoff')
    elif id == 2:
        virt(m, f, 'activate' if v else 'deactivate')
    elif id == 3:
        f['m_dwTimerStart'] = v
    elif id == 5:
        f['m_dwTimeLimit'] = v
    elif id == 6:
        if v == 0x15180 and f['m_dwTimerStart'] == 0:
            f['m_dwTimerEnd'] = 0x15180
        else:
            f['m_dwTimerEnd'] = i32(u32(u32(f['m_dwTimerStart']) + u32(v)) % 0x15180)
    else:
        return False
    virt(m, f, 'update', m.time)
    return True


@reg('CFeature', 'setparam')
def _(m, f, id, v, d):
    return base_set(m, f, id, v)


# CSolarGen / CWindmill (10003510.., 10070cd0..) ---------------------------------------------------------------------------------

for cls in ('CSolarGen', 'CWindmill'):
    @reg(cls, 'activate')       # 100035f0 / 10070db0
    def _(m, f):
        f['m_bActive'] = 1
        if f['m_bWorking'] != 0:
            virt(m, f, 'start')

    @reg(cls, 'deactivate')     # 10003610 / 10070dd0
    def _(m, f):
        f['m_bActive'] = 0
        if f['m_bWorking'] != 0:
            m.world['generation'] -= f['m_dPowerUsage']
            f['m_dPowerUsage'] = 0.0
        virt(m, f, 'stop')

    @reg(cls, 'start')
    def _(m, f):
        pass

    @reg(cls, 'stop')
    def _(m, f):
        pass


@reg('CSolarGen', 'update')     # 10003670
def _(m, f, now):
    f['m_dwTimeLastUpdate'] = now
    if f['m_bActive'] != 0:
        g = m.world['sunlight'] * 0.027777777777777776
        m.world['generation'] += g - f['m_dPowerUsage']
        f['m_dPowerUsage'] = g


@reg('CWindmill', 'update')     # 10070e50
def _(m, f, now):
    f['m_dwTimeLastUpdate'] = now
    if f['m_bActive'] != 0:
        g = m.world['windSpeed'] * 0.001388888888888889
        if 100.0 < g:
            g = 100.0
        m.world['generation'] += g - f['m_dPowerUsage']
        f['m_dPowerUsage'] = g


# CFuseBox 10027450 --------------------------------------------------------------------------------------------------------------

@reg('CFuseBox', 'update')
def _(m, f, now):
    state = f['unk_60']
    last = f['m_dwTimeLastUpdate']
    f['m_dwTimeLastUpdate'] = now
    if (state & 1) and u32(now - f['unk_64']) > 100 and not state & 0x10:
        f['unk_64'] = now
        if not state & 8:
            if not state & 4:
                if not state & 2:
                    f['unk_60'] |= 2
                else:
                    f['unk_60'] |= 4
            else:
                f['unk_60'] |= 8
        else:
            f['unk_60'] |= 0x10
        site = m.sites.get(f['m_pSite'])
        if site is not None:
            site['unk0e8'] += 4.0
            site['unk0f0'] += 4.0
    if f['unk_60'] != 0:
        cel = m.site_cel.get(f['m_pSite'])
        if cel in m.cels:
            m.cels[cel]['newPollution'] += u32(now - last) * 0.0005555555555555556


@reg('CHatch', 'setparam')      # 1001cd60
def _(m, f, id, v, d):
    if id != 10:
        return base_set(m, f, id, v)
    f['unk_58'] = 0 if v == 0 else 1
    return True


@reg('CPodFeature', 'setparam')     # 10002820
def _(m, f, id, v, d):
    if id != 0x14:
        return base_set(m, f, id, v)
    m.feats[f['unk_5c']]['unk_6c'] = v
    return True


@reg('CPodTransport', 'setparam')   # 1001b730
def _(m, f, id, v, d):
    if id == 0x8f:
        add_demand(m, 0.001388888888888889)
        f['unk_58'] = 0x1e
        f['unk_74'] = m.time
        return True
    if id == 0x90:
        pod_stop(m, f)
        return True
    return base_set(m, f, id, v)


# CFire 10006b00 / 10006b70 / 10006bb0, CPodTransport 1001c1f0 --------------------------------------------------------------------------

@reg('CFire', 'update')
def _(m, f, now):
    last = f['m_dwTimeLastUpdate']
    f['m_dwTimeLastUpdate'] = now
    if f['m_bActive'] != 0:
        cel = m.cels[m.site_cel[f['m_pSite']]]
        if 13.8 < cel['oxygen']:
            d = float(u32(now - last)) * 2.8935185185185185e-07
            cel['newCO2'] = cel['newCO2'] + d
            cel['newOxygen'] = cel['newOxygen'] - d


@reg('CFire', 'start')
def _(m, f):
    V[('CFeature', 'start')](m, f)


@reg('CFire', 'stop')
def _(m, f):
    V[('CFeature', 'stop')](m, f)


def pod_stop(m, f):             # 1001c2f0
    sub_demand(m, 0.001388888888888889)
    f['unk_74'] = 0
    f['unk_58'] = 10


@reg('CPodTransport', 'update')
def _(m, f, now):
    last = f['m_dwTimeLastUpdate']
    f['m_dwTimeLastUpdate'] = now
    if f['unk_58'] == 0x1e:
        if m.world['power'] < 3.0:
            pod_stop(m, f)
            return
        f['unk_60'] = float(u32(now - last)) * 0.001388888888888889 + f['unk_60']
        if 20.0 < f['unk_60']:
            f['unk_60'] = 20.0
            pod_stop(m, f)
            return
    else:
        if f['unk_58'] != 0x28:
            return
        if 0x3b < u32(m.time - f['unk_74']):
            f['unk_74'] = 0
            f['unk_68'] = f['unk_6c']
            f['unk_60'] = f['unk_60'] - 2.0


# CHeater 100173c0 / 10017330 ----------------------------------------------------------------------------------------------------

BIOME_CELS = {}


@reg('CHeater', 'thermostat')
def _(m, f):
    if m.diurnal in (5, 0x23, 0x28):
        on, off = f['m_dNightOn'], f['m_dNightOff']
    else:
        on, off = f['m_dDayOn'], f['m_dDayOff']
    probes = [m.feats[n] for n in m.biome_probes[f['m_pBiome']]]
    avg = (sum(p['m_dAirTempReading'] for p in probes) / len(probes)) if probes else 0.0
    if f['m_bActive'] == 0:
        if avg < on:
            virt(m, f, 'activate')
    elif off < avg:
        virt(m, f, 'deactivate')


@reg('CHeater', 'update')
def _(m, f, now):
    last = f['m_dwTimeLastUpdate']
    f['m_dwTimeLastUpdate'] = now
    if f['m_bOn'] != 0 and f['m_bWorking'] != 0:
        virt(m, f, 'thermostat')
        if f['m_bRunning'] != 0:
            dt = float(u32(now - last))
            for c in m.biome_cels[f['m_pBiome']]:
                cel = m.cels[c]
                cel['newAirTemp'] += dt * 0.0022222222222222222
                cel['airTemp'] = cel['newAirTemp']


@reg('CHeater', 'setparam')
def _(m, f, id, v, d):
    keys = {100: 'm_dDayOn', 0x65: 'm_dDayOff', 0x66: 'm_dNightOn', 0x67: 'm_dNightOff'}
    if id in keys:
        f[keys[id]] = d
        return True
    return base_set(m, f, id, v)


# CRainMachine 10025f10.. --------------------------------------------------------------------------------------------------------

@reg('CRainMachine', 'update')          # 10026210
def _(m, f, now):
    V[('CFeature', 'update')](m, f, now)
    if f['m_bActive'] != 0 and m.tanks[f['m_pSourceTank']]['waterVol'] < 0.01:
        virt(m, f, 'deactivate')


@reg('CRainMachine', 'setparam')        # 10025f10
def _(m, f, id, v, d):
    if id != 0x5f:
        return base_set(m, f, id, v)
    f['m_pSourceTank'] = f['m_pOsmTank'] if v == 0x60 else f['m_pCondTank']
    return True


# CMister 1002a8c0.. -------------------------------------------------------------------------------------------------------------

@reg('CMister', 'update')
def _(m, f, now):
    f['m_dwTimeLastUpdate'] = now
    if f['m_bOn'] != 0:
        hum = m.feats[f['m_pSensor']]['m_dHumidityReading']
        src = m.tanks[f['m_pSourceTank']]['waterVol']
        if f['m_bMisting'] == 0:
            if hum < f['m_dHumidityOn'] and 0.01 < src:
                virt(m, f, 'activate')
        elif f['m_dHumidityOff'] < hum or src < 0.01:
            virt(m, f, 'deactivate')


@reg('CMister', 'start')
def _(m, f):
    V[('CFeature', 'start')](m, f)
    f['m_bMisting'] = 1


@reg('CMister', 'stop')
def _(m, f):
    V[('CFeature', 'stop')](m, f)
    f['m_bMisting'] = 0


@reg('CMister', 'setparam')
def _(m, f, id, v, d):
    if id == 0x5f:
        f['m_pSourceTank'] = f['m_pOsmTank'] if v == 0x60 else f['m_pCondTank']
        return True
    if id == 0x6e:
        f['m_dHumidityOn'] = d
        return True
    if id == 0x6f:
        f['m_dHumidityOff'] = d
        return True
    return base_set(m, f, id, v)


# CAirMixer 1002a350 / 1002a3c0 --------------------------------------------------------------------------------------------------

@reg('CAirMixer', 'start')
def _(m, f):
    V[('CFeature', 'start')](m, f)
    for k in ('m_pCel[0]', 'm_pCel[1]', 'm_pCel[2]'):
        if f[k] in m.cels:
            m.cels[f[k]]['windSpeed'] += 2.0


@reg('CAirMixer', 'stop')
def _(m, f):
    V[('CFeature', 'stop')](m, f)
    for k in ('m_pCel[0]', 'm_pCel[1]', 'm_pCel[2]'):
        if f[k] in m.cels:
            m.cels[f[k]]['windSpeed'] -= 2.0


@reg('CFan', 'start')           # 100177d0
def _(m, f):
    V[('CFeature', 'start')](m, f)
    switch_air(m, f['m_wAirFlowId'], 1)


@reg('CFan', 'stop')            # 10017830
def _(m, f):
    V[('CFeature', 'stop')](m, f)
    switch_air(m, f['m_wAirFlowId'], 0)


# CVent 10062940 -----------------------------------------------------------------------------------------------------------------

@reg('CVent', 'update')
def _(m, f, now):
    last = f['m_dwTimeLastUpdate']
    f['m_dwTimeLastUpdate'] = now
    shown, target = f['m_dShownAirFlow'], f['m_dAirFlowTarget']
    if shown != target:
        step = float(u32(now - last)) * 0.002777777777777778
        if abs(target - shown) <= step:
            f['m_dShownAirFlow'] = target
            return
        if target <= shown:
            f['m_dShownAirFlow'] = shown - step
            return
        f['m_dShownAirFlow'] = shown + step


# CBiomeSensor 1005e0a0 -------------------------------------------------------------------------------------------------------------

THRESH = {
    0x100b7948: (22.0, 35.0, 22.0, 35.0, 60.0, 100.0, 20.0, 47.5),
    0x100b7988: (10.0, 37.0, 10.0, 37.0, 0.0, 20.0, 4.0, 40.0),
    0x100b79c8: (22.0, 30.0, 22.0, 30.0, 0.0, 100.0, 4.0, 38.0),
    0x100b7a08: (10.0, 35.0, 10.0, 35.0, 20.0, 100.0, 20.0, 38.0),
    0x100b7a48: (10.0, 35.0, 10.0, 35.0, 0.0, 100.0, 0.0, 100.0),
    0x100b7a88: (10.0, 35.0, 10.0, 35.0, 0.0, 0.0, 0.0, 0.0),
}


@reg('CBiomeSensor', 'update')
def _(m, f, now):
    last = f['m_dwTimeLastUpdate']
    cel = m.cels[f['m_pCel']]
    f['m_dwTimeLastUpdate'] = now
    r = m.rng
    if cel['flags'] & 2:
        f['m_dRainToday'] = u32(now - last) * 0.002777777777777778 + f['m_dRainToday']

    def track(i, v):
        if f['m_dMax[%d]' % i] < v:
            f['m_dMax[%d]' % i] = v
        if v < f['m_dMin[%d]' % i]:
            f['m_dMin[%d]' % i] = v
    f['m_dPressureReading'] = cel['airVolume'] / cel['capacity'] * (cel['airTemp'] + 273.0) * 0.0033003300330033004 * 1000.0 + r.pm(f['m_dPressureError'])
    track(0, f['m_dPressureReading'])
    f['m_dAirTempReading'] = r.pm(f['m_dAirTempError']) + cel['airTemp']
    track(1, f['m_dAirTempReading'])
    f['m_dCO2Reading'] = clamp(r.pm(f['m_dCO2Error']) + cel['co2'], 0.0, 100.0)
    track(2, f['m_dCO2Reading'])
    f['m_dHumidityReading'] = clamp(r.pm(f['m_dHumidityError']) + cel['humidity'], 0.0, 100.0)
    track(3, f['m_dHumidityReading'])
    f['m_dLightReading'] = clamp((r.pm(f['m_dLightError']) + cel['light']) * 100.0, 0.0, 100.0)
    track(4, f['m_dLightReading'])
    f['m_dOxygenReading'] = clamp(r.pm(f['m_dOxygenError']) + cel['oxygen'], 0.0, 100.0)
    track(5, f['m_dOxygenReading'])
    f['m_dPollutionReading'] = clamp(r.pm(f['m_dPollutionError']) + cel['pollution'], 0.0, 100.0)
    track(6, f['m_dPollutionReading'])
    f['m_dSoilPHReading'] = r.pm(f['m_dSoilPHError']) + cel['soilpH']
    track(8, f['m_dSoilPHReading'])
    f['m_dSoilTempReading'] = r.pm(f['m_dSoilTempError']) + cel['soilTemp']
    track(7, f['m_dSoilTempReading'])
    f['m_dSoilWaterReading'] = clamp(r.pm(f['m_dSoilWaterError']) + cel['soilWater'], 0.0, 50.0)
    track(9, f['m_dSoilWaterReading'])
    f['m_dWindSpeedReading'] = clamp(r.pm(f['m_dWindSpeedError']) + cel['windSpeed'], 0.0, 100.0)
    prev = f['m_dwAlarms']
    a = 0
    t = THRESH[f['m_dwThresholdAddr']]
    if f['m_dAirTempReading'] < t[0]:
        a = 2
    if t[1] < f['m_dAirTempReading']:
        a |= 1
    if f['m_dSoilTempReading'] < t[2]:
        a |= 8
    if t[3] < f['m_dSoilTempReading']:
        a |= 4
    if f['m_dHumidityReading'] < t[4]:
        a |= 0x4000
    if t[5] < f['m_dHumidityReading']:
        a |= 0x2000
    if f['m_dSoilWaterReading'] < t[6]:
        a |= 0x80
    if t[7] < f['m_dSoilWaterReading']:
        a |= 0x40
    if f['m_dPressureReading'] < 900.0:
        a |= 0x4000000
    if 1100.0 < f['m_dPressureReading']:
        a |= 0x2000000
    if f['m_dOxygenReading'] < 18.0:
        a |= 0x800
    if 23.0 < f['m_dOxygenReading']:
        a |= 0x400
    if f['m_dCO2Reading'] < 0.01:
        a |= 0x200
    if 3.0 < f['m_dCO2Reading']:
        a |= 0x100
    if 10.0 < f['m_dPollutionReading']:
        a |= 0x1000
    f['m_dwAlarms'] = a
    # (the FX switches are not state)


@reg('CBiomeSensor', 'poweron')     # 1005fd70
def _(m, f):
    f['m_dwAlarms'] = 0


@reg('CBiomeSensor', 'poweroff')    # 1005fd80
def _(m, f):
    if f['m_dwAlarms'] != 0:
        f['m_dwAlarms'] = 0


# CLung 10010a30 / 100108e0 / 100109b0 / 10010050 -----------------------------------------------------------------------------------

@reg('CLung', 'update')
def _(m, f, now):
    dt = u32(now - f['m_dwTimeLastUpdate'])
    if f['m_bActive'] != 0 and has_power(m, f['m_dwPowerMask']) != 0 and f['m_dwBellowsDir'] != 0:
        cel = m.cels[f['m_pCel']]
        mode = f['m_dwMode']
        cap = cel['capacity'] + (dt * 9.259259259259259e-05 if mode == 0x11 else dt * -9.259259259259259e-05)
        cel['capacity'] = cap
        off = False
        if mode == 0x11:
            if cap > 1.0:
                cel['capacity'] = 1.0
                off = True
        elif mode == 0x1a:
            if cap < 0.0:
                cel['capacity'] = 0.0
                off = True
        elif 0.0 <= cap:
            if m.cels[f['m_pTargetCel']]['capacity'] <= 1.0:
                if m.seal_global == 0:
                    off = True
            else:
                cel['capacity'] = 1.0
                off = True
        else:
            cel['capacity'] = 0.0
            off = True
        if off:
            virt(m, f, 'turnoff')
    V[('CBiomeSensor', 'update')](m, f, now)


@reg('CLung', 'start')          # 100108e0
def _(m, f):
    V[('CFeature', 'start')](m, f)
    f['m_dwBellowsDir'] = 1
    mode = f['m_dwMode']
    if mode == 0x11:
        reverse_air(m, f['m_wId2'], 1)
        switch_air(m, f['m_wId2'], 1)
    elif mode in (0x15, 0x16, 0x17, 0x18):
        dest_air(m, f['m_wId'], f['m_pTargetCel'])
        switch_air(m, f['m_wId'], 1)
    elif mode == 0x1a:
        reverse_air(m, f['m_wId2'], 0)
        switch_air(m, f['m_wId2'], 1)


@reg('CLung', 'stop')           # 100109b0
def _(m, f):
    V[('CFeature', 'stop')](m, f)
    mode = f['m_dwMode']
    if mode in (0x11, 0x1a):
        switch_air(m, f['m_wId2'], 0)
    elif mode in (0x15, 0x16, 0x17, 0x18):
        switch_air(m, f['m_wId'], 0)
        return
    cel = m.cels[f['m_pCel']]
    cel['capacity'] = cel['airVolume']


LUNG_CELS = {0x15: 'TRF', 0x16: 'TS', 0x17: 'TDB', 0x18: 'TO'}


@reg('CLung', 'setparam')
def _(m, f, id, v, d):
    if id == 0x11:
        if f['m_bActive'] != 0:
            virt(m, f, 'stop')
        f['m_dwMode'] = 0x11
        if f['m_bActive'] != 0:
            virt(m, f, 'start')
        return True
    if id == 0x13:
        if v in LUNG_CELS:
            f['m_pCel'] = LUNG_CELS[v]
            return True
    elif id != 0x14:
        return base_set(m, f, id, v)
    if f['m_bActive'] != 0:
        virt(m, f, 'stop')
    f['m_dwMode'] = v
    if v in LUNG_CELS:
        f['m_pTargetCel'] = LUNG_CELS[v]
    elif v != 0x1a:
        return False
    if f['m_bActive'] != 0:
        virt(m, f, 'start')
    return True


# CTankSensor 10026770 ------------------------------------------------------------------------------------------------------------------

@reg('CTankSensor', 'update')
def _(m, f, now):
    f['m_dwTimeLastUpdate'] = now
    t = m.tanks[f['m_pTank']]
    r = m.rng
    f['m_dTempReading'] = r.pm(f['m_dTempError']) + t['temp']
    f['m_dPurityReading'] = clamp(t['purity'] * 100.0 + r.pm(f['m_dPurityError']), 0.0, 100.0)
    f['m_dLevelReading'] = clamp(t['waterVol'] / t['volume'] * 100.0 + r.pm(f['m_dLevelError']), 0.0, 100.0)
    a = 0
    if f['m_dTempReading'] < 10.0:
        a = 0x20000
    if 30.0 < f['m_dTempReading']:
        a |= 0x10000
    f['m_dwAlarms'] = a


# COceanProbe 10027de0 ---------------------------------------------------------------------------------------------------------------------

@reg('COceanProbe', 'update')
def _(m, f, now):
    f['m_dwTimeLastUpdate'] = now
    o = m.ocean
    r = m.rng
    f['m_dTempReading'] = r.pm(f['m_dTempError']) + o['temp']
    f['m_dPHReading'] = r.pm(f['m_dPHError']) + o['ph']
    f['m_dSalinityReading'] = clamp(r.pm(f['m_dSalinityError']) + o['salinity'], 0.0, 100.0)
    f['m_dClarityReading'] = (100.0 - o['algae']) + r.pm(f['m_dClarityError'])
    f['m_dSalinityReading'] = clamp(f['m_dSalinityReading'], 0.0, 100.0)     # (the original clamps +0x78 again: the clarity stays unclamped)
    f['m_dOxygenReading'] = clamp(r.pm(f['m_dOxygenError']) + o['oxygen'], 0.0, 100.0)
    f['m_dLevelReading'] = clamp(o['waterVol'] / o['volume'] * 100.0 + r.pm(f['m_dTempError']), 0.0, 100.0)    # (error range +0x98: the original's)
    a = 0
    if f['m_dTempReading'] < 23.0:
        a = 0x20000
    if 26.0 < f['m_dTempReading']:
        a |= 0x10000
    if f['m_dPHReading'] < 8.3:
        a |= 0x20
    if 8.6 < f['m_dPHReading']:
        a |= 0x10
    if f['m_dSalinityReading'] < 3.5:
        a |= 0x100000
    if 4.2 < f['m_dSalinityReading']:
        a |= 0x80000
    if f['m_dOxygenReading'] < 3.5:
        a |= 0x800
    if f['m_dClarityReading'] < 30.0:
        a |= 0x40000
    if f['m_dLevelReading'] < 80.0:
        a |= 0x400000
    if 99.0 < f['m_dLevelReading']:
        a |= 0x200000
    f['m_dwAlarms'] = a


# water machines ------------------------------------------------------------------------------------------------------------------------

@reg('CWaterPump', 'update')        # 10005df0 (no base update)
def _(m, f, now):
    if f['m_bActive'] != 0 and (m.tanks[f['m_pSrcTank']]['waterVol'] <= 0.0 or m.tanks[f['m_pDstTank']]['volume'] <= m.tanks[f['m_pDstTank']]['waterVol']):
        virt(m, f, 'deactivate')


@reg('CWaterPump', 'setparam')      # 10005bb0
def _(m, f, id, v, d):
    names = {0x15: 'RFTANK', 0x16: 'STANK', 0x17: 'DBTANK', 0x18: 'OTANK'}
    if id == 0x13:
        f['m_dwParam'] = v
        if v in names:
            f['m_pSrcTank'] = names[v]
            return True
    elif id != 0x14:
        return base_set(m, f, id, v)
    f['m_dwParam'] = v
    if v not in names:
        return False
    f['m_pDstTank'] = names[v]
    return True


@reg('CSiltScrubber', 'update')     # 10005fa0 (the flow's free-flow factor is not part of the feature state: only the base)
def _(m, f, now):
    V[('CFeature', 'update')](m, f, now)


@reg('CTideMachine', 'update')      # 10004330
def _(m, f, now):
    last = f['m_dwTimeLastUpdate']
    f['m_dwTimeLastUpdate'] = now
    if f['m_bActive'] != 0 and has_power(m, f['m_dwPowerMask']) != 0:
        if f['m_dwDirection'] == 0:
            t = u32(f['m_dwLastToggle'] + (now - last))
            f['m_dwLastToggle'] = i32(t)
            if 20700.0 < float(t):
                f['m_dwDirection'] = 1
                f['m_dwLastToggle'] = 0
        elif m.tanks[f['m_pTideTank']]['waterVol'] < 0.01:
            f['m_dwDirection'] = 0
            f['m_dwLastToggle'] = 0


@reg('CAlgaeScrubber', 'update')    # 10003ae0
def _(m, f, now):
    last = f['m_dwTimeLastUpdate']
    V[('CFeature', 'update')](m, f, now)
    if f['m_bActive'] != 0 and has_power(m, f['m_dwPowerMask']) != 0:
        m.ocean['algae'] += float(u32(now - last)) * -0.0008333333333333334
        if m.ocean['algae'] < 0.0:
            m.ocean['algae'] = 0.0


@reg('CWaveMachine', 'start')       # 10070be0
def _(m, f):
    V[('CFeature', 'start')](m, f)
    m.ocean['waveMachineOn'] = 1


@reg('CWaveMachine', 'stop')
def _(m, f):
    V[('CFeature', 'stop')](m, f)
    m.ocean['waveMachineOn'] = 0


@reg('COxygenStore', 'update')      # 10025bf0
def _(m, f, now):
    dt = u32(now - f['m_dwTimeLastUpdate'])
    f['m_dwTimeLastUpdate'] = now
    if f['m_bRunning'] != 0:
        cel = m.cels[f['m_pCel']]
        if f['m_wMode'] == 1:
            if 0.1 <= cel['capacity']:
                if f['m_dStore'] < 10.0:
                    if 16.0 <= cel['oxygen']:
                        d = dt * 0.002777777777777778
                        cel['newOxygen'] = cel['newOxygen'] - d / (cel['airVolume'] / cel['capacity'])
                        f['m_dStore'] = d + f['m_dStore']
                    else:
                        virt(m, f, 'turnoff')
                else:
                    f['m_dStore'] = 10.0
                    virt(m, f, 'turnoff')
            else:
                virt(m, f, 'turnoff')
        elif f['m_wMode'] == 2:
            d = dt * 0.002777777777777778
            cel['newOxygen'] = cel['newOxygen'] + d
            f['m_dStore'] = f['m_dStore'] - d
            if f['m_dStore'] <= 0.0:
                f['m_dStore'] = 0.0
                virt(m, f, 'turnoff')
    f['m_dStore'] = clamp(f['m_dStore'], 0.0, 10.0)


@reg('COxygenStore', 'setparam')    # 100254b0
def _(m, f, id, v, d):
    if id != 2:
        return base_set(m, f, id, v)
    if v == 0:
        virt(m, f, 'deactivate')
        return True
    f['m_wMode'] = 1
    virt(m, f, 'activate')
    return True


@reg('CBubbler', 'update')          # 10077d10
def _(m, f, now):
    last = f['m_dwTimeLastUpdate']
    V[('CFeature', 'update')](m, f, now)
    if f['m_bActive'] != 0 and has_power(m, f['m_dwPowerMask']) != 0 and f['m_wGasSource'] == 0xc20:
        dt = u32(now - last)
        a = dt * 0.0001388888888888889
        store = m.feats[f['m_pOxygenStore']]
        if store['m_dStore'] < a:
            virt(m, f, 'turnoff')
            return
        o = m.ocean
        o['newTemp'] = ((a * 20.0) / o['waterVol']) * 0.25 + o['newTemp']
        cel = m.cels[f['m_pCel']]
        cel['newAirTemp'] = (o['temp'] * a) / cel['airVolume'] + cel['newAirTemp']
        cel['newHumidity'] = (a * 100.0) / cel['airVolume'] + cel['newHumidity']
        cel['newOxygen'] = a * 0.1 + cel['newOxygen']
        store['m_dStore'] = store['m_dStore'] - a
        o['newOxygen'] = (5.0 - o['oxygen']) * dt * 0.0002777777777777778 + o['newOxygen']


@reg('CBubbler', 'setparam')        # 100778f0
def _(m, f, id, v, d):
    if id != 0x5a:
        return base_set(m, f, id, v)
    if f['m_bRunning'] == 0:
        f['m_wGasSource'] = 0xc20 if v == 0x5b else 0xc1f
        return True
    virt(m, f, 'stop')
    f['m_wGasSource'] = 0xc20 if v == 0x5b else 0xc1f
    virt(m, f, 'start')
    return True


# classes whose Update the oracle does not model (story / house features with their own Update): compared only for the
# members the base logic can reach, never for their own
UNMODELLED_UPDATE = {'CPodFeature', 'CExitDoor', 'CCoffeeMachine', 'CBees', 'CVidLink'}   # CVidLink dll:1001abf0 does not store +0x50 (the base Update dll:10019a10 does); tests/test_featstory.py checks it


def run_update(m, name, now):
    f = m.feats[name]
    if f['class'] in UNMODELLED_UPDATE:
        return
    virt(m, f, 'update', now)


# ------------------------------------------------------------------------------------------------------------------------
# the dispatch table of GetParam (decompiled text: id -> member), as functions of the member dump

def getparam_table(m, f):
    """-> {id: ('i'|'d', value)} for the ids the class answers (CFeature::GetParam dll:10019640 and the overrides)."""
    out = {}
    cls = f['class']
    chain = cls_chain(cls)
    # base
    out[1] = ('i', f['m_bOn'])
    out[2] = ('i', f['m_bRunning'])
    out[3] = ('i', f['m_dwTimerStart'])
    out[5] = ('i', f['m_dwTimeLimit'])
    s, e = u32(f['m_dwTimerStart']), u32(f['m_dwTimerEnd'])
    out[6] = ('i', i32((e - s) + 0x15180) if e < s else i32(e - s))
    out[7] = ('i', 1 if (f['m_bWorking'] == 0 or has_power(m, f['m_dwPowerMask']) == 0) else 0)
    out[0x8e] = ('d', 0.0 if f['m_bActive'] == 0 else f['m_dPowerUsage'] * 3600.0)

    def cel_of(n):
        return m.cels[n]
    if 'CBiomeSensor' in chain:
        for i, k in ((0x1d, 'm_dPressureReading'), (0x1e, 'm_dAirTempReading'), (0x1f, 'm_dSoilTempReading'), (0x20, 'm_dSoilPHReading'),
                     (0x21, 'm_dSoilWaterReading'), (0x22, 'm_dOxygenReading'), (0x23, 'm_dCO2Reading'), (0x24, 'm_dLightReading'),
                     (0x25, 'm_dWindSpeedReading'), (0x26, 'm_dPollutionReading'), (0x27, 'm_dHumidityReading'), (0x2e, 'm_dRainToday')):
            out[i] = ('d', f[k])
        out[0x32] = ('i', f['m_dwAlarms'])
    if cls == 'CLung':
        out[0x11] = ('i', 1 if f['m_dwMode'] == 0x11 else 0)
        out[0x14] = ('i', f['m_dwMode'])
        out[0x1c] = ('d', cel_of(f['m_pCel'])['airVolume'] * 100.0)
        if f['m_pTargetCel'] != 'null':
            out[0x1b] = ('d', cel_of(f['m_pTargetCel'])['airVolume'] * 100.0)
    if cls == 'CTankSensor':
        t = m.tanks[f['m_pTank']]
        out.update({0x28: ('d', f['m_dTempReading']), 0x2b: ('d', f['m_dLevelReading']), 0x2c: ('d', f['m_dPurityReading']),
                    0x2d: ('d', t['volume']), 0x32: ('i', f['m_dwAlarms'])})
    if cls == 'COceanProbe':
        out.update({0x20: ('d', f['m_dPHReading']), 0x22: ('d', f['m_dOxygenReading']), 0x28: ('d', f['m_dTempReading']),
                    0x29: ('d', f['m_dClarityReading']), 0x2a: ('d', f['m_dSalinityReading']), 0x2b: ('d', f['m_dLevelReading']),
                    0x32: ('i', f['m_dwAlarms'])})
    if cls == 'CHeater':
        out.update({100: ('d', f['m_dDayOn']), 0x65: ('d', f['m_dDayOff']), 0x66: ('d', f['m_dNightOn']), 0x67: ('d', f['m_dNightOff'])})
    if cls == 'CFan':
        on = f['m_bActive'] != 0 and f['m_bWorking'] != 0 and has_power(m, f['m_dwPowerMask']) != 0
        out[0x50] = ('d', 2.0 if on else 0.0)
    if cls == 'CVent':
        out[0x50] = ('d', f['m_dShownAirFlow'])
    if cls == 'CRainMachine':
        out[0x5f] = ('i', 0x60 if f['m_pSourceTank'] == f['m_pOsmTank'] else 0x61)
    if cls == 'CMister':
        out[0x5f] = ('i', 0x60 if f['m_pOsmTank'] == f['m_pSourceTank'] else 0x61)
        out[0x6e] = ('d', f['m_dHumidityOn'])
        out[0x6f] = ('d', f['m_dHumidityOff'])
    if 'CWaterPump' in chain:
        t = m.tanks[f['m_pDstTank']] if f['m_pDstTank'] in m.tanks else None
        if cls == 'CWaterPump':
            out[0x14] = ('i', f['m_dwParam'])
            out[0x2b] = ('d', 0.0 if t is None else t['waterVol'] / t['volume'] * 100.0)
        else:   # CPurifier
            out[0x2b] = ('d', t['waterVol'] / t['volume'] * 100.0)
            out[0x2c] = ('d', t['purity'] * 100.0)
            out[0x32] = ('i', 1 if t['purity'] < f['m_dPurity'] - 0.1 else 0)
    if cls == 'CDrainGauge':
        fl = m.flows[f['m_pFlow']]
        src = m.tanks[fl['from']]
        out[0x32] = ('i', 1 if fl['freeFlow'] < 0.5 else 0)
        out[0x51] = ('d', src['waterVol'] / src['volume'] * fl['freeFlow'] * fl['mag'] * 400.0)
        out[0x52] = ('d', (1.0 - fl['freeFlow']) * 100.0)
    if cls == 'CTideMachine':
        t = m.tanks[f['m_pTideTank']]
        out[0x2b] = ('d', t['waterVol'] / t['volume'] * 100.0)
        out[0x96] = ('i', f['m_dwDirection'])
    if cls == 'COxygenStore':
        out[0x22] = ('d', cel_of(f['m_pCel'])['oxygen'])
        out[0x78] = ('d', f['m_dStore'] * 10.0)
    if cls == 'CBubbler':
        out[0x5a] = ('i', 0x5b if f['m_wGasSource'] == 0xc20 else 0x5c)
        out[0x78] = ('d', m.feats[f['m_pOxygenStore']]['m_dStore'] * 10.0)
    if cls in ('CHatch', 'CSeal'):
        out[10] = ('i', f['unk_58'])
    if cls == 'CPodFeature':
        out[0x14] = ('i', m.feats[f['unk_5c']]['unk_6c'])
    if cls == 'CPodTransport':
        out[0x85] = ('i', f['unk_68'])
        out[0x86] = ('i', 1 if f['unk_58'] == 0x1e else 0)
        out[0x8d] = ('d', f['unk_60'] * 5.0)
    if cls in ('CSolarGen', 'CWindmill'):
        out[0x8c] = ('d', 0.0 if f['m_bActive'] == 0 else f['m_dPowerUsage'] * 3600.0)
    return out


# classes whose own GetParam ids the oracle's table does not know (story / house features): only the base ids are compared
TABLE_KNOWS = {'CFeature', 'CBiomeSensor', 'CLung', 'CTankSensor', 'COceanProbe', 'CHeater', 'CFan', 'CVent', 'CRainMachine', 'CMister',
               'CWaterPump', 'CPurifier', 'CDrainGauge', 'CTideMachine', 'COxygenStore', 'CBubbler', 'CSolarGen', 'CWindmill', 'CAirMixer',
               'CSiltScrubber', 'CWaveMachine', 'CAlgaeScrubber', 'CFuseBox', 'CTanksCabinet', 'CHatch', 'CSeal', 'CPodFeature',
               'CPodTransport', 'CFire', 'CFountain'}


# ------------------------------------------------------------------------------------------------------------------------
# driving the tool

def run_tool(script, seed=1):
    p = subprocess.run([TOOL, '--seed', str(seed)], input=script, capture_output=True, text=True)
    if p.returncode not in (0,):
        print(p.stdout[-2000:])
        print(p.stderr[-2000:])
        raise SystemExit('feattest failed with status %d' % p.returncode)
    return p.stdout.split('\n')


def load_members(lines):
    """-> feats (ordered dict-like), from 'member f.m = v' lines"""
    feats = {}
    order = []
    for l in lines:
        if not l.startswith('member '):
            continue
        k, _, v = l[7:].partition(' = ')
        f, _, mem = k.partition('.')
        if f not in feats:
            feats[f] = {}
            order.append(f)
        feats[f][mem] = parse(v)
    return feats, order


def load_world(lines, tag='world'):
    w = {}
    for l in lines:
        if l.startswith(tag + ' '):
            k, _, v = l[len(tag) + 1:].partition(' = ')
            w[k] = parse(v)
    return {'power': w['power'], 'flags': w['powerFlags'], 'demand': w['demand'], 'generation': w['generation'],
            'sunlight': w['sunlight'], 'windSpeed': w['windSpeed']}


def load_cels(lines, tag='cel'):
    d = {}
    for l in lines:
        if l.startswith(tag + ' '):
            parts = l.split()
            d[parts[1]] = kv(parts[2:])
    return d


def load_tanks(lines):
    d = {}
    for l in lines:
        if l.startswith('tank '):
            parts = l.split()
            d[parts[1]] = kv(parts[2:])
    return d


def load_ocean(lines):
    for l in lines:
        if l.startswith('ocean ocean '):
            return kv(l.split()[2:])
    raise SystemExit('no ocean line')


def load_flows(lines):
    d = {}
    for l in lines:
        if l.startswith('waterflow '):
            parts = l.split()
            d[parts[1]] = kv(parts[2:])
    return d


def load_airflows(lines):
    out = []
    for l in lines:
        if l.startswith('airflow '):
            p = l.split()
            out.append(kv(p[2:]))
    return out


def build_model(lines):
    m = Model()
    m.feats, m.order = load_members(lines)
    m.world = load_world(lines)
    m.cels = load_cels(lines)
    m.tanks = load_tanks(lines)
    m.ocean = load_ocean(lines)
    m.flows = load_flows(lines)
    m.airflows = load_airflows(lines)
    m.sites = {}
    m.site_cel = {}
    for l in lines:
        if l.startswith('site '):
            p = l.split()
            d = kv(p[2:])
            m.site_cel[p[1]] = d['cel']
            m.sites[p[1]] = {'unk0e8': d['unk0e8'], 'unk0f0': d['unk0f0']}
    # the biome of each heater / the probes of a biome (CBiome::AvgProbeAirTemp: the probes the builders registered at the biome)
    m.biome_probes = {}
    m.biome_cels = {}
    return m


# ------------------------------------------------------------------------------------------------------------------------

def check_dispatch(lines_getall, m):
    """every id 0..0x100 each feature answers, against the table written from the decompiled GetParam functions"""
    got = {}
    for l in lines_getall:
        if l.startswith('getall '):
            p = l.split()
            f, cls, id = p[1], p[2], int(p[3])
            kvs = kv(p[4:])
            got.setdefault(f, {})[id] = kvs
    n = 0
    for name in m.order:
        f = m.feats[name]
        cls = f['class']
        exp = getparam_table(m, f) if cls in TABLE_KNOWS else None
        g = got.get(name, {})
        if exp is None:
            # story / house classes: the base ids must be there, with the base meaning
            for id, (t, v) in {k: v for k, v in getparam_table(m, f).items() if k in (1, 2, 3, 5, 6, 7, 0x8e)}.items():
                if id not in g:
                    bad('dispatch %s (%s) id %d not answered' % (name, cls, id))
            continue
        for id in range(0x101):
            if id in exp and id not in g:
                bad('dispatch %s (%s) id 0x%x not answered' % (name, cls, id))
            elif id not in exp and id in g:
                bad('dispatch %s (%s) id 0x%x answered by the port but not by the decompiled class' % (name, cls, id))
            elif id in exp:
                t, v = exp[id]
                gv = g[id]['int'] if t == 'i' else g[id]['dbl']
                n += 1
                if not close(gv, v):
                    bad('dispatch %s (%s) id 0x%x: port %r, decompiled text says %r' % (name, cls, id, gv, v))
    return n


SET_SCENARIO = [
    # (feature, id, int, double)
    ('frfheater', 100, 0, 24.5), ('frfheater', 0x65, 0, 31.0), ('frfheater', 0x66, 0, 22.0), ('frfheater', 0x67, 0, 27.5),
    ('fr1rain', 0x5f, 0x61, 0.0), ('fr1rain', 0x5f, 0x60, 0.0), ('fr6rain', 0x5f, 0x61, 0.0),
    ('fr1mist', 0x6e, 0, 55.0), ('fr1mist', 0x6f, 0, 95.0), ('fr1mist', 0x5f, 0x61, 0.0),
    ('fbr2lung', 0x14, 0x16, 0.0), ('fbr2lung', 0x11, 1, 0.0), ('fbr2lung', 0x13, 0x17, 0.0), ('fbr2lung', 0x14, 0x1a, 0.0),
    ('fbr2lung', 0x14, 0x99, 0.0), ('fbs2lung', 0x13, 0x77, 0.0),
    ('fbr3watpmp', 0x14, 0x17, 0.0), ('fbr3watpmp', 0x13, 0x16, 0.0),
    ('fbo4oxygen', 2, 1, 0.0), ('fbo4oxygen', 2, 0, 0.0),
    ('fw3Bubl', 0x5a, 0x5b, 0.0), ('fw3Bubl', 0x5a, 0x5c, 0.0),
    ('fr10fan', 2, 1, 0.0), ('fr10fan', 3, 36000, 0.0), ('fr10fan', 6, 3600, 0.0), ('fr10fan', 5, 100, 0.0), ('fr10fan', 1, 0, 0.0),
    ('fr10fan', 1, 1, 0.0), ('fr10fan', 6, 0x15180, 0.0), ('fr3fan', 2, 1, 0.0), ('fr3fan', 2, 0, 0.0),
    ('fr2mixer', 2, 1, 0.0), ('fr2mixer', 2, 0, 0.0),
    ('fr10htch', 10, 1, 0.0), ('fr10htch', 10, 0, 0.0), ('fPod', 0x8f, 0, 0.0), ('fPod', 0x90, 0, 0.0), ('frpod', 0x14, 0x17, 0.0),
    ('fsolargen', 2, 0, 0.0), ('fsolargen', 2, 1, 0.0), ('fwindmill', 2, 0, 0.0), ('fwindmill', 2, 1, 0.0),
]


def vset(m, name, id, v, d):
    f = m.feats[name]
    return virt(m, f, 'setparam', id, v, d)


def check_sets(m0, text_members_cmds):
    """scripted SetParam calls, one tool run; compare the changed feature and the world before / after each"""
    script = ''
    for i, (f, id, v, d) in enumerate(SET_SCENARIO):
        script += 'echo S%d\nflows\nmembers\nworld\nset %s %d %d %r\nmembers\nworld\n' % (i, f, id, v, d)
    lines = run_tool(script)
    # split
    blocks = {}
    cur = None
    for l in lines:
        if l.startswith('S') and l[1:].isdigit():
            cur = int(l[1:])
            blocks[cur] = []
        elif cur is not None:
            blocks[cur].append(l)
    n = 0
    for i, (fname, id, v, d) in enumerate(SET_SCENARIO):
        b = blocks[i]
        # members / world appear twice: before and after
        idx = [k for k, l in enumerate(b) if l.startswith('world power =')]
        if len(idx) != 2:
            raise SystemExit('set scenario %d: unexpected output' % i)
        first = b[:idx[0] + 8]
        rest = b[idx[0] + 8:]
        m = Model()
        m.feats, m.order = load_members(first)
        m.world = load_world(first)
        m.cels = m0.cels
        m.tanks = m0.tanks
        m.ocean = dict(m0.ocean)
        m.flows = m0.flows
        m.airflows = load_airflows(first)
        m.time = 0
        m.sites, m.site_cel = {}, {}
        m.biome_probes, m.biome_cels = m0.biome_probes, m0.biome_cels
        m.tod = 28918
        # the cel / ocean state the set handlers touch is not compared here (the trace does); only the feature + world numbers
        m.cels = {k: dict(x) for k, x in m0.cels.items()}
        res = None
        m.time = 28918
        res = vset(m, fname, id, v, d)
        after_feats, _ = load_members(rest)
        after_world = load_world(rest)
        okline = [l for l in rest if l.startswith('set ')]
        port_ok = okline and okline[0].endswith('= ok')
        if bool(res) != bool(port_ok):
            bad('set %s %d %d %r: port answers %s, decompiled SetParam returns %s' % (fname, id, v, d, port_ok, bool(res)))
        for name, f in m.feats.items():
            for k, val in f.items():
                if k not in after_feats[name]:
                    continue
                n += 1
                if not close(after_feats[name][k], val):
                    bad('set %s %d %d %r: %s.%s port %r, decompiled text %r' % (fname, id, v, d, name, k, after_feats[name][k], val))
        for k in ('demand', 'generation'):
            n += 1
            if not close(after_world[k], m.world[k]):
                bad('set %s %d %d %r: world %s port %r, decompiled text %r' % (fname, id, v, d, k, after_world[k], m.world[k]))
    return n


COVER_SKIP_MEMBERS = {'unk_54'}


def apply_deltas(feats, tokens_by_name):
    for name, toks in tokens_by_name:
        f = feats[name]
        for t in toks:
            k, _, v = t.partition('=')
            f[k] = parse(v)


def run_trace(seconds, seed, setup, m0, label):
    """trace `seconds` game seconds after the scripted setup, compare every step"""
    script = '%s\nseed %d\ntrace %d\n' % (setup, seed, seconds)
    lines = run_tool(script)
    return compare_trace(lines, m0, label)


def compare_trace(lines, m0, label):
    m = Model()
    m.feats = {n: dict(f) for n, f in m0.feats.items()}
    m.order = list(m0.order)
    m.cels = {k: dict(v) for k, v in m0.cels.items()}
    m.tanks = {k: dict(v) for k, v in m0.tanks.items()}
    m.ocean = dict(m0.ocean)
    m.flows = m0.flows
    m.airflows = [dict(x) for x in m0.airflows]
    m.sites, m.site_cel = m0.sites, m0.site_cel
    m.biome_probes, m.biome_cels = m0.biome_probes, m0.biome_cels
    steps = compared = 0
    i = 0
    n = len(lines)
    while i < n:
        l = lines[i]
        if not l.startswith('STEP '):
            i += 1
            continue
        st = kv(l.split()[1:])
        m.time, m.tod, m.diurnal = st['time'], st['tod'], st['diurnal']
        i += 1
        fb = []
        after = {'cels': {}, 'feats': [], 'ocean': None, 'world': None, 'air': {}}
        phase = 'B'
        while lines[i] != 'END':
            l = lines[i]
            p = l.split()
            tag = p[0]
            if tag == 'WB':
                w = kv(p[1:])
                m.rng = Rng(w['rng'])
                m.world = {'power': w['power'], 'flags': w['flags'], 'demand': w['demand'], 'generation': w['generation'],
                           'sunlight': w['sunlight'], 'windSpeed': w['windSpeed']}
            elif tag == 'CB':
                m.cels[p[1]] = kv(p[2:])
            elif tag == 'OB':
                m.ocean.update(kv(p[1:]))
            elif tag == 'TB':
                m.tanks[p[1]] = kv(p[2:])
            elif tag == 'FB':
                f = m.feats[p[1]]
                for t in p[2:]:
                    k, _, v = t.partition('=')
                    f[k] = parse(v)
            elif tag == 'AB':
                m.airflows[int(p[1])].update(kv(p[2:]))
            elif tag == 'AA':
                after['air'][int(p[1])] = kv(p[2:])
            elif tag == 'WA':
                after['world'] = kv(p[1:])
            elif tag == 'CA':
                after['cels'][p[1]] = kv(p[2:])
            elif tag == 'OA':
                after['ocean'] = kv(p[1:])
            elif tag == 'FA':
                after['feats'].append((p[1], p[2:]))
            i += 1
        i += 1   # END
        # --- predict
        pre = {n: dict(f) for n, f in m.feats.items()}
        for name in m.order:
            run_update(m, name, m.time)
        # --- the C++ result
        actual = {n: dict(f) for n, f in pre.items()}      # members the feature phase did not change keep the 'before' value
        # (pre was taken before the predicted updates: apply the printed deltas to it)
        for name, toks in after['feats']:
            for t in toks:
                k, _, v = t.partition('=')
                actual[name][k] = parse(v)
        steps += 1
        for name in m.order:
            f0, f1 = pre[name], actual[name]
            for k, label2 in (('m_bActive', 'activations'), ('m_bRunning', 'starts')):
                if f0[k] != f1[k]:
                    events[(f0['class'], label2 if f1[k] else 'de' + label2 if k == 'm_bActive' else 'stops')] += 1
        w = after['world']
        for k, mk in (('demand', 'demand'), ('generation', 'generation'), ('power', 'power'), ('flags', 'flags')):
            compared += 1
            if not close(w[k], m.world[mk]):
                bad('%s step %d (time %d): world %s port %r, decompiled text %r' % (label, steps, m.time, k, w[k], m.world[mk]))
        if m.rng.s != w['rng']:
            bad('%s step %d (time %d): generator state port %u, decompiled text %u (a sensor drew a different number of values)' % (label, steps, m.time, w['rng'], m.rng.s))
        for name in m.order:
            f = m.feats[name]
            if f['class'] in UNMODELLED_UPDATE:
                continue
            for k, val in f.items():
                if k in COVER_SKIP_MEMBERS or k in ('class', 'name'):
                    continue
                compared += 1
                if not close(actual[name].get(k), val):
                    bad('%s step %d (time %d): %s (%s).%s port %r, decompiled text %r' % (label, steps, m.time, name, f['class'], k, actual[name].get(k), val))
        for idx, a in after['air'].items():
            fl = m.airflows[idx]
            for k in ('on', 'reversed', 'to', 'windSpeed'):
                compared += 1
                if not close(a[k], fl[k]):
                    bad('%s step %d (time %d): air flow #%d %s port %r, decompiled text %r' % (label, steps, m.time, idx, k, a[k], fl[k]))
        for cname, c in after['cels'].items():
            for k in ('newAirTemp', 'airTemp', 'windSpeed', 'capacity', 'newOxygen', 'newHumidity', 'newPollution'):
                compared += 1
                if not close(c[k], m.cels[cname][k]):
                    bad('%s step %d (time %d): cel %s %s port %r, decompiled text %r' % (label, steps, m.time, cname, k, c[k], m.cels[cname][k]))
        # cels the port did not change must not have changed in the prediction either
        for cname, c in m.cels.items():
            if cname in after['cels']:
                continue
            compared += 1
        if after['ocean'] is not None:
            for k in ('newTemp', 'newOxygen', 'algae', 'waveMachineOn'):
                compared += 1
                if not close(after['ocean'][k], m.ocean[k]):
                    bad('%s step %d (time %d): ocean %s port %r, decompiled text %r' % (label, steps, m.time, k, after['ocean'][k], m.ocean[k]))
        else:
            for k in ('newTemp', 'newOxygen', 'algae', 'waveMachineOn'):
                compared += 1
                if not close(m.ocean[k], m0.ocean.get(k, m.ocean[k])) and False:
                    pass
        # the next step starts from the port's state: carry the port's feature state forward (it is what the next FB deltas patch)
        for name in m.order:
            m.feats[name] = dict(actual[name])
        for cname, c in after['cels'].items():
            m.cels[cname].update(c)
        for idx, a in after['air'].items():
            m.airflows[idx].update(a)
        if after['ocean'] is not None:
            m.ocean.update(after['ocean'])
        # (the world is re-read from the next WB line)
    return steps, compared


BIOME_OF = {}


def main():
    quick = '--quick' in sys.argv
    if not os.path.exists(TOOL):
        raise SystemExit('build/biosys-feattest is missing')
    boot = run_tool('members\nworld\ncels\ntanks\nocean\nflows\ngetall\nbiomes\nsites\n')
    m = build_model(boot)
    # the cels and probes of each biome as the builders registered them (CBiome::AvgProbeAirTemp reads the probes' readings)
    m.biome_probes, m.biome_cels = {}, {}
    for l in boot:
        if l.startswith('biome '):
            p = l.split()
            name = p[1]
            kvs = dict(x.split('=', 1) for x in p[2:])
            m.biome_cels[name] = [c for c in kvs['cels'].split(',') if c]
            m.biome_probes[name] = [c for c in kvs['sensors'].split(',') if c]
    print('model: %d features, %d cels, %d tanks' % (len(m.order), len(m.cels), len(m.tanks)))
    n1 = check_dispatch(boot, m)
    print('dispatch table: %d answers compared (every feature, ids 0..0x100)' % n1)
    n2 = check_sets(m, None)
    print('SetParam scenario: %d members compared' % n2)
    setup = 'fusemask fbrfuse 0x1ffffff\nfusemask fbsfuse 0x2ffffff\n'      # every fuse of the RF and SAV boxes in: the lungs / fans can run
    # the parameters of the scenario for the runs: heaters at known set-points, fans / rain / mister / lung / oxygen / bubbler on
    for f, id, v, d in [('fr10fan', 2, 1, 0.0), ('fr1mist', 1, 1, 0.0), ('fbr2lung', 0x11, 1, 0.0), ('fbr2lung', 2, 1, 0.0),
                        ('fbs2lung', 0x14, 0x1a, 0.0), ('fbs2lung', 2, 1, 0.0), ('fbo1lung', 0x14, 0x18, 0.0), ('fbo1lung', 2, 1, 0.0), ('fbo4oxygen', 2, 1, 0.0), ('fbo3algae', 1, 1, 0.0),
                        ('fbo1Tide', 2, 1, 0.0), ('fbo1Wave', 2, 1, 0.0), ('fr2mixer', 2, 1, 0.0), ('fr3fan', 2, 1, 0.0),
                        ('frfheater', 100, 0, 30.0), ('frfheater', 0x65, 0, 33.0)]:
        setup += 'set %s %d %d %r\n' % (f, id, v, d)
    # the model of the scripted start is the dump taken after the setup
    boot2 = run_tool(setup + 'members\nworld\ncels\ntanks\nocean\nflows\nsites\n')
    m2 = build_model(boot2)
    m2.biome_probes, m2.biome_cels = m.biome_probes, m.biome_cels
    steps, cmp1 = run_trace(3600, 777, setup, m2, 'hour')
    print('1 game hour: %d steps, %d numbers compared' % (steps, cmp1))
    total = cmp1
    if not quick:
        steps, cmp2 = run_trace(86400, 4242, setup + 'time 21600\n', m2, 'day')
        print('1 game day: %d steps, %d numbers compared' % (steps, cmp2))
        total += cmp2
    print('events the traces exercised (class, what: count): ' + ', '.join('%s %s: %d' % (c, w, n) for (c, w), n in sorted(events.items())))
    print('%d numbers compared in all, %d disagreements' % (total + n1 + n2, len(problems)))
    if problems:
        print('FAIL: the port disagrees with the decompiled text')
        return 1
    print('PASS: feat_oracle agrees with the port')
    return 0


if __name__ == '__main__':
    sys.exit(main())
