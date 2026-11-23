#!/usr/bin/env python3
"""Independent re-implementation of the simulation core of BiosGame.dll, written directly from the DECOMPILED text of
each update function (python3 -I analysis/scripts/dllpack.py show <addr>), not from the C++ port.

Every object is a dict keyed by the byte offset the decompiled code uses (`this[0x38]` is `*(double *)(this + 0x38)`);
pointers are references to the other dicts (None = NULL). Functions are named after the original and carry its address.

Inputs: the saved game (dynamic state, via analysis/scripts/biosys_savblob.py dump), the static data the blob does not
carry (`biosys-simtest static`: topology and constants of the world builders) and the start state of the random
generator. It runs the same schedule as `biosys-simtest run` and prints the same `section/item/member = value` lines.

usage: sim_oracle.py <file.sav> <static.tsv> --rng STATE --mode timer|advance --at S,S,... [--flows off]
Python standard library only.
"""
import os as _os
# the developer's folder above the repository (original discs, analysis notes); see WORKERS.md
_DEV = _os.environ.get('BIOSYS_DEV_ROOT') or _os.path.abspath(_os.path.join(_os.path.dirname(_os.path.abspath(__file__)), '..', '..'))
import struct, subprocess, sys

SCRIPTS = (_DEV + '/analysis/scripts')
DLL = (_DEV + '/analysis/bin/BiosGame.dll')


# ------------------------------------------------------------------------------------------------ the DLL image
def _image():
    data = open(DLL, 'rb').read()
    pe = struct.unpack_from('<I', data, 0x3c)[0]
    nsec = struct.unpack_from('<H', data, pe + 6)[0]
    optsz = struct.unpack_from('<H', data, pe + 20)[0]
    base = struct.unpack_from('<I', data, pe + 24 + 28)[0]
    secs = []
    for i in range(nsec):
        so = pe + 24 + optsz + 40 * i
        vsz, va, rsz, ro = struct.unpack_from('<IIII', data, so + 8)
        secs.append((va, vsz, ro, rsz))
    return data, base, secs


_IMG = None


def read_double(vma):
    global _IMG
    if _IMG is None:
        _IMG = _image()
    data, base, secs = _IMG
    rva = vma - base
    for va, vsz, ro, rsz in secs:
        if va <= rva < va + rsz:
            return struct.unpack_from('<d', data, rva - va + ro)[0]
    raise SystemExit('address not in the image: %x' % vma)


def read_table(addr, last):
    """LookUpVal (dll:10007a60) tables: (x, y) double pairs, entries 0..last."""
    return [(read_double(addr + 16 * k), read_double(addr + 16 * k + 8)) for k in range(last + 1)]


# ------------------------------------------------------------------------------------------------ C runtime
class Rand:
    """dll:10079380 _rand: holdrand = holdrand * 0x343fd + 0x269ec3; (holdrand & 0x7fff0000) >> 16"""
    def __init__(self, state):
        self.s = state & 0xffffffff

    def rand(self):
        self.s = (self.s * 0x343fd + 0x269ec3) & 0xffffffff
        return (self.s & 0x7fff0000) >> 16

    def range(self, p1, p2):
        """RandRange dll:100075a0: (p2 - p1) * rand * 3.051850947599719e-05 + p1"""
        i = self.rand()
        return (p2 - p1) * i * 3.051850947599719e-05 + p1


def Clamp(o, k, lo, hi):
    """dll:100074b0 Clamp(double *, lo, hi)"""
    if o[k] < lo:
        o[k] = lo
    elif hi < o[k]:
        o[k] = hi


U32 = 0xffffffff


# ------------------------------------------------------------------------------------------------ biome (CBiome)
def biome_update_target_temp(S, b):
    """dll:1000a0d0 CBiome::UpdateTargetTemp: _DAT_100c11f0 = sunlight (world+0x290), _DAT_100c1168 = world+0x208"""
    if (b[0x60] & 1) == 0:
        b[0x58] = b[0x50] * S.W[0x290] + S.W[0x208]
    else:
        b[0x58] = b[0x68]


# ------------------------------------------------------------------------------------------------ CBiomeCel
def cel_update_state(S, this, param_1):
    """dll:10018530 CBiomeCel::UpdateState"""
    W = S.W
    dVar6 = float(param_1)
    this[0xa0] = (((this[0x98] * -0.01 + 1.0) * this[0x10][0x50] * W[0x290] + W[0x208]) - this[0x38]) * dVar6 * 8.333333333333333e-05 + this[0xa0]
    if 33.0 <= this[0xa0]:
        this[0x108] = this[0x108] | 8
    dVar7 = (this[0x38] - this[0x40]) * dVar6 * 0.0002777777777777778
    this[0xa8] = dVar7 * 0.3333333333333333 + this[0xa8]
    this[0xa0] = this[0xa0] - dVar7
    iVar4 = this[0x14]
    dVar7 = 1.0 - this[0x98] * 0.01
    this[0x88] = dVar7
    this[0x60] = dVar7 * W[0x290]
    if iVar4 is not None:
        uVar8 = this[0x108]
        this[0x70] = (46.5 / iVar4[0x10]) * iVar4[0x18] + 1.0
        if (uVar8 & 4) != 0:
            bVar5 = this[0x70] < 20.0
            if (uVar8 & 0x10) == 0:
                if (not bVar5) and ((uVar8 & 8) != 0):
                    uVar8 = uVar8 | 0x10
                    this[0x108] = uVar8
            elif bVar5:
                uVar8 = uVar8 & 0xffffffef
                this[0x108] = uVar8
        if (this[0x108] & 2) != 0:
            this[0x78] = S.time
    if this[0x18] is not None:
        if this[0x48] <= 0.0 or this[0x38] <= this[0x10][0x58]:
            if S.DAT_100a10d8 != 0:
                S.switch_water_flows(this[0x1c], 0)       # CWorld::SwitchWaterFlows(world, id, 0)
                S.DAT_100a10d8 = 0
        else:
            if S.DAT_100a10d8 == 0:
                S.switch_water_flows(this[0x1c], 1)
                S.DAT_100a10d8 = 1
            this[0xb0] = this[0x48] * 0.01 * dVar6 * -0.002777777777777778 + this[0xb0]
            Clamp(this, 0xb0, 0.0, 100.0)
    if (this[0xa0] < -20.0 or 60.0 < this[0xa0]) and S.DAT_100a10dc == 0:
        S.log.append('CBiomeCel::UpdateState  m_dAirTemp out of range: %f' % this[0x38])
        S.DAT_100a10dc = 1
    Clamp(this, 0xa0, -20.0, 50.0)
    this[0x38] = this[0xa0]
    Clamp(this, 0xa8, -20.0, 50.0)
    this[0x40] = this[0xa8]
    Clamp(this, 0xb8, 0.0, 100.0)
    this[0x50] = this[0xb8]
    Clamp(this, 0xc0, 0.0, 100.0)
    this[0x58] = this[0xc0]
    Clamp(this, 0xb0, 0.0, 100.0)
    this[0x48] = this[0xb0]
    Clamp(this, 0xc8, 0.0, 100.0)
    this[0x98] = this[0xc8]


def cel_update_plant_health_avg(this):
    """dll:10018860 CBiomeCel::UpdatePlantHealthAvg"""
    iVar7 = 0
    if len(this['plants']) != 0:            # *(int *)(param_1 + 0xdc): the list's count
        dVar4 = this[0xf0]
        dVar3 = this[0xf0]
        this[0xf0] = 0.0
        for p in this['plants']:
            if (p[0xc] & 8) == 0:
                iVar7 = iVar7 + 1
                this[0xf0] = p[0x78] + this[0xf0]
        if 0 < iVar7:
            this[0xf0] = this[0xf0] / float(iVar7)
        if this[0x100] < this[0xf0]:
            this[0x100] = this[0xf0]
        if this[0xf0] < this[0xf8]:
            this[0xf8] = this[0xf0]
        if dVar3 < this[0xf0]:
            uVar6 = this[0x108] & 0xffffffbf
            this[0x108] = uVar6
            this[0x108] = uVar6 | 0x20
            return
        if this[0xf0] < dVar4:
            uVar6 = this[0x108]
            this[0x108] = uVar6 | 0x40
            this[0x108] = uVar6 & 0xffffffdf | 0x40


def cel_commit(iVar2):
    """dll:10013c10 CWorld::CommitBiomeCels, loop body"""
    iVar2[0x38] = iVar2[0xa0]
    iVar2[0x40] = iVar2[0xa8]
    iVar2[0x48] = iVar2[0xb0]
    iVar2[0x50] = iVar2[0xb8]
    iVar2[0x58] = iVar2[0xc0]
    iVar2[0x98] = iVar2[0xc8]


# ------------------------------------------------------------------------------------------------ air flows
def airflow_update(S, this, param_1):
    """dll:1005aca0 CAirFlow::Update (vtable +0x20)"""
    param_1 = param_1 & U32
    if this[0x2c] != 0:
        if this[0x34] != 0:
            iVar1 = this[4]
            iVar2 = this[8]
            dVar3 = float(param_1) * this[0x38] * 0.0001388888888888889
            dVar4 = (iVar1[0x28] / iVar1[0x20]) * dVar3
            dVar3 = (iVar2[0x28] / iVar2[0x20]) * dVar3
            dVar5 = (iVar1[0x28] - dVar4) + dVar3
            dVar6 = (iVar2[0x28] - dVar3) + dVar4
            iVar2[0xa0] = ((iVar1[0x38] - iVar2[0x38]) / dVar6) * dVar4 + iVar2[0xa0]
            iVar1 = this[4]
            iVar1[0xa0] = ((this[8][0x38] - iVar1[0x38]) / dVar5) * dVar3 + iVar1[0xa0]
            iVar1 = this[8]
            iVar1[0xb0] = ((this[4][0x48] - iVar1[0x48]) / dVar6) * dVar4 + iVar1[0xb0]
            iVar1 = this[4]
            iVar1[0xb0] = ((this[8][0x48] - iVar1[0x48]) / dVar5) * dVar3 + iVar1[0xb0]
            iVar1 = this[8]
            iVar1[0xc0] = ((this[4][0x58] - iVar1[0x58]) / dVar6) * dVar4 + iVar1[0xc0]
            iVar1 = this[4]
            iVar1[0xc0] = ((this[8][0x58] - iVar1[0x58]) / dVar5) * dVar3 + iVar1[0xc0]
            iVar1 = this[8]
            iVar1[0xb8] = ((this[4][0x50] - iVar1[0x50]) / dVar6) * dVar4 + iVar1[0xb8]
            iVar1 = this[4]
            iVar1[0xb8] = ((this[8][0x50] - iVar1[0x50]) / dVar5) * dVar3 + iVar1[0xb8]
            iVar1 = this[8]
            iVar1[200] = ((this[4][0x98] - iVar1[0x98]) / dVar6) * dVar4 + iVar1[200]
            iVar1 = this[4]
            iVar1[200] = ((this[8][0x98] - iVar1[0x98]) / dVar5) * dVar3 + iVar1[200]
            this[8][0x28] = dVar6
            this[4][0x28] = dVar5
            return
        dVar3 = this[0xc][0x10] * this[0x38]
        iVar1 = this[8]
        iVar1[0xa0] = (this[4][0x38] * dVar3) / iVar1[0x28] + iVar1[0xa0]
        iVar1 = this[4]
        iVar1[0xa0] = iVar1[0xa0] - (iVar1[0x38] * dVar3) / iVar1[0x28]
        iVar1 = this[8]
        iVar1[0xb0] = (this[4][0x48] * dVar3) / iVar1[0x28] + iVar1[0xb0]
        iVar1 = this[4]
        iVar1[0xb0] = iVar1[0xb0] - (iVar1[0x48] * dVar3) / iVar1[0x28]
        iVar1 = this[8]
        iVar1[0xc0] = (this[4][0x58] * dVar3) / iVar1[0x28] + iVar1[0xc0]
        iVar1 = this[4]
        iVar1[0xc0] = iVar1[0xc0] - (iVar1[0x58] * dVar3) / iVar1[0x28]
        iVar1 = this[8]
        iVar1[0xb8] = (this[4][0x50] * dVar3) / iVar1[0x28] + iVar1[0xb8]
        iVar1 = this[4]
        iVar1[0xb8] = iVar1[0xb8] - (iVar1[0x50] * dVar3) / iVar1[0x28]
        iVar1 = this[8]
        iVar1[200] = (this[4][0x98] * dVar3) / iVar1[0x28] + iVar1[200]
        iVar1 = this[4]
        iVar1[200] = iVar1[200] - (dVar3 * iVar1[0x98]) / iVar1[0x28]


def soilbed_update(S, this, param_1):
    """dll:1000b3d0 CSoilBedFlow::Update"""
    param_1 = param_1 & U32
    if this[0x2c] != 0:
        iVar1 = this[4]
        this[0x10] = float(param_1) * this[0x38] * 0.0001388888888888889
        if iVar1[0x28] < this[0x10]:
            this[0x10] = 0.0
            return
        iVar1[0xa8] = (this[0x10] * iVar1[0x38]) / (iVar1[0x28] * 3.0) + iVar1[0xa8]
        iVar1 = this[4]
        iVar1[0xa0] = iVar1[0xa0] - (iVar1[0x38] * this[0x10]) / iVar1[0x28]
        iVar1 = this[8]
        iVar1[0xa8] = (this[4][0x40] * this[0x10]) / (iVar1[0x28] * 3.0) + iVar1[0xa8]
        iVar1 = this[4]
        iVar1[0xa8] = iVar1[0xa8] - (iVar1[0x40] * this[0x10]) / (iVar1[0x28] * 3.0)
        iVar1 = this[8]
        iVar1[0xa0] = (iVar1[0x40] * this[0x10]) / iVar1[0x28] + iVar1[0xa0]
        iVar1 = this[8]
        iVar1[0xa8] = iVar1[0xa8] - (iVar1[0x40] * this[0x10]) / (iVar1[0x28] * 3.0)
        iVar1 = this[8]
        iVar1[0xb0] = (this[4][0x48] * this[0x10]) / iVar1[0x28] + iVar1[0xb0]
        iVar1 = this[4]
        iVar1[0xb0] = iVar1[0xb0] - (iVar1[0x48] * this[0x10]) / iVar1[0x28]
        iVar1 = this[8]
        iVar1[0xc0] = (this[4][0x58] * this[0x10]) / iVar1[0x28] + iVar1[0xc0]
        iVar1 = this[4]
        iVar1[0xc0] = iVar1[0xc0] - (iVar1[0x58] * this[0x10]) / iVar1[0x28]
        iVar1 = this[8]
        iVar1[0xb8] = (this[4][0x50] * this[0x10]) / iVar1[0x28] + iVar1[0xb8]
        iVar1 = this[4]
        iVar1[0xb8] = iVar1[0xb8] - (iVar1[0x50] * this[0x10]) / iVar1[0x28]
        iVar1 = this[4]
        iVar1[200] = iVar1[200] - (iVar1[0x98] * this[0x10]) / iVar1[0x28]


def airflow_turn_on(this, param_1):
    """dll:1005af40 CAirFlow::TurnOn (vtable +0x24). The vent's target airflow (iVar1 + 0x60) belongs to a machine that
    is not simulated; it is kept here so that the stores can be compared."""
    if param_1 == 0:
        if this[0x2c] != 0:
            pdVar2 = this[8]
            pdVar2[0x80] = pdVar2[0x80] - this[0x40]
        this[0x2c] = 0
        return
    if this[0x2c] == 0:
        pdVar2 = this[8]
        pdVar2[0x80] = this[0x40] + pdVar2[0x80]
    this[0x2c] = 1


def lung_update(S, this, param_1):
    """dll:10024500 CLungAirFlow::Update: needs the lung machine (feature), which this oracle does not simulate"""
    if this[0x2c] != 0:
        raise SystemExit('lung air flow switched on: the lung machine is not simulated by this oracle')


def bubble_update(S, this, param_1):
    """dll:1002ab70 CBubbleFlow::Update; DAT_100c1cc8 = the ocean"""
    param_1 = param_1 & U32
    OC = S.ocean
    if this[0x2c] != 0:
        iVar1 = this[4]
        local_8 = float(param_1)
        this[0x10] = local_8 * this[0x38] * 0.0001388888888888889
        iVar1[0xa0] = iVar1[0xa0] - (this[0x10] * iVar1[0x38]) / iVar1[0x28]
        OC[0x50] = ((this[4][0x38] * this[0x10]) / OC[0x18]) * 0.25 + OC[0x50]
        iVar1 = this[8]
        iVar1[0xa0] = (OC[0x28] * this[0x10]) / iVar1[0x28] + iVar1[0xa0]
        iVar1 = this[4]
        iVar1[0xb0] = iVar1[0xb0] - (iVar1[0x48] * this[0x10]) / iVar1[0x28]
        iVar1 = this[8]
        iVar1[0xb0] = (this[0x10] * 100.0) / iVar1[0x28] + iVar1[0xb0]
        iVar1 = this[8]
        iVar1[0xc0] = (this[4][0x58] * this[0x10]) / iVar1[0x28] + iVar1[0xc0]
        iVar1 = this[4]
        iVar1[0xc0] = iVar1[0xc0] - (iVar1[0x58] * this[0x10]) / iVar1[0x28]
        local_10 = {0: this[4][0x58] * -1.4999999999999947 + 8.6}
        Clamp(local_10, 0, 8.3, 8.6)
        OC[0x68] = (local_10[0] - OC[0x40]) * local_8 * 5.555555555555555e-07 + OC[0x68]
        iVar1 = this[8]
        iVar1[0xb8] = (this[4][0x50] * this[0x10]) / iVar1[0x28] + iVar1[0xb8]
        iVar1 = this[4]
        iVar1[0xb8] = iVar1[0xb8] - (iVar1[0x50] * this[0x10]) / iVar1[0x28]
        OC[0x60] = ((this[4][0x50] * 0.09523809523809523 + 3.0) - OC[0x38]) * local_8 * 0.0002777777777777778 + OC[0x60]
        iVar1 = this[8]
        iVar1[200] = (this[4][0x98] * this[0x10]) / iVar1[0x28] + iVar1[200]
        iVar1 = this[4]
        iVar1[200] = iVar1[200] - (iVar1[0x98] * this[0x10]) / iVar1[0x28]


# ------------------------------------------------------------------------------------------------ water
def tank_commit(p):
    """dll:1000f020 CWaterTank::Commit (vtable +0x20)"""
    Clamp(p, 0x60, 0.0, 100.0)
    p[0x38] = p[0x60]
    Clamp(p, 0x68, 3.0, 13.0)
    p[0x40] = p[0x68]
    Clamp(p, 0x58, 0.0, 100.0)
    p[0x30] = p[0x58]
    Clamp(p, 0x50, -20.0, 50.0)
    p[0x28] = p[0x50]
    Clamp(p, 0x70, 0.0, 100.0)
    p[0x48] = p[0x70]


def waterflow_update(S, this, param_1):
    """dll:1005d080 CWaterFlow::Update (vtable +0x24)"""
    param_1 = param_1 & U32
    if this[0x10] != 0:
        if this[0x14] == 0:
            iVar4 = this[8]
            iVar5 = this[4]
        else:
            iVar4 = this[4]
            iVar5 = this[8]
        local_8 = param_1
        local_18 = float(local_8) * this[0x20] * this[0x30] * 0.0002777777777777778
        if this[0x18] != 0:
            local_18 = (iVar5[0x18] / iVar5[0x10]) * local_18
        if local_18 < iVar5[0x18]:
            if iVar4[0x20] != 0:
                if iVar4[0x10] <= iVar4[0x18]:
                    return
                dVar3 = iVar4[0x10] - iVar4[0x18]
                if dVar3 < local_18:
                    local_18 = dVar3
            if local_18 != 0.0:
                dVar3 = iVar4[0x18] + local_18
                iVar4[0x60] = (iVar5[0x38] * local_18 + iVar4[0x38] * iVar4[0x18]) / dVar3
                iVar4[0x68] = (iVar5[0x40] * local_18 + iVar4[0x40] * iVar4[0x18]) / dVar3
                iVar4[0x58] = (iVar5[0x30] * local_18 + iVar4[0x30] * iVar4[0x18]) / dVar3
                iVar4[0x50] = (iVar5[0x28] * local_18 + iVar4[0x28] * iVar4[0x18]) / dVar3
                if this[0x38] < 0.0:
                    dVar2 = iVar5[0x48]
                else:
                    dVar2 = this[0x38]
                iVar4[0x70] = (dVar2 * local_18 + iVar4[0x48] * iVar4[0x18]) / dVar3
                iVar5[0x18] = iVar5[0x18] - local_18
                iVar4[0x18] = dVar3
                if this[0x28] != 0.0:
                    this[0x30] = this[0x30] - this[0x28] * float(local_8)
                    Clamp(this, 0x30, 0.0, 1.0)


def evaporation_update(S, this, param_1):
    """dll:1006fc50 CEvaporation::Update"""
    param_1 = param_1 & U32
    if this[0x10] != 0:
        local_8 = param_1
        iVar1 = this[4]
        dVar3 = float(local_8) * this[0x40][0x48] * 0.01 * this[0x20] * 0.0002777777777777778
        if dVar3 <= iVar1[0x18]:
            iVar2 = this[8]
            dVar4 = iVar2[0x18] + dVar3
            dVar5 = iVar1[0x18] - dVar3
            iVar2[0x60] = (iVar1[0x38] * dVar3 + iVar2[0x38] * iVar2[0x18]) / dVar4
            iVar1 = this[8]
            iVar1[0x68] = (this[4][0x40] * dVar3 + iVar1[0x18] * iVar1[0x40]) / dVar4
            iVar1 = this[8]
            iVar1[0x50] = (this[4][0x28] * dVar3 + iVar1[0x28] * iVar1[0x18]) / dVar4
            iVar1 = this[4]
            iVar1[0x58] = (iVar1[0x18] * iVar1[0x30]) / dVar5
            iVar1 = this[8]
            iVar1[0x58] = (iVar1[0x18] * iVar1[0x30]) / dVar4
            iVar1 = this[8]
            iVar1[0x70] = (this[0x38] * dVar3 + iVar1[0x48] * iVar1[0x18]) / dVar4
            this[4][0x18] = dVar5
            this[8][0x18] = dVar4


def rain_update(S, this, param_1):
    """dll:10071fc0 CRain::Update; DAT_100c272c = the story milestones (blob header)"""
    param_1 = param_1 & U32
    if this[0x10] != 0:
        local_8 = param_1
        dVar2 = float(local_8)
        dVar3 = dVar2 * this[0x20] * 0.0002777777777777778
        if (((S.milestones & 2) != 0) or (this[8][0x18] <= this[8][0x10] * 1.01)) and (dVar3 <= this[4][0x18]):
            dVar4 = this[8][0x18] + dVar3
            dVar6 = dVar3 * 4.0
            iVar1 = this[0x40]
            dVar5 = (iVar1[0x38] * iVar1[0x28] + this[4][0x28] * dVar6) / (iVar1[0x28] + dVar6)
            iVar1[0x38] = dVar5
            this[0x40][0xa0] = dVar5
            iVar1 = this[0x40]
            dVar7 = iVar1[0x28] * 3.0
            dVar5 = (dVar6 * dVar5 + iVar1[0x40] * dVar7) / (dVar6 + dVar7)
            iVar1[0x40] = dVar5
            this[0x40][0xa8] = dVar5
            iVar1 = this[8]
            iVar1[0x60] = (dVar3 * 4.5 + iVar1[0x18] * iVar1[0x38]) / dVar4
            iVar1 = this[8]
            iVar1[0x68] = (this[4][0x40] * dVar3 + iVar1[0x18] * iVar1[0x40]) / dVar4
            iVar1 = this[8]
            iVar1[0x58] = (this[4][0x30] * dVar3 + iVar1[0x18] * iVar1[0x30]) / dVar4
            iVar1 = this[8]
            iVar1[0x50] = (dVar5 * dVar3 + iVar1[0x28] * iVar1[0x18]) / dVar4
            pdVar8 = this[4]
            pdVar8[0x18] = pdVar8[0x18] - dVar3
            this[8][0x18] = dVar4
            c = this[0x40]
            c[0x98] = dVar2 * -0.0002777777777777778 + c[0x98]
            if c[0x98] < 0.0:
                c[0x98] = 0.0
            if 0.0 < this[0x48]:
                c = this[0x40]
                c[0xb0] = this[0x48] * dVar2 + c[0xb0]
                Clamp(this[0x40], 0xb0, 0.0, 100.0)
                iVar1 = this[0x40]
                iVar1[0x48] = iVar1[0xb0]


# ------------------------------------------------------------------------------------------------ ocean
def ocean_update_state(S, this, param_1):
    """dll:10029990 COcean::UpdateState(this, now) (without the page display)"""
    iVar2 = this[0x78]
    this[0x78] = param_1
    iVar3 = this[0x7c]
    local_8 = float((param_1 - iVar2) & U32)
    dVar4 = local_8
    dVar5 = (iVar3[0x38] - this[0x28]) * dVar4 * 0.0002777777777777778
    this[0x50] = dVar5 * 0.25 + this[0x50]
    iVar3[0xa0] = iVar3[0xa0] - dVar5
    loc = {0: this[0x7c][0x58] * -1.4999999999999947 + 8.6}
    Clamp(loc, 0, 8.3, 8.6)
    dVar5 = (loc[0] - this[0x40]) * dVar4 * 5.555555555555555e-07 + this[0x68]
    if this[0x80] == 0:
        this[0x68] = dVar5
        dVar5 = dVar4 * -2.777777777777778e-05
    else:
        this[0x68] = dVar5
        if 3.4 <= this[0x38]:
            dVar5 = dVar4 * -2.777777777777778e-05
        else:
            dVar5 = (3.4 - this[0x38]) * dVar4 * 0.0002777777777777778
    this[0x60] = dVar5 + this[0x60]
    this[0x98] = this[0x7c][0x60] * this[0x28] * dVar4 * 1.1574074074074073e-05 + this[0x98]
    Clamp(this, 0x98, 0.0, 100.0)
    this[0x90] = 0                                  # the word at +0x90 (flags); +0x91 is its high byte
    if 23.0 <= this[0x28]:
        if 26.0 < this[0x28]:
            this[0x90] = 1
            dVar5 = (this[0x28] - 26.0) * dVar4 * -1.1574074074074073e-05 + this[0x88]
            this[0x88] = dVar5
    else:
        this[0x90] = 2
        dVar5 = (23.0 - this[0x28]) * dVar4 * -1.1574074074074073e-05 + this[0x88]
        this[0x88] = dVar5
    if this[0x38] < 3.5:
        this[0x90] = this[0x90] | 0x20
        this[0x88] = (3.5 - this[0x38]) * dVar4 * -1.1574074074074074e-06 + this[0x88]
    if 3.5 <= this[0x30]:
        if this[0x30] <= 4.2:
            pass
        else:
            this[0x90] = this[0x90] | 0x80
            this[0x88] = (this[0x30] - 4.2) * dVar4 * -1.1574074074074073e-05 + this[0x88]
    else:
        this[0x90] = this[0x90] | 0x40
        this[0x88] = (3.5 - this[0x30]) * dVar4 * -1.1574074074074073e-05 + this[0x88]
    if 70.0 < this[0x98]:
        this[0x90] = this[0x90] | 0x100             # byte +0x91 |= 1
        this[0x88] = (this[0x98] - 70.0) * dVar4 * -3.8580246913580245e-07 + this[0x88]
    if this[0x90] == 0:
        this[0x88] = dVar4 * 3.858024691358025e-06 + this[0x88]
    Clamp(this, 0x88, 0.0, 1.0)
    if this[0x18] <= 11.25:
        if 10.75 <= this[0x18]:
            this[0x84] = 2
        else:
            this[0x84] = 3
    else:
        this[0x84] = 1
    # (page display of the level: not simulated)
    if this[0x28] < 0.0 or 1000.0 < this[0x28]:
        S.log.append('Ocean m_dTemp %f' % this[0x28])
    if 30.0 < this[0x28]:
        S.log.append('Ocean::UpdateState  m_dTemp > 30 ')
        S.log.append('Ocean m_dTemp %f' % this[0x28])


# ------------------------------------------------------------------------------------------------ plants
def LookUpVal(S, table, x, n):
    """dll:10007a60"""
    xs = table
    if x <= xs[n][0]:
        iVar4 = 0
        if xs[0][0] < x:
            p = 0
            while True:
                p += 1
                iVar4 += 1
                if not (xs[p][0] < x):
                    break
        return xs[iVar4][1]
    S.log.append('BiosGame.dll: dStat(%f) > lookUp[ulLastEntry].dTopLimit(%f) in LookUpVal' % (x, xs[n][0]))
    S.lookup_overflow = True
    return xs[n][1]


def plant_update(S, this, param_1):
    """dll:100733c0 CPlant::Update"""
    uVar7 = (param_1 - this[0x5c]) & U32
    this[0x5c] = param_1
    if uVar7 == 0:
        return
    if (this[0xc] & 8) != 0:
        return
    this[0x80] = this[0x78]
    if (this[0x88] & 4) != 0 and this[0x10] != 0:
        S.remove_plant_fruit(this)                  # FUN_10014740
    iVar3 = this[0x4c]
    local_18 = 0.0
    local_10 = 0.0
    this[0x88] = 0
    this[0xa0] = 1.0
    if iVar3[0x70] <= 20.0:
        this[0xa8] = iVar3[0x70] * 0.05
    else:
        this[0xa8] = 1.0
    stress = False
    if this[0x90] <= iVar3[0x70]:
        if this[0x98] < iVar3[0x70]:
            dVar1 = iVar3[0x70]
            this[0x88] = 0x10080
            this[0xa8] = (1.0 - (dVar1 - 47.5) / (this[0x90] - 47.5)) * this[0xa8]
            stress = True
    else:
        dVar1 = iVar3[0x70]
        this[0x88] = 0x10040
        this[0xa8] = ((dVar1 - 1.0) / (this[0x90] - 1.0)) * this[0xa8]
        stress = True
    if stress:
        local_18 = float(uVar7) * 4.050925925925926e-06
        local_10 = float(uVar7) * 1.6534391534391535e-06
    dVar1 = (iVar3[0x60] + 1.0) * (iVar3[0x48] * -0.01 + 1.0) * iVar3[0x38] * 0.03333333333333333
    this[0xb0] = (iVar3[0x80] * 0.3 + 1.0) * dVar1
    if this[0xa8] < this[0xb0]:
        this[0xb0] = this[0xa8]
        this[0xa0] = this[0xb0] / ((iVar3[0x80] + 1.0) * dVar1)
    dVar1 = float(uVar7)                            # (double)(longlong)(ulonglong)uVar7
    iVar3[0xb0] = dVar1 * this[0xb0] * 2.3148148148148148e-09 + iVar3[0xb0]
    iVar3 = this[0x4c]
    this[0xb8] = (iVar3[0x38] - this[0xb0]) + iVar3[0x60]
    if S.diurnal == 0x14:                           # DAT_100c1c44
        local_20 = this[0xf8]
    else:
        local_20 = this[0xf0]
    hit = False
    if local_20 <= this[0xb8]:
        if this[0x100] < this[0xb8]:
            dVar2 = this[0xb8] - this[0x100]
            this[0x88] = this[0x88] | 0x40200
            hit = True
    else:
        dVar2 = this[0xf0] - this[0xb8]
        this[0x88] = this[0x88] | 0x400
        hit = True
    if hit:
        local_18 = dVar2 * dVar1 * 8.101851851851852e-06 + local_18
        local_10 = dVar2 * dVar1 * 5.787037037037037e-06 + local_10
    fVar9 = LookUpVal(S, this[0xd8], iVar3[0x60], 0x13)
    this[200] = fVar9
    this[0x88] = this[0x88] | 8
    fVar9 = LookUpVal(S, this[0xdc], this[0x4c][0x58], 0x17)
    if fVar9 < this[200]:
        uVar7b = this[0x88]
        this[0x88] = uVar7b | 0x20
        this[200] = fVar9
        this[0x88] = uVar7b & 0xfffffff7 | 0x20
    dVar2 = abs(this[0xb8] - this[0xe8])
    fVar9 = LookUpVal(S, this[0xe0], dVar2, 0x13)
    if fVar9 < this[200]:
        uVar7b = this[0x88]
        this[0x88] = uVar7b | 0x10
        this[200] = fVar9
        this[0x88] = uVar7b & 0xfffffff7 | 0x10
        this[0x88] = uVar7b & 0xffffffd7 | 0x10
    iVar3 = this[0x4c]
    dVar2 = (iVar3[0x80] * 0.3 + 1.0) * this[0xc0] * this[200]
    if (this[0xc] & 4) == 0:
        this[200] = dVar2
        dVar2 = dVar2 * this[0xa0]
    this[200] = dVar2
    if 10.0 < iVar3[0x98]:
        dVar2 = iVar3[0x98]
        this[0x88] = this[0x88] | 0x1000
        dVar2 = dVar2 * 0.01 * dVar1 * 3.858024691358025e-06
        local_18 = local_18 + dVar2
        local_10 = dVar2 + local_10
    local_18 = local_18 / this[0x108]
    if local_18 <= 0.0:
        this[0x68] = dVar1 * 0.0002777777777777778 + this[0x68]
        dVar2 = dVar1 * 3.472222222222222e-06 + this[0x70]
    else:
        this[0x68] = this[0x68] - local_18
        dVar2 = this[0x70] - local_10 / this[0x108]
    this[0x70] = dVar2
    Clamp(this, 0x68, 0.3, 1.0)
    Clamp(this, 0x70, 0.0, 1.0)
    if this[0x70] <= this[0x68]:
        this[0x78] = this[0x70]
    else:
        this[0x78] = this[0x68]
    this[200] = this[200] * this[0x78]
    if 0.2 < this[0x70]:
        if 0.5 <= this[0x70]:
            if 0.5 <= this[0x68]:
                this[0x88] = this[0x88] | 1
            else:
                this[0x88] = this[0x88] | 0x800
        else:
            this[0x88] = this[0x88] | 2
            if this[0x10] != 0:
                S.remove_plant_fruit(this)
    else:
        this[0x88] = this[0x88] | 4
        if this[0x10] != 0:
            S.remove_plant_fruit(this)
    local_20 = (this[200] - this[0xd0]) * dVar1
    skip = False
    if local_20 < 0.0:
        iVar3 = this[0x4c]
        if 20.0 <= iVar3[0x58]:
            skip = True
        else:
            if 10.0 < iVar3[0x58]:
                local_20 = (20.0 - iVar3[0x58]) * local_20 * 0.1
            iVar3[0xb8] = iVar3[0xb8] + local_20
    else:
        iVar3 = this[0x4c]
        iVar3[0xb8] = iVar3[0xb8] + local_20
    if not skip:
        iVar3[0xc0] = iVar3[0xc0] - local_20
    iVar3 = this[0x4c]
    Clamp(iVar3, 0xb8, 0.0, 100.0)
    iVar3[0x50] = iVar3[0xb8]
    Clamp(iVar3, 0xc0, 0.0, 100.0)
    iVar3[0x58] = iVar3[0xc0]
    if (this[0xc] & 0x40) != 0 and (this[0x4c][0x108] & 2) != 0 and this[0x64] != 0 and 600 < ((S.time - this[0x60]) & U32):
        this[0x64] = this[0x64] - 1
        this[0x60] = S.time
        return
    if (this[0xc] & 0x10) != 0 and this[0x64] != 0 and 0x3840 < ((S.time - this[0x60]) & U32):
        this[0x64] = this[0x64] - 1
        this[0x60] = S.time


# ------------------------------------------------------------------------------------------------ the world
class World:
    """The CWorld object (theWorld), the timeline and everything the update functions reach through globals."""
    def __init__(self):
        self.W = {}
        self.log = []
        self.DAT_100a10d8 = 0
        self.DAT_100a10dc = 0
        self.lookup_overflow = False
        self.time = 0
        self.tod = 0
        self.day = 1
        self.daylight = 0.0
        self.diurnal = 0x28
        self.milestones = 0
        self.awake = 1
        self.rng = None
        self.cels = []
        self.tanks = []
        self.airflows = []
        self.waterflows = []
        self.plants = []
        self.ocean = None
        self.biomes = {}
        self.events = []
        self.foods = []          # fruit objects on plants (none are modelled: the objects are not simulated)
        self.plant_cursor = None

    # --- CTimeline ---
    def update_time_of_day(self):
        """dll:100155e0"""
        self.tod = (self.time & U32) % 0x15180

    def update_diurnal(self):
        """dll:10015710 CTimeline::UpdateDiurnal (the time-of-day state and daylight; the site calls are display)"""
        uVar1 = self.tod
        if uVar1 < 0x2a30 or 0x1274f < uVar1:
            self.daylight = 0.0
            self.diurnal = 0x28
        elif uVar1 < 0x5460:
            local_8 = float(uVar1 - 0x2a30)
            self.diurnal = 5
            self.daylight = local_8 * 9.259259259259259e-05 * 0.5
        elif uVar1 < 0x7080:
            local_8 = float(uVar1 - 0x5460)
            self.diurnal = 0xf
            self.daylight = local_8 * 0.0001388888888888889 * 0.5 + 0.5
        elif uVar1 < 0xe100:
            self.daylight = 1.0
            self.diurnal = 0x14
        elif uVar1 < 0xfd20:
            local_8 = float(uVar1 - 0xe100)
            self.diurnal = 0x1e
            self.daylight = local_8 * 0.0001388888888888889 * -0.5 + 1.0
        else:
            local_8 = float(uVar1 - 0xfd20)
            self.diurnal = 0x23
            self.daylight = local_8 * 9.259259259259259e-05 * -0.5 + 0.5

    def on_new_day(self):
        """dll:10014ee0 CWorld::OnNewDay (the sensors are not simulated here); dll:100189a0 CBiomeCel::OnNewDay"""
        for c in self.cels:
            c[0x108] = c[0x108] & 0xfffffff7

    def advance_real_time(self):
        """dll:100153b0 CTimeline::AdvanceRealTime"""
        uVar2 = (self.now - self.last_tick) & U32
        if uVar2 < 10000:
            self.time = self.time + uVar2 // self.ticks_per_game_sec
        self.last_tick = self.now
        iVar3 = (self.time & U32) // 0x15180 + 1
        if self.day != iVar3:
            self.day = iVar3
            self.on_new_day()
        self.update_time_of_day()

    def real_time_tick(self):
        """dll:10015470 CTimeline::RealTimeTick"""
        if 4999 < ((self.now - self.last_real_tick) & U32):
            self.last_real_tick = self.now
            self.advance_real_time()
            self.update_diurnal()

    # --- CWorld ---
    def switch_water_flows(self, id_, on):
        """dll:10014110 CWorld::SwitchWaterFlows (turn-on is vtable +0x20: sets +0x10)"""
        if id_ != 0:
            for f in self.waterflows:
                if f[0xc] == id_:
                    f[0x10] = on

    def remove_plant_fruit(self, plant):
        """FUN_10014740: calls vtable +0x94 (CFood remove, FUN_10063750 after FUN_10029510) of every food of the plant in state 2"""
        for fo in self.foods:
            if fo['plant'] is plant and fo['state'] == 2:
                # FUN_10029510 (Eng_RemoveGameObject succeeds): m_bInScene = 0, then FUN_10063750:
                # plant+0x14 &= ~(1 << (slot - 1)) (ushort), plant+0x10 -= 1, state = 1
                plant[0x14] = (plant[0x14] & ~(1 << ((fo['slot'] - 1) & 0x1f))) & 0xffff
                plant[0x10] = plant[0x10] - 1
                fo['state'] = 1

    def update_weather(self):
        """dll:10013420 CWorld::UpdateWeather"""
        W = self.W
        uVar4 = (self.time - W[0x1c8]) & U32
        if uVar4 != 0:
            local_10 = uVar4
            dVar1 = float(local_10)
            W[0x1c8] = self.time
            dVar2 = dVar1 * 2.777777777777778e-05
            if dVar2 < abs(W[0x278] - W[0x270]):
                if W[0x270] < W[0x278]:
                    dVar3 = W[0x270] + dVar2
                else:
                    dVar3 = W[0x270] - dVar2
                W[0x270] = dVar3
            else:
                W[0x278] = self.rng.range(W[0x280], W[0x288])
            # FUN_1000e3a0(DAT_100c1f84): the site palette
            W[0x290] = (W[0x270] * 0.7 + 0.3) * self.daylight
            if dVar2 < abs(W[0x218] - W[0x210]):
                if W[0x210] < W[0x218]:
                    dVar3 = W[0x210] + dVar2
                else:
                    dVar3 = W[0x210] - dVar2
                W[0x210] = dVar3
            else:
                W[0x218] = self.rng.range(W[0x220], W[0x228])
            if dVar2 < abs(W[0x238] - W[0x230]):
                if W[0x230] < W[0x238]:
                    dVar2 = W[0x230] + dVar2
                else:
                    dVar2 = W[0x230] - dVar2
                W[0x230] = dVar2
            else:
                W[0x238] = self.rng.range(W[0x240], W[0x248])
            if self.tod < 0x5460:
                local_10 = 0x5460 - self.tod
                dVar2 = float(local_10) * 1.54320987654321e-05
            elif self.tod < 0xa8c0:
                local_10 = self.tod - 0x5460
                dVar2 = float(local_10) * 4.6296296296296294e-05
            else:
                local_10 = self.tod - 0xa8c0
                dVar2 = float(local_10) * -1.54320987654321e-05 + 1.0
            W[0x208] = (W[0x210] - W[0x230]) * dVar2 + W[0x230]
            dVar2 = dVar1 * 0.0002777777777777778
            if dVar2 < abs(W[600] - W[0x250]):
                if W[0x250] < W[600]:
                    dVar2 = W[0x250] + dVar2
                else:
                    dVar2 = W[0x250] - dVar2
                W[0x250] = dVar2
            else:
                W[600] = self.rng.range(W[0x260], W[0x268])
            for a in (0x100c1c50, 0x100c0ee8, 0x100c1280, 0x100c2768, 0x100c24e8, 0x100c2560, 0x100c1208, 0x100c1ba0, 0x100c25d8):
                biome_update_target_temp(self, self.biomes[a])
            self.update_biome_cels()
            W[0x1d0] = W[0x1d0] - W[0x1e0] * dVar1
            W[0x1d0] = W[0x1e8] * dVar1 + W[0x1d0]
            if (W[0x1d8] >> 24 & 0x40) == 0:
                if 3.0 <= W[0x1d0]:
                    self.power_restored()
                    return
                if W[0x1d0] < 0.0:
                    W[0x1d0] = 0.0
            else:
                if W[0x1d0] <= 0.0:
                    self.power_failure()
                    return
                if 200.0 < W[0x1d0]:
                    W[0x1d0] = 200.0
        return

    def power_failure(self):
        """dll:10014a90 (the features' OnPowerOff are not simulated)"""
        self.W[0x1d8] = self.W[0x1d8] & 0xbfffffff
        self.W[0x1d0] = 0.0

    def power_restored(self):
        """dll:10014b20 (the features' OnPowerOn are not simulated)"""
        self.W[0x1d8] = self.W[0x1d8] | 0x40000000

    def update_biome_cels(self):
        """dll:10013cc0 CWorld::UpdateBiomeCels"""
        W = self.W
        uVar4 = (self.time - W[0x164]) & U32
        uVar5 = 0
        W[0x164] = self.time
        local_8 = 0.0
        dVar2 = local_8
        for this in self.cels:
            cel_update_state(self, this, uVar4)
            cel_update_plant_health_avg(this)
            if (this[0x108] >> 24 & 0x80) != 0:
                dVar2 = this[0xf0] + dVar2
                uVar5 = uVar5 + 1
        if self.awake != 0:                         # (DAT_100c1fe4 != 0) && (DAT_100c1f8c == 0)
            local_8 = float(uVar5)
            dVar2 = dVar2 / local_8 if local_8 != 0.0 else float('nan')
            if 0.01 < dVar2:
                self.events.append(0x10000000)
            if 0.3 < dVar2:
                self.events.append(0x20000000)
            if 0.4 < dVar2:
                self.events.append(0x40000000)
        return 1

    def update_air_flows(self):
        """dll:10013e00 CWorld::UpdateAirFlows"""
        iVar3 = (self.time - self.W[0x184]) & U32
        self.W[0x184] = self.time
        for f in self.airflows:
            f['update'](self, f, iVar3)
        for this in self.cels:                      # CWorld::CommitBiomeCels
            cel_commit(this)
        return 1

    def update_water_flows(self):
        """dll:100140b0 CWorld::UpdateWaterFlows"""
        iVar3 = (self.time - self.W[0x1c0]) & U32
        self.W[0x1c0] = self.time
        for f in self.waterflows:
            f['update'](self, f, iVar3)
        for t in self.tanks:                        # CWorld::CommitWaterTanks
            tank_commit(t)
        return 1

    def update_next_plant(self, param_1):
        """dll:100144f0 CWorld::UpdateNextPlant"""
        if self.plant_cursor is None:
            if len(self.plants) == 0:
                return False
            self.plant_cursor = 0
        p = self.plants[self.plant_cursor]
        self.plant_cursor += 1
        if self.plant_cursor >= len(self.plants):
            self.plant_cursor = None
        plant_update(self, p, param_1)
        return self.plant_cursor is not None

    def update_all_plants(self):
        """dll:10014540 CWorld::UpdateAllPlants"""
        self.plant_cursor = None
        iVar1 = self.time
        while self.update_next_plant(iVar1):
            pass
        return 1

    # --- the two schedules of biosys-simtest ---
    def timer_call(self):
        """dll:1001d970 Event_OnTimer (simulation part)"""
        self.now += 1000
        phase = self.phase
        self.phase += 1
        if phase == 0:
            self.update_weather()
            ocean_update_state(self, self.ocean, self.time)
            self.real_time_tick()
        elif phase == 1:
            self.update_air_flows()
        elif phase == 2:
            self.update_water_flows()
        elif phase == 3:
            if (self.time & U32) - 0 > 0x5a:          # DAT_100a17b8 (the idle handler's stamp) stays 0 here
                self.update_all_plants()
        else:
            self.phase = 0

    def advance(self, param_1):
        """dll:100154b0 CTimeline::Advance"""
        uVar1 = (param_1 + 0x5a) // 0x5a
        uVar3 = uVar1
        while True:
            self.update_weather()
            ocean_update_state(self, self.ocean, self.time)
            self.update_air_flows()
            self.update_water_flows()
            self.update_all_plants()
            self.time = self.time + 0x5a
            self.update_time_of_day()
            self.update_diurnal()
            uVar3 -= 1
            if uVar3 == 0:
                break
        if param_1 < uVar1 * 0x5a:
            self.time = self.time + param_1 - uVar1 * 0x5a
            self.update_time_of_day()
            self.update_diurnal()


# ------------------------------------------------------------------------------------------------ loading
WORLD = {'m_dBlueSky': 0x270, 'm_dwStamp_1c8': 0x1c8, 'm_dDayMaxExtTemp': 0x210, 'm_dDayMaxExtTempTarget': 0x218,
         'm_dDayMaxExtTempLo': 0x220, 'm_dDayMaxExtTempHi': 0x228, 'm_dNightMinExtTemp': 0x230,
         'm_dNightMinExtTempTarget': 0x238, 'm_dNightMinExtTempLo': 0x240, 'm_dNightMinExtTempHi': 0x248,
         'm_dExtTemp': 0x208, 'm_dPower': 0x1d0, 'm_dwPowerFlags': 0x1d8, 'm_dPowerGeneration': 0x1e8,
         'm_dPowerDemand': 0x1e0, 'm_dSunlight': 0x290, 'm_dWindSpeed': 0x250, 'm_dWindSpeedTarget': 0x258,
         'm_dWindSpeedLo': 0x260, 'm_dWindSpeedHi': 0x268, 'm_dwStamp_1c4': 0x1c4, 'm_dwStamp_184': 0x184,
         'm_dwStamp_164': 0x164, 'm_dwStamp_184_dup': 0x184, 'm_dwStamp_94': 0x94, 'm_dwStamp_1c0': 0x1c0, 'unk_298': 0x298, 'unk_2a0': 0x2a0}
CEL = {'m_dAirCapacity': 0x20, 'm_dAirVolume': 0x28, 'm_dAirTemp': 0x38, 'm_dCO2': 0x58, 'm_dHumidity': 0x48,
       'm_dLight': 0x60, 'm_dLightLetThru': 0x88, 'm_dOxygen': 0x50, 'm_dPollution': 0x98, 'm_dSoilNutrients': 0x90,
       'm_dSoilpH': 0x68, 'm_dSoilTemp': 0x40, 'm_dSoilWater': 0x70, 'm_dwTimeLastRain': 0x78, 'm_dWindSpeed': 0x80,
       'm_dPlantHealthAvg': 0xf0, 'm_dPlantHealthMin': 0xf8, 'm_dPlantHealthMax': 0x100, 'm_dwFlags': 0x108}
TANK = {'m_dOxygen': 0x38, 'm_dPH': 0x40, 'm_dSalinity': 0x30, 'm_dTemp': 0x28, 'm_dPurity': 0x48, 'm_dWaterVol': 0x18,
        'm_bWaveMachineOn': 0x80, 'm_dAlgae': 0x98, 'm_dHealth': 0x88, 'm_wProblemFlags': 0x90, 'm_wLevelState': 0x84,
        'unk_78': 0x78}
AIRFLOW = {'m_bOn': 0x2c, 'm_bReversed': 0x30, 'm_dWindSpeed': 0x40}
WATERFLOW = {'m_bOn': 0x10, 'm_bReversed': 0x14, 'm_dFreeFlow': 0x30, 'm_dPurityOverride': 0x38}
PLANT = {'m_dLeafTemp': 0xb8, 'm_dHealth': 0x78, 'm_dHealthPrev': 0x80, 'm_dLongTermHealth': 0x70, 'm_dPSRate': 200,
         'm_dShortTermHealth': 0x68, 'm_dStomatalAperture': 0xa0, 'm_dTranspirationRate': 0xb0,
         'm_dWaterAvailability': 0xa8, 'm_dwTimeLastUpdate': 0x5c, 'm_dwTimeLastPicked': 0x60, 'm_dwNumPicked': 0x64,
         'm_dwHealthState': 0x88, 'unk_10': 0x10, 'unk_14': 0x14, 'unk_110': 0x110}
SECTIONS = {'cels': CEL, 'tanks': TANK, 'airflows': AIRFLOW, 'waterflows': WATERFLOW, 'plants': PLANT}


# food objects that grow on a plant: object name -> (plant name, 1-based fruit slot); CFood ctor dll:10063520 (builder data)
FOOD_PLANTS = {
    'db1xx_1': ('ADB1XX', 1), 'db1xx_2': ('ADB1XX', 2), 'db1xx_3': ('ADB1XX', 3), 'db1xx_4': ('ADB1XX', 4),
    'db1xx_5': ('ADB1XX', 5), 'db3xx_1': ('ADB3XX', 1), 'db3xx_2': ('ADB3XX', 3), 'o2cc_1': ('AO2CC', 1), 'o2cc_2': ('AO2CC', 2),
    'o2cc_3': ('AO2CC', 3), 'o2cc_4': ('AO2CC', 4), 'o2cc_5': ('AO2CC', 5), 'r1ma_1': ('AR1MA', 1), 'r1pa_1': ('AR1PA', 1),
    'r2ba_1': ('AR2BA', 1), 'r2ba_2': ('AR2BA', 2), 'r2ba_3': ('AR2BA', 3), 'r2or_1': ('AR2OR', 1), 'r2or_2': ('AR2OR', 2),
    'r2or_3': ('AR2OR', 3), 'r2or_4': ('AR2OR', 4), 'r2or_5': ('AR2OR', 5), 'r2or_6': ('AR2OR', 6), 'r2pa_1': ('AR2PA', 1),
    'r2pa_2': ('AR2PA', 2), 'r2pa_3': ('AR2PA', 3), 'r2pa_4': ('AR2PA', 4), 'r3av_1': ('AR3AV', 1), 'r3av_2': ('AR3AV', 2),
    'r3av_3': ('AR3AV', 3), 'r3av_4': ('AR3AV', 4), 'r3eg_1': ('AR3EG', 1), 'r3eg_2': ('AR3EG', 2), 'r3eg_3': ('AR3EG', 3),
    'r3eg_4': ('AR3EG', 4), 'r3eg_5': ('AR3EG', 5), 'r3eg_6': ('AR3EG', 6), 'r3ya_1': ('AR3YA', 1), 'r4bf_1': ('AR4BF', 1),
    'r4bf_2': ('AR4BF', 2), 'r4bf_3': ('AR4BF', 3), 'r4nv_1': ('AR4NV', 1), 'r4nv_2': ('AR4NV', 2), 'r4nv_3': ('AR4NV', 3),
    'r4ya_1': ('AR4YA', 1), 'r5av_1': ('AR5AV', 1), 'r5av_2': ('AR5AV', 2), 'r5av_3': ('AR5AV', 3), 'r5av_4': ('AR5AV', 4),
    'r5av_5': ('AR5AV', 5), 'r5av_6': ('AR5AV', 6), 'r5bz_1': ('AR5BZ', 1), 'r5bz_2': ('AR5BZ', 2), 'r5bz_3': ('AR5BZ', 3),
    'r5bz_4': ('AR5BZ', 4), 'r5bz_5': ('AR5BZ', 5), 'r5ci_1': ('AR5CI', 1), 'r5ci_2': ('AR5CI', 2), 'r5gu_1': ('AR5GU', 1),
    'r5gu_2': ('AR5GU', 2), 'r5gu_3': ('AR5GU', 3), 'r5gu_4': ('AR5GU', 4), 'r6an_1': ('AR6AN', 1), 'r6an_2': ('AR6AN', 2),
    'r6an_3': ('AR6AN', 3), 'r6an_4': ('AR6AN', 4), 'r6an_5': ('AR6AN', 5), 'r6ba_1': ('AR6BA', 1), 'r6ba_2': ('AR6BA', 2),
    'r6ba_3': ('AR6BA', 3), 'r7ag_1': ('AR7AG', 1), 'r7ag_2': ('AR7AG', 2), 'r7av_1': ('AR7AV', 1), 'r7av_2': ('AR7AV', 2),
    'r7av_3': ('AR7AV', 3), 'r7av_4': ('AR7AV', 4), 'r7av_5': ('AR7AV', 5), 'r7or_1': ('AR7OR', 1), 'r7or_2': ('AR7OR', 2),
    'r7or_3': ('AR7OR', 3), 'r7or_4': ('AR7OR', 4), 'r7or_5': ('AR7OR', 5), 'r8ba_1': ('AR8BA', 1), 'r8ba_2': ('AR8BA', 2),
    'r8ba_3': ('AR8BA', 3), 'r8ba_4': ('AR8BA', 4), 'r8ba_5': ('AR8BA', 5), 'r8co_1': ('AR8CO', 1), 'r8co_2': ('AR8CO', 2),
    'r8co_3': ('AR8CO', 3), 'r8co_4': ('AR8CO', 4), 'r8da_1': ('AR8DA', 1), 'r8da_2': ('AR8DA', 2), 'r8da_3': ('AR8DA', 3),
    'r8da_4': ('AR8DA', 4), 'r8da_5': ('AR8DA', 5), 'r9ba_1': ('AR9BA', 1), 'r9ba_2': ('AR9BA', 2), 'r9ba_3': ('AR9BA', 3),
    'r9bf_1': ('AR9BF', 1), 'r9bf_2': ('AR9BF', 2), 'r9bf_3': ('AR9BF', 3), 'r9bf_4': ('AR9BF', 4), 'r9bf_5': ('AR9BF', 5),
    'r9bf_6': ('AR9BF', 6), 'r9ma_1': ('AR9MA', 1), 'r9pa_1': ('AR9PA', 1), 'r9pa_2': ('AR9PA', 2), 's1ag_1': ('AS1AG', 1),
    's1ag_2': ('AS1AG', 2), 's1ag_3': ('AS1AG', 3), 's2ep_1': ('AS2EP', 1), 's2ep_2': ('AS2EP', 2), 's2ep_3': ('AS2EP', 3),
    's2fg1_1': ('AS2FG1', 1), 's2fg1_2': ('AS2FG1', 2), 's2fg2_1': ('AS2FG2', 1), 's2fg2_2': ('AS2FG2', 2),
    's3al_1': ('AS3AL', 1), 's3al_2': ('AS3AL', 2), 's3al_3': ('AS3AL', 3), 's3ok_1': ('AS3OK', 1), 's3ok_2': ('AS3OK', 2),
    's5ok_1': ('AS5OK', 1), 's5ok_2': ('AS5OK', 2), 's5ok_3': ('AS5OK', 3), 's5wm_1': ('AS5WM', 1), 's5wm_2': ('AS5WM', 2),
    's5wm_3': ('AS5WM', 3), 's5wm_4': ('AS5WM', 4), 's6eg_1': ('AS6EG', 1), 's6eg_2': ('AS6EG', 2), 's6eg_3': ('AS6EG', 3),
    's6eg_4': ('AS6EG', 4), 's6eg_5': ('AS6EG', 5), 's6fg_1': ('AS6FG', 1), 's6fg_2': ('AS6FG', 2), 's6fg_3': ('AS6FG', 3),
    's6pp_1': ('AS6PP', 1), 's6pp_2': ('AS6PP', 2), 's6pp_3': ('AS6PP', 3), 's6pp_4': ('AS6PP', 4), 's7eg_1': ('AS7EG', 1),
    's7eg_2': ('AS7EG', 2), 's7eg_3': ('AS7EG', 3), 's7eg_4': ('AS7EG', 4), 's7eg_5': ('AS7EG', 5), 's7eg_6': ('AS7EG', 6),
    's7eg_7': ('AS7EG', 7), 's7eg_8': ('AS7EG', 8), 's7sq_1': ('AS7SQ', 1), 's7sq_2': ('AS7SQ', 2), 's7sq_3': ('AS7SQ', 3),
    's7sq_4': ('AS7SQ', 4), 's8he1_1': ('AS8HE1', 1), 's8he1_2': ('AS8HE1', 2), 's8he1_3': ('AS8HE1', 3),
    's8he1_4': ('AS8HE1', 4), 's8he1_5': ('AS8HE1', 5), 's8he1_6': ('AS8HE1', 6), 's8he2_1': ('AS8HE2', 1),
    's8he2_2': ('AS8HE2', 2), 's8he2_3': ('AS8HE2', 3), 's8he2_4': ('AS8HE2', 4), 's8sg_1': ('AS8SG', 1), 's8sg_2': ('AS8SG', 2),
    's8sg_3': ('AS8SG', 3), 's8sg_4': ('AS8SG', 4), 's8sg_5': ('AS8SG', 5), 's8sg_6': ('AS8SG', 6), 's9av_1': ('AS9AV', 1),
    's9av_2': ('AS9AV', 2), 's9av_3': ('AS9AV', 3), 's9av_4': ('AS9AV', 4), 's9av_5': ('AS9AV', 5), 's9av_6': ('AS9AV', 6),
    's9pe_1': ('AS9PE', 1), 's9pe_2': ('AS9PE', 2),
}


def parse_value(text):
    text = text.strip()
    if text.startswith('0x'):
        return int(text, 16)
    try:
        return int(text)
    except ValueError:
        return float(text)


def load_dump(path):
    out = subprocess.run(['python3', '-I', SCRIPTS + '/biosys_savblob.py', 'dump', path], capture_output=True, text=True, check=True).stdout
    rows = []
    for line in out.splitlines():
        if ' = ' not in line:
            continue
        key, val = line.split(' = ', 1)
        if key.count('/') < 2:
            continue
        sec, item, member = key.split('/', 2)
        rows.append((sec, item, member, val))
    return rows


def load_static(path):
    rows = []
    for line in open(path):
        f = line.rstrip('\n').split('\t')
        d = {}
        for kv in f[3:]:
            k, v = kv.split('=', 1)
            d[k] = v
        rows.append((f[0], f[1], f[2], d))
    return rows


def build(sav, static_path, rng_state, flows, pollute, drain):
    S = World()
    rows = load_dump(sav)
    static = load_static(static_path)
    order = {k: [] for k in SECTIONS}
    items = {}
    names = {}
    for sec, item, member, val in rows:
        if sec == 'world' and item == 'state':
            if member in WORLD:
                S.W[WORLD[member]] = parse_value(val)
        elif sec == 'header' and member == 'milestones':
            S.milestones = parse_value(val)
        elif sec == 'timeline':
            if member == 'm_dwTime':
                S.time = parse_value(val)
            elif member == 'm_dwDay':
                S.day = parse_value(val)
            elif member == 'm_dwTimeOfDay':
                S.tod = parse_value(val)
        elif sec in SECTIONS and member in SECTIONS[sec]:
            key = (sec, item)
            if key not in items:
                items[key] = {}
                order[sec].append(key)
            items[key][SECTIONS[sec][member]] = parse_value(val)
            names[(sec, item, member)] = SECTIONS[sec][member]
    # static data
    S.items = items
    S.names = names
    S.order = order
    # biomes: CBiome (dll:10009f60) and World__InitBiomeParams (dll:1002b560)
    for a in (0x100c1c50, 0x100c0ee8, 0x100c1280, 0x100c2768, 0x100c24e8, 0x100c2560, 0x100c1208, 0x100c1ba0, 0x100c25d8):
        S.biomes[a] = {0x50: 0.0, 0x58: 0.0, 0x60: 0, 0x68: 0.0}
    for a in (0x100c1c50, 0x100c0ee8, 0x100c1280, 0x100c24e8, 0x100c2560):
        S.biomes[a][0x50] = 10.0
    for a in (0x100c1ba0, 0x100c2768, 0x100c25d8):
        S.biomes[a][0x60] |= 1
        S.biomes[a][0x68] = 15.0
    for a in (0x100c1c50, 0x100c0ee8, 0x100c1280, 0x100c24e8):
        S.biomes[a][0x60] |= 4
    by_name = {}
    for kind, key, name, d in static:
        if kind == 'tank':
            t = items[('tanks', key)]
            t[0x10] = parse_value(d['volume'])
            t[0x20] = int(d['bounded'])
            t['name'] = name
            for off, src in ((0x50, 0x28), (0x58, 0x30), (0x60, 0x38), (0x68, 0x40), (0x70, 0x48)):
                t[off] = t[src]                     # CWaterTank::Load: the new values follow the loaded ones
            by_name[('tank', name)] = t
            S.tanks.append(t)
            if d['ocean'] != '-':
                S.ocean = t
                t['celname'] = d['ocean']
    for kind, key, name, d in static:
        if kind == 'cel':
            c = items[('cels', key)]
            c['plants'] = []
            c[0x10] = S.biomes[int(d['biome'], 16)]
            c[0x14] = by_name[('tank', d['soil'])] if d['soil'] != '-' else None
            c[0x18] = by_name[('tank', d['cond'])] if d['cond'] != '-' else None
            c[0x1c] = int(d['id'])
            for off, src in ((0xa0, 0x38), (0xa8, 0x40), (0xb0, 0x48), (0xb8, 0x50), (0xc0, 0x58), (0xc8, 0x98)):
                c[off] = c[src]                     # CBiomeCel::Load
            by_name[('cel', name)] = c
            S.cels.append(c)
    S.ocean[0x7c] = by_name[('cel', S.ocean['celname'])]
    alist = []
    for kind, key, name, d in static:
        if kind == 'airflow':
            f = items[('airflows', key)]
            f[4] = by_name[('cel', d['from'])]
            f[8] = by_name[('cel', d['to'])]
            f[0x28] = int(d['id'])
            f[0x38] = parse_value(d['mag'])
            f[0x34] = int(d['twoway'])
            f[0x10] = 0.0
            f['masteridx'] = None if d['master'] == '-' else int(d['master'])
            kindname = d['kind']
            if kindname == 'lung':
                if f[0x2c] != 0:
                    raise SystemExit('lung air flow switched on: the lung machine is not simulated by this oracle')
                f['update'] = lung_update
            elif kindname == 'soilbed':
                f['update'] = soilbed_update
            elif kindname == 'bubble':
                f['update'] = bubble_update
            else:
                f['update'] = airflow_update
            S.airflows.append(f)
            alist.append(f)
    for f in alist:
        f[0xc] = alist[f['masteridx']] if f['masteridx'] is not None else None
    for kind, key, name, d in static:
        if kind == 'waterflow':
            f = items[('waterflows', key)]
            f[4] = by_name[('tank', d['from'])]
            f[8] = by_name[('tank', d['to'])] if d['to'] != '-' else None
            f[0xc] = int(d['id'])
            f[0x20] = parse_value(d['mag'])
            f[0x28] = parse_value(d['clog'])
            f[0x18] = int(d['proportional'])
            if d['kind'] == 'evap':
                f[0x40] = by_name[('cel', d['cel'])]
                f['update'] = evaporation_update
                f[0x30] = f.get(0x30, 0.0)
            elif d['kind'] == 'rain':
                f[0x40] = by_name[('cel', d['cel'])]
                f[0x48] = parse_value(d['humidityRate'])
                f['update'] = rain_update
            else:
                f['update'] = waterflow_update
            S.waterflows.append(f)
    tables = {}
    for kind, key, name, d in static:
        if kind == 'plant':
            p = items[('plants', key)]
            p[0xc] = int(d['flags'])
            p[0x4c] = by_name[('cel', d['cel'])]
            p[0xc0] = parse_value(d['maxps'])
            p[0xd0] = parse_value(d['resp'])
            p[0xe8] = parse_value(d['topt'])
            p[0xf0] = parse_value(d['tnight'])
            p[0xf8] = parse_value(d['tday'])
            p[0x100] = parse_value(d['tmax'])
            p[0x108] = parse_value(d['hard'])
            p[0x90] = parse_value(d['swmin'])
            p[0x98] = parse_value(d['swmax'])
            for off, k, last in ((0xd8, 'light', 0x13), (0xdc, 'co2', 0x17), (0xe0, 'temp', 0x13)):
                addr = int(d[k], 16)
                if addr:
                    if addr not in tables:
                        tables[addr] = read_table(addr, last)
                    p[off] = tables[addr]
                else:
                    p[off] = None
            p[0x4c]['plants'].append(p)
            S.plants.append(p)
            p['name'] = key
    pbyname = {p['name']: p for p in S.plants}
    fstate = {}
    for sec, item, member, val in rows:
        if sec == 'objects' and member == 'm_byState':
            fstate[item] = parse_value(val)
    for oname, (pname, slot) in FOOD_PLANTS.items():
        if oname in fstate and pname in pbyname:
            S.foods.append({'plant': pbyname[pname], 'slot': slot, 'state': fstate[oname]})
    S.rng = Rand(rng_state)
    if flows == 'off':
        for f in S.airflows:
            airflow_turn_on(f, 0)
        for f in S.waterflows:
            f[0x10] = 0                             # CWaterFlow::TurnOn dll:1005d070 (also CRain 10071fb0)
    elif flows == 'on':
        for f in S.airflows:
            if f['update'] is not lung_update:
                airflow_turn_on(f, 1)
        for f in S.waterflows:
            if f[4] is not f[8]:                    # a pump flow from a tank to itself stays as it is
                f[0x10] = 1
    if pollute >= 0:
        for c in S.cels:
            c[0x98] = pollute                       # CBiomeCel::SetPollution (dll:100184b0): current and new
            c[0xc8] = pollute
    if drain >= 0:
        for c in S.cels:
            if c[0x14] is not None:
                c[0x14][0x18] = c[0x14][0x18] * drain
    S.ticks_per_game_sec = 40
    S.now = 4000
    S.last_tick = 0
    S.last_real_tick = 0
    S.phase = 0
    S.update_time_of_day()
    S.update_diurnal()
    # the world scalars the constructor sets and the blob does not carry (dll:10010f70): +0x278 comes from the C runtime's
    # first draws (seed 1), the range +0x280 / +0x288 is 0.9 .. 1.0
    boot = Rand(1)
    boot.range(0.9, 1.0)
    S.W[0x280] = 0.9
    S.W[0x288] = 1.0
    S.W[0x278] = boot.range(S.W[0x280], S.W[0x288])
    return S


# ------------------------------------------------------------------------------------------------ output
def fmt(v, member):
    if isinstance(v, float):
        return repr(v)
    if member in ('m_dwFlags', 'm_dwHealthState', 'm_dwPowerFlags', 'm_dwFlags'):
        return '0x%08x' % (v & U32)
    return str(v)


def dump(S, out):
    W = S.W
    for member, off in WORLD.items():
        out.write('world/state/%s = %s\n' % (member, fmt(W[off], member)))
    out.write('timeline/timeline/m_dwTime = %d\n' % S.time)
    out.write('timeline/timeline/m_dwDay = %d\n' % S.day)
    out.write('timeline/timeline/m_dwTimeOfDay = %d\n' % S.tod)
    # members in the order of the dump file
    for (sec, item, member), off in S.names.items():
        it = S.items[(sec, item)]
        v = it[off]
        out.write('%s/%s/%s = %s\n' % (sec, item, member, fmt(v, member)))
    out.write('sim/world/blueSkyTarget = %s\n' % repr(W[0x278]))
    out.write('sim/world/rngState = %d\n' % S.rng.s)
    out.write('sim/world/daylight = %s\n' % repr(S.daylight))
    out.write('sim/world/diurnal = %d\n' % S.diurnal)
    for a in (0x100c1c50, 0x100c0ee8, 0x100c1280, 0x100c2768, 0x100c24e8, 0x100c2560, 0x100c1208, 0x100c1ba0, 0x100c25d8):
        out.write('sim/biome/%08x/target = %s\n' % (a, repr(S.biomes[a][0x58])))
    out.write('sim/world/condensationFlowsOn = %d\n' % S.DAT_100a10d8)
    for n, text in enumerate(S.log):
        out.write('sim/log/%d = %s\n' % (n, text))


def main(argv):
    sav, static_path = argv[1], argv[2]
    rng = 1
    mode = 'timer'
    at = []
    flows = ''
    pollute = -1.0
    drain = -1.0
    i = 3
    while i < len(argv):
        a = argv[i]
        if a == '--rng':
            rng = int(argv[i + 1]); i += 2
        elif a == '--mode':
            mode = argv[i + 1]; i += 2
        elif a == '--at':
            at = [int(x) for x in argv[i + 1].split(',')]; i += 2
        elif a == '--flows':
            flows = argv[i + 1]; i += 2
        elif a == '--pollute':
            pollute = float(argv[i + 1]); i += 2
        elif a == '--drain':
            drain = float(argv[i + 1]); i += 2
        else:
            raise SystemExit('unknown option ' + a)
    S = build(sav, static_path, rng, flows, pollute, drain)
    start = S.time
    for target in at:
        if mode == 'timer':
            while ((S.time - start) & U32) < target:
                for _ in range(5):
                    S.timer_call()
        else:
            done = (S.time - start) & U32
            if target > done:
                S.advance(target - done)
        sys.stdout.write('== t=%d elapsed=%d\n' % (S.time, S.time - start))
        dump(S, sys.stdout)


if __name__ == '__main__':
    main(sys.argv)
