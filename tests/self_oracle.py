#!/usr/bin/env python3
"""Independent re-implementation of the player model (CSelf) and of the inventory objects of BiosGame.dll, written only
from the DECOMPILED text and the disassembly of the DLL (python3 -I analysis/scripts/dllpack.py show ADDR, pl_dis.sh),
never from the C++ port.  usage:

    python3 -I tests/self_oracle.py <file.sav> <script> [--seed N]

prints the dump format of tests/self_protocol.txt.  Python standard library only.

Functions are named after the original and carry its address; Self members have the C++ names of src/game/self.h
(the member table SELF_MEMBERS is that header's list), offsets in comments are the object offsets the DLL uses.

WHAT IS MODELLED (everything else is an engine/shell effect and a no-op):
  CSelf: Update 10066aa0, UpdateAilments 10067c80, CatchMalaria 1006df90, GetMaxHealth 1006f1c0, Die 10068f10 (cause, dead flag),
  stat setters/adders/losers 10066730..10066a60, Consume 1006cf40, Drink 1006ccd0, Eat 10068d70, Vomit/Retch 1006c9c0/1006ca20,
  AddWeight/Drop/CanPickUp, CanSleep 10067540, Sleep 10067600, OnIdle 10065e80 (sleep loop + the idle bookkeeping), WakeUp 10067910,
  Dream 1006dfc0 (dream choice, one rand), WhiteAngelTrip 1006dc20, PeyoteTrip 1006dd30, PlaySymptomSounds 100689a0 (its
  ChancePercent draws, Vomit, the hogweed pain rand), UpdateLoopSounds 10068960, the sound-cue flags +0x2c0..0x31c.
  CTimeline::Advance 100154b0 (world simulation left out), AdvanceRealTime 100153b0, SetTicksPerGameSec 10015ab0.
  Objects: CWorld::UpdateAllObjects 100146a0, Event_OnGameObject 1001e380 with the handlers of CGameObj (PickUp 10029000,
  dropped 100290a0, base slots), CFood (Eat 10063790, PickUp 10063ad0, Update 100639b0, remove 10063750), CGoggles, COxyMask,
  COxyCylinder. Other classes (consumables, extinguisher, paper, babysynth, alarm clock, torch, fishing rod ...) get the base
  handlers only; their own handlers/Update need engine queries and are NOT modelled.

EXTENSION: the command `ticks N` (CTimeline::SetTicksPerGameSec(N), dll:10015ab0) exists only in this oracle. The saved game has
the clock frozen (CTimeline +0x0c = -1 in NewGame.sav), in which CTimeline::Advance (100154b0) does nothing at all; WakeUp and Dream
set the value again (0x28 / -1). Scripts that need a running clock for `advance` / the sleep loop / object updates start with
`ticks 40`; they are listed in the oracle worker's report.

DLL GLOBALS MODELLED (printed as global.<name>; DAT_ names are the decompiler's, values of a fresh game):
  dead (DAT_100c2728, set by Die), DAT_100c42e4 / DAT_100c430c (last Sleep arguments a, b), DAT_100c42e0 (sleep length),
  DAT_100c42f8 (pill-sleep flag), DAT_100c4300 / DAT_100c42f0 (health / energy gain per 90 s of sleep, doubles), DAT_100c42e8
  (energy gain ceiling), DAT_100c42d0 (describe suppression flag), DAT_1009f838 and DAT_100c2498 (story flags set by object
  pick-up/drop), DAT_100c27e4 (oxygen cylinder state 5/1), DAT_100c1c44 (goggles cleared), DAT_100bd308 (tick of the last
  thirst message). Not printed because constant here: DAT_100c275c (modal flag, 0), DAT_100c2718 (cheat flags, 0), DAT_100c272c
  (milestone bits, from the header of the saved game), DAT_1009f834 (tick of the last input, 0 = never: Update never starts a
  sleep), DAT_100bc7b0 / DAT_100c4310 (message timers, 0), DAT_100c1f9c .. DAT_100c1fac are Self members (m_pFeature ... m_pAirMask).

INTERPRETATIONS (where the decompiler lost x87 state or an engine result is needed; see also the report of the oracle worker):
  * ChancePercent(x) = ftol(x * 0.01 * 32767.0) in x87 extended precision, then rand() <= that (10007570).  Every ftol of a
    value that was computed in x87 (heart-beat volumes, the sleep length) is emulated with 64-bit-mantissa arithmetic
    (class X); all other arithmetic is IEEE double.
  * The second sleep length of Sleep (10067600) is ftol(float(first) * X2) where X2 is the biome's +0x48 multiplier; the
    decompiler shows only the first multiplier.  Biome +0x40/+0x48 = 1.0 except the "house" biome (1.5) (InitBiomeParams).
  * Engine calls that return a success flag (palette lock, PlayWav of the drunk voice, OpenAVIMovie, ReturnGameObjectHidden,
    ChangeGameObjectVariation, RemoveGameObject, JumpToPage) are assumed to succeed, except Eng_OpenAVIMovie in CSelf::Dream
    which is assumed to FAIL (no movie headless): Dream returns 0 and OnIdle calls WakeUp.  Sound handles stay -1.
  * Eng_Api45_ReturnGameObjectHidden (CFood::Update) is assumed true for an object whose m_bInScene is 0.
  * Messages (ShowMessage / FUN_1006f330 ShowMessageDeferred) are not modelled: m_dwUnk298/29c/2a0/2a4 keep their constructor
    values; CGame::TriggerEvent (story) and CSelf::OnEnterPage are not modelled.
  * m_pPage prints the location text (the page of m_szLocation); m_pBiome prints the biome name (biomeName of world_ctors).
"""
import os as _os
# the developer's folder above the repository (original discs, analysis notes); see WORKERS.md
_DEV = _os.environ.get('BIOSYS_DEV_ROOT') or _os.path.abspath(_os.path.join(_os.path.dirname(_os.path.abspath(__file__)), '..', '..'))
import ast, math, re, struct, subprocess, sys
from fractions import Fraction

SCRIPTS = (_DEV + '/analysis/scripts')
U32 = 0xffffffff


def i32(x):
    x &= U32
    return x - (1 << 32) if x & 0x80000000 else x


def u32(x):
    return x & U32


# ------------------------------------------------------------------------------------------------ x87 extended precision
def xround(fr):
    """round a Fraction to the 64-bit mantissa of an x87 register (nearest even)"""
    if fr == 0:
        return Fraction(0)
    s = -1 if fr < 0 else 1
    a = abs(fr)
    e = a.numerator.bit_length() - a.denominator.bit_length() - 64
    while True:
        m = a / (Fraction(2) ** e)
        if m < 2 ** 63:
            e -= 1
        elif m >= 2 ** 64:
            e += 1
        else:
            break
    fl = m.numerator // m.denominator
    rem = m - fl
    if rem > Fraction(1, 2) or (rem == Fraction(1, 2) and fl % 2 == 1):
        fl += 1
    return s * fl * (Fraction(2) ** e)


def xadd(a, b):
    return xround(Fraction(a) + Fraction(b))


def xsub(a, b):
    return xround(Fraction(a) - Fraction(b))


def xmul(a, b):
    return xround(Fraction(a) * Fraction(b))


def ftol(fr):
    """__ftol 100791e8: truncation toward zero"""
    return math.trunc(Fraction(fr))


def hb_volume(t, a, b, c):
    """heart-beat volume of the stimulant timers: ftol((t - a) * b - c) in x87 (t an exact double)"""
    return ftol(xsub(xmul(xsub(float(t), a), b), c))


# ------------------------------------------------------------------------------------------------ C runtime
class Rng:
    def __init__(self, seed):
        self.s = seed & U32

    def rand(self):                      # _rand 10079380
        self.s = (self.s * 214013 + 2531011) & U32
        return (self.s >> 16) & 0x7fff

    def chance(self, x):                 # ChancePercent 10007570 (the argument is a double on the stack)
        thr = ftol(xmul(xmul(float(x), 0.01), 32767.0))
        return self.rand() <= thr

    def rand_range_ftol(self, p1, p2):   # ftol(RandRange(p1, p2)) 100075a0, all in x87
        r = self.rand()
        v = xmul(xsub(p2, p1), r)
        v = xmul(v, 3.051850947599719e-05)
        v = xadd(v, p1)
        return ftol(v)


def clamp(v, lo, hi):                    # Clamp 100074b0
    if v < lo:
        return lo
    if hi < v:
        return hi
    return v


# ------------------------------------------------------------------------------------------------ static data of the builder
# member table of src/game/self.h (name, C++ type, object offset)
SELF_MEMBERS = [
    ('m_dHydration', 'double', '0x000'), ('m_dHydrationDelta', 'double', '0x008'), ('m_dHydrationShown', 'double', '0x010'),
    ('m_dNutrition', 'double', '0x018'), ('m_dNutritionDelta', 'double', '0x020'), ('m_dNutritionShown', 'double', '0x028'),
    ('m_dProtein', 'double', '0x030'), ('m_dProteinDelta', 'double', '0x038'), ('m_dTemperature', 'double', '0x040'),
    ('m_dTemperatureDelta', 'double', '0x048'), ('m_dTemperatureShown', 'double', '0x050'), ('m_dIdealTemp', 'double', '0x058'),
    ('m_dMaxTemp', 'double', '0x060'), ('m_dHealth', 'double', '0x068'), ('m_dHealthDelta', 'double', '0x070'),
    ('m_dHealthShown', 'double', '0x078'), ('m_dEnergy', 'double', '0x080'), ('m_dEnergyDelta', 'double', '0x088'),
    ('m_dEnergyShown', 'double', '0x090'), ('m_dStimulant', 'double', '0x098'), ('m_dSedative', 'double', '0x0a0'),
    ('m_dSalt', 'double', '0x0a8'), ('m_dWeightCarried', 'double', '0x0b0'), ('m_wHydrationState', 'u16', '0x0b8'),
    ('m_wNutritionState', 'u16', '0x0ba'), ('m_wTempState', 'u16', '0x0bc'), ('m_wHealthState', 'u16', '0x0be'),
    ('m_wEnergyState', 'u16', '0x0c0'), ('m_wBreathing', 'u16', '0x0c2'), ('m_szLastJourneyName', 'std::array<u8, 11>', '0x0c4'),
    ('m_dJourneyCost', 'double', '0x0d0'), ('m_dwJourneySeconds', 'i32', '0x0d8'), ('m_dJourneyEnergy', 'double', '0x0e0'),
    ('m_dwJourneyTime', 'i32', '0x0e8'), ('m_szLocation', 'std::array<u8, 11>', '0x0ec'), ('m_ulCompassHeading', 'u32', '0x0f8'),
    ('m_bAbleToSleep', 'i32', '0x0fc'), ('m_dwLastFellAsleep', 'i32', '0x100'), ('m_dwLastWokeUp', 'i32', '0x104'),
    ('m_wWakeReason', 'u16', '0x110'), ('m_dwDreamsAvailable', 'u32', '0x114'), ('m_dwDreamsSeen', 'u32', '0x118'),
    ('m_dwNextDream', 'i32', '0x11c'), ('m_dwLastAte', 'i32', '0x120'), ('m_dwLastBarfed', 'i32', '0x124'),
    ('m_pLastEatenPlant', 'NamedRef', '0x128'), ('m_dwLastDrank', 'i32', '0x12c'), ('m_wLastDrinkType', 'u16', '0x130'),
    ('m_bWearingPoncho', 'i32', '0x134'), ('m_bHasSleepingBag', 'i32', '0x138'), ('m_dwSymptoms', 'u32', '0x13c'),
    ('m_dwSymptomsCured', 'u32', '0x140'), ('m_dwSpiderBiteTimer', 'i32', '0x144'), ('m_dwHogweedBurnTimer', 'i32', '0x148'),
    ('m_dwFumes', 'i32', '0x14c'), ('m_dwMalaria', 'i32', '0x150'), ('m_dMalariaHealthLoss', 'double', '0x158'),
    ('m_dwChloroquine', 'i32', '0x160'), ('m_dwCinchona', 'i32', '0x164'), ('m_bImmuneToMalaria', 'i32', '0x168'),
    ('m_dSynthHealthLoss', 'double', '0x170'), ('m_dwIndigestionTimer', 'i32', '0x178'),
    ('m_dwCactusPoisonTimer', 'i32', '0x17c'), ('m_dwEphedrineTimer', 'i32', '0x180'), ('m_dwWorms', 'i32', '0x184'),
    ('m_dWormsHealthLoss', 'double', '0x188'), ('m_dwDioscorin', 'i32', '0x190'), ('m_dDioscorinHealthLoss', 'double', '0x198'),
    ('m_dwSynthAttack', 'i32', '0x1a0'), ('m_dSynthAttackHealthLoss', 'double', '0x1a8'), ('m_dwPrussic', 'i32', '0x1b0'),
    ('m_dPrussicHealthLoss', 'double', '0x1b8'), ('m_dwChampagne', 'i32', '0x1c0'), ('m_byGlassesDrunk', 'u8', '0x1c4'),
    ('m_dwMescal', 'i32', '0x1c8'), ('m_bySwigsDrunk', 'u8', '0x1cc'), ('m_dwWhiteAngelTrip', 'i32', '0x1d0'),
    ('m_bInsideAngelSequence', 'i32', '0x1d4'), ('m_dwPeyoteTripTimer', 'i32', '0x1d8'),
    ('m_bInsidePeyoteSequence', 'i32', '0x1dc'), ('m_dwCoffee', 'i32', '0x1e0'), ('m_dEnergyGainCoffee', 'double', '0x1e8'),
    ('m_byDosesCoffee', 'u8', '0x1f0'), ('m_dwGuarana', 'i32', '0x1f4'), ('m_dEnergyGainGuarana', 'double', '0x1f8'),
    ('m_byDosesGuarana', 'u8', '0x200'), ('m_dwCoca', 'i32', '0x204'), ('m_dEnergyGainCoca', 'double', '0x208'),
    ('m_byDosesCoca', 'u8', '0x210'), ('m_dwAdrenaline', 'i32', '0x214'), ('m_dEnergyGainAdrenaline', 'double', '0x218'),
    ('m_dwSleepingPill', 'i32', '0x220'), ('m_wSleepingPillsPopped', 'u16', '0x224'), ('m_dwTimeLastUpdate', 'i32', '0x228'),
    ('m_dwUpdateDt', 'i32', '0x22c'), ('m_bDrunkVisuals', 'i32', '0x230'), ('m_bTripAurals', 'i32', '0x234'),
    ('m_bTripVisuals', 'i32', '0x238'), ('m_bMalariaVisuals', 'i32', '0x23c'), ('m_lHeartBeatVol', 'i32', '0x240'),
    ('m_bDrunkenCursor', 'i32', '0x244'), ('m_pPage', 'Item*', '0x248'), ('m_pSite', 'NamedRef', '0x24c'),
    ('m_pPreviousSite', 'NamedRef', '0x250'), ('m_pPath', 'Item*', '0x254'), ('m_pBiome', 'const Biome*', '0x258'),
    ('m_pCel', 'Item*', '0x25c'), ('m_pPlant', 'NamedRef', '0x260'), ('m_pFeature', 'NamedRef', '0x264'),
    ('m_pObjHolding', 'NamedRef', '0x268'), ('m_pObjTarget', 'Item*', '0x26c'), ('m_pGoggles', 'NamedRef', '0x270'),
    ('m_pAirMask', 'NamedRef', '0x274'), ('m_dwUnk278', 'i32', '0x278'), ('m_szHotspotName', 'std::array<u8, 13>', '0x287'),
    ('m_dwHotspotExpiry', 'i32', '0x294'), ('m_dwUnk298', 'i32', '0x298'), ('m_dwUnk29c', 'i32', '0x29c'),
    ('m_dwUnk2a0', 'i32', '0x2a0'), ('m_bUnk2a4', 'i32', '0x2a4'), ('m_dwUnk2a8', 'i32', '0x2a8'), ('m_bAwake', 'i32', '0x2ac'),
    ('m_szReturnPage', 'std::array<u8, 11>', '0x2b0'), ('m_dwUnk2bc', 'i32', '0x2bc'),
    ('m_aSoundCue', 'std::array<i32, 24>', '0x2c0'), ('m_dwUnk320', 'i32', '0x320'), ('m_dwUnk324', 'i32', '0x324'),
    ('m_lTopBarHandle', 'i32', '0x328'), ('m_hDrunkVoice', 'i32', '0x72c'), ('m_hUnk730', 'i32', '0x730'),
    ('m_hBreathingLoop', 'i32', '0x734'), ('m_bStatusScreenVisible', 'i32', '0x738'), ('m_hUnk73c', 'i32', '0x73c'),
    ('m_hUnk740', 'i32', '0x740'), ('m_hUnk744', 'i32', '0x744'), ('m_hUnk748', 'i32', '0x748'), ('m_hUnk74c', 'i32', '0x74c'),
    ('m_bFallAsleepOnIdle', 'i32', '0x758'), ('m_bRunSleepOnIdle', 'i32', '0x75c'), ('m_bDieOnUpdate', 'i32', '0x764'),
    ('m_wCauseOfDeath', 'u16', '0x768'), ('m_dLargestHealthLoss', 'double', '0x770'), ('m_hHeartBeatLoop', 'i32', '0x778'),
]

# site name -> (cel name or None, biome name or None)   [world_build builder: CSite ctor dll:1000c270 / CSiteVariant dll:10024f60]
SITES = {
    'BD': ('BD', 'basement'), 'BDLIFT': ('BD', 'basement'), 'BO': ('BASEMENTS', 'basement'), 'BOLIFT': ('BASEMENTS', 'basement'),
    'BP': ('BASEMENTS', 'basement'), 'BPLIFT': ('BASEMENTS', 'basement'), 'BPOD': ('BASEMENTS', 'basement'), 'BR': ('BASEMENTS', 'basement'),
    'BRLIFT': ('BASEMENTS', 'basement'), 'BROCK': ('BD', 'basement'), 'BS': ('BASEMENTS', 'basement'), 'BSLIFT': ('BASEMENTS', 'basement'),
    'CE10': ('CE', 'clubeden'), 'CE11': ('CE', 'clubeden'), 'CE12': ('CE', 'clubeden'), 'CE15': ('CE', 'clubeden'), 'CE3': ('CE', 'clubeden'),
    'CE30': ('CE', 'clubeden'), 'CE4': ('CE', 'clubeden'), 'CE5': ('CE', 'clubeden'), 'CE6': ('CE', 'clubeden'), 'CE7': ('CE', 'clubeden'),
    'CE7A': ('CE', 'clubeden'), 'CE9': ('CE', 'clubeden'), 'DB1': ('DB1', 'deadbiome'), 'DB3': ('DB3', 'deadbiome'), 'DB4': ('DB4', 'deadbiome'),
    'DB5': ('DB5', 'deadbiome'), 'DB6': ('DB6', 'deadbiome'), 'DB6Cave': ('DB6', 'deadbiome'), 'DB7': ('DB7', 'deadbiome'),
    'DB8': (None, 'deadbiome'), 'DB9': ('DB9', 'deadbiome'), 'EAngel1': (None, None), 'EAngel2': (None, None), 'EDeath': (None, None),
    'EGameEnd': (None, None), 'EPeyote1': (None, None), 'O1': ('O1', 'ocean'), 'O2': ('O1', 'ocean'), 'PH1': ('PH', 'house'),
    'PH10': ('PH', 'house'), 'PH11': ('PH', 'house'), 'PH12': ('PH', 'house'), 'PH13': ('PH', 'house'), 'PH14': ('PH', 'house'),
    'PH15': ('PH', 'house'), 'PH16': ('PH', 'house'), 'PH17': ('PH', 'house'), 'PH2': ('PH', 'house'), 'PH3': ('PH', 'house'),
    'PH5': ('PH', 'house'), 'PH6': ('PH', 'house'), 'PH7': ('PH', 'house'), 'PH8': ('PH', 'house'), 'PH9': ('PH', 'house'),
    'R1': ('R1', 'rainforest'), 'R10': ('R4', 'rainforest'), 'R11': ('R1', 'rainforest'), 'R2': ('R2', 'rainforest'), 'R3': ('R3', 'rainforest'),
    'R4': ('R4', 'rainforest'), 'R5': ('R5', 'rainforest'), 'R6': ('R6', 'rainforest'), 'R7': ('R7', 'rainforest'), 'R8': ('R8', 'rainforest'),
    'R9': ('R9', 'rainforest'), 'RHUT': ('R7', 'rainforest'), 'S1': ('S1', 'savannah'), 'S2': ('S2', 'savannah'), 'S3': ('S3', 'savannah'),
    'S4': ('S4', 'savannah'), 'S4BAOBAB': (None, 'savannah'), 'S5': ('S5', 'savannah'), 'S6': ('S6', 'savannah'), 'S7': ('S7', 'savannah'),
    'S8': ('S8', 'savannah'), 'S9': ('S9', 'savannah'), 'T1': ('R4', 'tunnels'), 'T10': ('TO2S6', 'tunnels'), 'T11': (None, 'tunnels'),
    'T12': ('O1', 'tunnels'), 'T2': ('TR10DB1', 'tunnels'), 'T3': ('TR10DB1', 'tunnels'), 'T4': ('TDB1R10', 'tunnels'), 'T5': (None, 'tunnels'),
    'T6': ('DB1', 'tunnels'), 'T7': ('S6', 'tunnels'), 'T8': ('TS6O2', 'tunnels'), 'T9': ('TS6O2', 'tunnels'), 'W1': ('O1', 'undersea'),
    'W2': ('O1', 'undersea'), 'W3': ('O1', 'undersea'),
}
# plant name -> site name (CPlant ctor dll:10072210 arg 5; the plant cel is that site's cel)
PLANTS = {'AR1PA': 'R1', 'AR2PA': 'R2', 'AR2BA': 'R2', 'AR2OR': 'R2', 'AR3AV': 'R3', 'AR3YA': 'R3', 'AR3BR': 'R3', 'AR3EG': 'R3', 'AR4LI': 'R4', 'AR4BR': 'R4', 'AR4NV': 'R4', 'AR5AV': 'R5', 'AR5CI': 'R5', 'AR5GU': 'R5', 'AR5BZ': 'R5', 'AR6LI': 'R6', 'AR6BA': 'R6', 'AR6AN': 'R6', 'AR6HG': 'R6', 'AR7OR': 'R7', 'AR7AG': 'R7', 'AR7AV': 'R7', 'AR8DA': 'R8', 'AR8BA': 'R8', 'AR8CO': 'R8', 'AR9BF': 'R9', 'AR9MA': 'R9', 'AR9BA': 'R9', 'AR9LO': 'R9', 'AR9PA': 'R9', 'AR4YA': 'R10', 'AR4BF': 'R10', 'AR1MA': 'R11', 'AR1LO': 'R11', 'AS1AG': 'S1', 'AS2FG1': 'S2', 'AS2FG2': 'S2', 'AS2EP': 'S2', 'AS3AL': 'S3', 'AS3OK': 'S3', 'AS4BB': 'S4', 'AS5OK': 'S5', 'AS5WM': 'S5', 'AS6FG': 'S6', 'AS6PP': 'S6', 'AS6EG': 'S6', 'AS7SQ': 'S7', 'AS7BL1': 'S7', 'AS7BL2': 'S7', 'AS7BL3': 'S7', 'AS7BL4': 'S7', 'AS7EG': 'S7', 'AS8SG': 'S8', 'AS8HE1': 'S8', 'AS8HE2': 'S8', 'AS9PE': 'S9', 'AS9AV': 'S9', 'AS9BL1': 'S9', 'AS9BL2': 'S9', 'AO2CC': 'O2', 'ADB1XX': 'DB1', 'ADB3XX': 'DB3', 'ADB4XX': 'DB4', 'ADB5XX': 'DB5', 'ADB6XX': 'DB6', 'ADB7XX': 'DB7', 'ADB9XX': 'DB9'}
# game objects: name -> tuple. food: ("food", nameId, flags, plant, slot, nutrition, protein, weight, water, age70, age74, regrow, code, pokes)
# goggles/mask: (cls, nameId, flags, pokes); cyl: (cyl, nameId, flags, fxKey, fxSite, pokes); consumable: (consumable, nameId, flags, code, maxDoses, wFull, wEmpty, pokes)
# single: (single, nameId, flags, code, pokes); other: (other, nameId, flags, pokes). pokes = [(offset, bytes, value, Set|Or)] applied after construction
OBJECTS = {
    'adren1': ('consumable', 224, 16, 30, 1, 0.2, 0.0, []),
    'adren2': ('consumable', 224, 16, 30, 1, 0.2, 0.0, []),
    'adren3': ('consumable', 224, 16, 30, 1, 0.2, 0.0, []),
    'adren4': ('consumable', 224, 16, 30, 1, 0.2, 0.0, []),
    'adren5': ('consumable', 224, 16, 30, 1, 0.2, 0.0, []),
    'alarm': ('other', 274, 0, []),
    'apple': ('other', 247, -2147483648, [(24, 4, 32768, 'Set')]),
    'apple2': ('other', 247, -2147483648, [(24, 4, 32768, 'Set')]),
    'baby1': ('other', 218, 524352, []),
    'baby2': ('other', 218, 524352, []),
    'baby3': ('other', 218, 524352, []),
    'baby4': ('other', 218, 524352, []),
    'baby5': ('other', 218, 524352, []),
    'baby6': ('other', 218, 524352, []),
    'baby7': ('other', 218, 524352, []),
    'babylab': ('other', 218, 524352, [(64, 2, 2, 'Set')]),
    'beef1': ('food', 276, 536870912, None, 0, 20.0, 0.5, 4.0, 1.0, -1, -1, -1, 90, []),
    'beef10': ('food', 276, 536870912, None, 0, 20.0, 0.5, 4.0, 1.0, -1, -1, -1, 90, []),
    'beef11': ('food', 276, 536870912, None, 0, 20.0, 0.5, 4.0, 1.0, -1, -1, -1, 90, []),
    'beef12': ('food', 276, 536870912, None, 0, 20.0, 0.5, 4.0, 1.0, -1, -1, -1, 90, []),
    'beef13': ('food', 276, 536870912, None, 0, 20.0, 0.5, 4.0, 1.0, -1, -1, -1, 90, []),
    'beef14': ('food', 276, 536870912, None, 0, 20.0, 0.5, 4.0, 1.0, -1, -1, -1, 90, []),
    'beef15': ('food', 276, 536870912, None, 0, 20.0, 0.5, 4.0, 1.0, -1, -1, -1, 90, []),
    'beef16': ('food', 276, 536870912, None, 0, 20.0, 0.5, 4.0, 1.0, -1, -1, -1, 90, []),
    'beef17': ('food', 276, 536870912, None, 0, 20.0, 0.5, 4.0, 1.0, -1, -1, -1, 90, []),
    'beef18': ('food', 276, 536870912, None, 0, 20.0, 0.5, 4.0, 1.0, -1, -1, -1, 90, []),
    'beef19': ('food', 276, 536870912, None, 0, 20.0, 0.5, 4.0, 1.0, -1, -1, -1, 90, []),
    'beef2': ('food', 276, 536870912, None, 0, 20.0, 0.5, 4.0, 1.0, -1, -1, -1, 90, []),
    'beef3': ('food', 276, 536870912, None, 0, 20.0, 0.5, 4.0, 1.0, -1, -1, -1, 90, []),
    'beef4': ('food', 276, 536870912, None, 0, 20.0, 0.5, 4.0, 1.0, -1, -1, -1, 90, []),
    'beef5': ('food', 276, 536870912, None, 0, 20.0, 0.5, 4.0, 1.0, -1, -1, -1, 90, []),
    'beef6': ('food', 276, 536870912, None, 0, 20.0, 0.5, 4.0, 1.0, -1, -1, -1, 90, []),
    'beef7': ('food', 276, 536870912, None, 0, 20.0, 0.5, 4.0, 1.0, -1, -1, -1, 90, []),
    'beef8': ('food', 276, 536870912, None, 0, 20.0, 0.5, 4.0, 1.0, -1, -1, -1, 90, []),
    'beef9': ('food', 276, 536870912, None, 0, 20.0, 0.5, 4.0, 1.0, -1, -1, -1, 90, []),
    'canteen': ('consumable', 219, 4102, 210, 5, 2.0, 1.0, []),
    'ceblue': ('other', 267, 0, []),
    'champ1': ('single', 277, 0, 60, []),
    'champ2': ('single', 277, 0, 60, []),
    'champ3': ('single', 277, 0, 60, []),
    'champ4': ('single', 277, 0, 60, []),
    'champ5': ('single', 277, 0, 60, []),
    'champ6': ('single', 277, 0, 60, []),
    'champ7': ('single', 277, 0, 60, []),
    'chlor1': ('consumable', 225, 24, 80, 5, 0.2, 0.0, []),
    'chlor2': ('consumable', 225, 24, 80, 5, 0.2, 0.0, []),
    'chlor3': ('consumable', 225, 24, 80, 5, 0.2, 0.0, []),
    'chlor4': ('consumable', 225, 24, 80, 5, 0.2, 0.0, []),
    'chlor5': ('consumable', 225, 24, 80, 5, 0.2, 0.0, []),
    'compass': ('other', 256, 0, []),
    'crowbar': ('other', 252, 0, []),
    'dat1': ('other', 240, 0, []),
    'dat2': ('other', 241, 0, []),
    'dat3': ('other', 242, 0, []),
    'dat4': ('other', 243, 0, []),
    'db1xx_1': ('food', 282, 536903744, 'ADB1XX', 1, 23.076923076923077, 0.1095890410958904, 2.0, 5.0, 86400, 172800, 259200, 1, []),
    'db1xx_2': ('food', 282, 536903744, 'ADB1XX', 2, 23.076923076923077, 0.1095890410958904, 2.0, 5.0, 86400, 172800, 259200, 1, []),
    'db1xx_3': ('food', 282, 536903744, 'ADB1XX', 3, 23.076923076923077, 0.1095890410958904, 2.0, 5.0, 86400, 172800, 259200, 1, []),
    'db1xx_4': ('food', 282, 536903744, 'ADB1XX', 4, 23.076923076923077, 0.1095890410958904, 2.0, 5.0, 86400, 172800, 259200, 1, []),
    'db1xx_5': ('food', 282, 536903744, 'ADB1XX', 5, 23.076923076923077, 0.1095890410958904, 2.0, 5.0, 86400, 172800, 259200, 1, []),
    'db3xx_1': ('food', 282, 536903744, 'ADB3XX', 1, 23.076923076923077, 0.1095890410958904, 2.0, 5.0, 86400, 172800, 259200, 1, []),
    'db3xx_2': ('food', 282, 536903744, 'ADB3XX', 3, 23.076923076923077, 0.1095890410958904, 2.0, 5.0, 86400, 172800, 259200, 1, []),
    'dbaby1': ('other', 218, 524352, []),
    'dbaby2': ('other', 218, 524352, []),
    'dbaby3': ('other', 218, 524352, []),
    'dbaby4': ('other', 218, 524352, []),
    'dbaby5': ('other', 218, 524352, []),
    'dbaby6': ('other', 218, 524352, []),
    'devlet': ('other', 306, 0, []),
    'file1': ('other', 262, 0, []),
    'file2': ('other', 263, 0, []),
    'file3': ('other', 264, 0, []),
    'fireCO22': ('other', 260, 1048576, []),
    'fireCO23': ('other', 260, 1048576, []),
    'fireCO24': ('other', 260, 1048576, []),
    'fireCO25': ('other', 260, 1048576, []),
    'fireH2O1': ('other', 261, 1048576, []),
    'fireH2O2': ('other', 261, 1048576, []),
    'fireH2O3': ('other', 261, 1048576, []),
    'fishrod': ('other', 227, 8389632, []),
    'flop1': ('other', 244, 0, []),
    'forekey': ('other', 245, 0, []),
    'friendlet': ('other', 270, 0, []),
    'fuse1': ('other', 278, 16777216, []),
    'fuse10': ('other', 278, 16777216, []),
    'fuse11': ('other', 278, 16777216, []),
    'fuse12': ('other', 278, 16777216, []),
    'fuse13': ('other', 278, 16777216, []),
    'fuse14': ('other', 278, 16777216, []),
    'fuse15': ('other', 278, 16777216, []),
    'fuse16': ('other', 278, 16777216, []),
    'fuse17': ('other', 278, 16777216, []),
    'fuse18': ('other', 278, 16777216, []),
    'fuse19': ('other', 278, 16777216, []),
    'fuse2': ('other', 278, 16777216, []),
    'fuse20': ('other', 278, 16777216, []),
    'fuse21': ('other', 278, 16777216, []),
    'fuse22': ('other', 278, 16777216, []),
    'fuse23': ('other', 278, 16777216, []),
    'fuse24': ('other', 278, 16777216, []),
    'fuse25': ('other', 278, 16777216, []),
    'fuse26': ('other', 278, 16777216, []),
    'fuse27': ('other', 278, 16777216, []),
    'fuse28': ('other', 278, 16777216, []),
    'fuse29': ('other', 278, 16777216, []),
    'fuse3': ('other', 278, 16777216, []),
    'fuse30': ('other', 278, 16777216, []),
    'fuse31': ('other', 278, 16777216, []),
    'fuse32': ('other', 278, 16777216, []),
    'fuse33': ('other', 278, 16777216, []),
    'fuse34': ('other', 278, 16777216, []),
    'fuse35': ('other', 278, 16777216, []),
    'fuse36': ('other', 278, 16777216, []),
    'fuse37': ('other', 278, 16777216, []),
    'fuse38': ('other', 278, 16777216, []),
    'fuse39': ('other', 278, 16777216, []),
    'fuse4': ('other', 278, 16777216, []),
    'fuse40': ('other', 278, 16777216, []),
    'fuse41': ('other', 278, 16777216, []),
    'fuse42': ('other', 278, 16777216, []),
    'fuse43': ('other', 278, 16777216, []),
    'fuse44': ('other', 278, 16777216, []),
    'fuse45': ('other', 278, 16777216, []),
    'fuse46': ('other', 278, 16777216, []),
    'fuse47': ('other', 278, 16777216, []),
    'fuse48': ('other', 278, 16777216, []),
    'fuse49': ('other', 278, 16777216, []),
    'fuse5': ('other', 278, 16777216, []),
    'fuse50': ('other', 278, 16777216, []),
    'fuse51': ('other', 278, 16777216, []),
    'fuse52': ('other', 278, 16777216, []),
    'fuse53': ('other', 278, 16777216, []),
    'fuse54': ('other', 278, 16777216, []),
    'fuse55': ('other', 278, 16777216, []),
    'fuse56': ('other', 278, 16777216, []),
    'fuse57': ('other', 278, 16777216, []),
    'fuse58': ('other', 278, 16777216, []),
    'fuse59': ('other', 278, 16777216, []),
    'fuse6': ('other', 278, 16777216, []),
    'fuse60': ('other', 278, 16777216, []),
    'fuse61': ('other', 278, 16777216, []),
    'fuse7': ('other', 278, 16777216, []),
    'fuse8': ('other', 278, 16777216, []),
    'fuse9': ('other', 278, 16777216, []),
    'goggles': ('goggles', 253, 0, []),
    'grub_1': ('food', 230, 536870976, None, 0, 2.307692307692308, 0.2191780821917808, 0.0, 1.0, 172800, 190080, 43200, 1, [(20, 4, 4194304, 'Or')]),
    'grub_10': ('food', 230, 536870976, None, 0, 2.307692307692308, 0.2191780821917808, 0.0, 1.0, 172800, 190080, 43200, 1, [(20, 4, 4194304, 'Or')]),
    'grub_2': ('food', 230, 536870976, None, 0, 2.307692307692308, 0.2191780821917808, 0.0, 1.0, 172800, 190080, 43200, 1, [(20, 4, 4194304, 'Or')]),
    'grub_3': ('food', 230, 536870976, None, 0, 2.307692307692308, 0.2191780821917808, 0.0, 1.0, 172800, 190080, 43200, 1, [(20, 4, 4194304, 'Or')]),
    'grub_4': ('food', 230, 536870976, None, 0, 2.307692307692308, 0.2191780821917808, 0.0, 1.0, 172800, 190080, 43200, 1, [(20, 4, 4194304, 'Or')]),
    'grub_5': ('food', 230, 536870976, None, 0, 2.307692307692308, 0.2191780821917808, 0.0, 1.0, 172800, 190080, 43200, 1, [(20, 4, 4194304, 'Or')]),
    'grub_6': ('food', 230, 536870976, None, 0, 2.307692307692308, 0.2191780821917808, 0.0, 1.0, 172800, 190080, 43200, 1, [(20, 4, 4194304, 'Or')]),
    'grub_7': ('food', 230, 536870976, None, 0, 2.307692307692308, 0.2191780821917808, 0.0, 1.0, 172800, 190080, 43200, 1, [(20, 4, 4194304, 'Or')]),
    'grub_8': ('food', 230, 536870976, None, 0, 2.307692307692308, 0.2191780821917808, 0.0, 1.0, 172800, 190080, 43200, 1, [(20, 4, 4194304, 'Or')]),
    'grub_9': ('food', 230, 536870976, None, 0, 2.307692307692308, 0.2191780821917808, 0.0, 1.0, 172800, 190080, 43200, 1, [(20, 4, 4194304, 'Or')]),
    'honey': ('food', 231, 536870976, None, 0, 75.0, 0.0, 1.0, 0.0, 172800, 259200, 345600, 1, []),
    'inhal1': ('consumable', 222, 16, 70, 5, 0.2, 0.0, []),
    'inhal2': ('consumable', 222, 16, 70, 5, 0.2, 0.0, []),
    'inhal3': ('consumable', 222, 16, 70, 5, 0.2, 0.0, []),
    'inhal4': ('consumable', 222, 16, 70, 5, 0.2, 0.0, []),
    'inhal5': ('consumable', 222, 16, 70, 5, 0.2, 0.0, []),
    'kukri': ('other', 249, 0, [(24, 4, 16384, 'Set')]),
    'latex': ('consumable', 220, 1073745926, 1, 1, 4.0, 0.4, []),
    'medipass': ('other', 250, 0, []),
    'mescal1': ('consumable', 221, 528, 320, 10, 1.0, 1.0, []),
    'mikememo': ('other', 308, 0, []),
    'o1fish_1': ('food', 325, 536871105, None, 0, 34.61538461538462, 0.547945205479452, 2.0, 2.0, 43200, 69120, 259200, 340, []),
    'o1fish_2': ('food', 325, 536871105, None, 0, 34.61538461538462, 0.547945205479452, 2.0, 2.0, 43200, 69120, 259200, 340, []),
    'o1fish_3': ('food', 325, 536871105, None, 0, 34.61538461538462, 0.547945205479452, 2.0, 2.0, 43200, 69120, 259200, 340, []),
    'o1fish_4': ('food', 325, 536871105, None, 0, 34.61538461538462, 0.547945205479452, 2.0, 2.0, 43200, 69120, 259200, 340, []),
    'o1fish_5': ('food', 325, 536871105, None, 0, 34.61538461538462, 0.547945205479452, 2.0, 2.0, 43200, 69120, 259200, 340, []),
    'o1fish_6': ('food', 325, 536871105, None, 0, 34.61538461538462, 0.547945205479452, 2.0, 2.0, 43200, 69120, 259200, 340, []),
    'o1p_1': ('food', 324, 536870977, None, 0, 34.61538461538462, 0.547945205479452, 2.0, 2.0, 86400, 172800, 345600, 340, []),
    'o1p_2': ('food', 324, 536870977, None, 0, 34.61538461538462, 0.547945205479452, 2.0, 2.0, 86400, 172800, 345600, 340, []),
    'o1p_3': ('food', 326, 536870977, None, 0, 34.61538461538462, 0.547945205479452, 2.0, 2.0, 43200, 69120, 345600, 340, []),
    'o1p_4': ('food', 326, 536870977, None, 0, 34.61538461538462, 0.547945205479452, 2.0, 2.0, 43200, 69120, 345600, 340, []),
    'o1p_5': ('food', 326, 536870977, None, 0, 34.61538461538462, 0.547945205479452, 2.0, 2.0, 43200, 69120, 345600, 340, []),
    'o2cc_1': ('food', 37, 536870976, 'AO2CC', 1, 103.84615384615385, 0.3068493150684931, 15.0, 15.0, 86400, 172800, 432000, 1, []),
    'o2cc_2': ('food', 37, 536870976, 'AO2CC', 2, 103.84615384615385, 0.3068493150684931, 15.0, 15.0, 86400, 172800, 432000, 1, []),
    'o2cc_3': ('food', 37, 536870976, 'AO2CC', 3, 103.84615384615385, 0.3068493150684931, 15.0, 15.0, 86400, 172800, 432000, 1, []),
    'o2cc_4': ('food', 37, 536870976, 'AO2CC', 4, 103.84615384615385, 0.3068493150684931, 15.0, 15.0, 86400, 172800, 432000, 1, []),
    'o2cc_5': ('food', 37, 536870976, 'AO2CC', 5, 103.84615384615385, 0.3068493150684931, 15.0, 15.0, 86400, 172800, 432000, 1, []),
    'o2p_1': ('food', 324, 536870977, None, 0, 34.61538461538462, 0.547945205479452, 2.0, 2.0, 86400, 172800, 345600, 340, []),
    'o2p_2': ('food', 326, 536870977, None, 0, 34.61538461538462, 0.547945205479452, 2.0, 2.0, 43200, 69120, 345600, 340, []),
    'o2p_3': ('food', 326, 536870977, None, 0, 34.61538461538462, 0.547945205479452, 2.0, 2.0, 43200, 69120, 345600, 340, []),
    'oxy_cyl': ('cyl', 232, 2048, 'sounds\\objects\\oxyhiss.wav@ce7', 'CE7', []),
    'oxy_mask': ('mask', 257, 0, []),
    'pamphlet': ('other', 269, 0, []),
    'poem1': ('other', 272, 0, [(24, 4, 2048, 'Set')]),
    'poem2': ('other', 273, 0, [(24, 4, 524288, 'Set')]),
    'poncho': ('other', 255, 512, []),
    'r10p_1': ('food', 329, 536870977, None, 0, 34.61538461538462, 0.547945205479452, 2.0, 2.0, 86400, 172800, 345600, 340, []),
    'r10p_2': ('food', 329, 536870977, None, 0, 34.61538461538462, 0.547945205479452, 2.0, 2.0, 86400, 172800, 345600, 340, []),
    'r10p_3': ('food', 329, 536870977, None, 0, 34.61538461538462, 0.547945205479452, 2.0, 2.0, 86400, 172800, 345600, 340, []),
    'r1ma_1': ('food', 5, 536870977, 'AR1MA', 1, 39.23076923076923, 0.3287671232876712, 3.0, 0.0, 86400, 172800, 432000, 10, []),
    'r1pa_1': ('food', 4, 536870976, 'AR1PA', 1, 13.846153846153847, 0.1095890410958904, 3.0, 6.0, 86400, 172800, 345600, 1, []),
    'r2ba_1': ('food', 7, 536870976, 'AR2BA', 1, 36.34615384615385, 0.12931506849315066, 3.0, 2.0, 86400, 172800, 302400, 1, []),
    'r2ba_2': ('food', 7, 536870976, 'AR2BA', 2, 36.34615384615385, 0.12931506849315066, 3.0, 2.0, 86400, 172800, 302400, 1, []),
    'r2ba_3': ('food', 7, 536870976, 'AR2BA', 3, 36.34615384615385, 0.12931506849315066, 3.0, 2.0, 86400, 172800, 302400, 1, []),
    'r2fish_1': ('food', 325, 536871105, None, 0, 34.61538461538462, 0.547945205479452, 2.0, 2.0, 43200, 69120, 259200, 340, []),
    'r2fish_2': ('food', 325, 536871105, None, 0, 34.61538461538462, 0.547945205479452, 2.0, 2.0, 43200, 69120, 259200, 340, []),
    'r2fish_3': ('food', 325, 536871105, None, 0, 34.61538461538462, 0.547945205479452, 2.0, 2.0, 43200, 69120, 259200, 340, []),
    'r2or_1': ('food', 8, 536870976, 'AR2OR', 1, 36.34615384615385, 0.12931506849315066, 3.0, 10.0, 86400, 172800, 432000, 1, []),
    'r2or_2': ('food', 8, 536870976, 'AR2OR', 2, 36.34615384615385, 0.12931506849315066, 3.0, 10.0, 86400, 172800, 432000, 1, []),
    'r2or_3': ('food', 8, 536870976, 'AR2OR', 3, 36.34615384615385, 0.12931506849315066, 3.0, 10.0, 86400, 172800, 432000, 1, []),
    'r2or_4': ('food', 8, 536870976, 'AR2OR', 4, 36.34615384615385, 0.12931506849315066, 3.0, 10.0, 86400, 172800, 432000, 1, []),
    'r2or_5': ('food', 8, 536870976, 'AR2OR', 5, 36.34615384615385, 0.12931506849315066, 3.0, 10.0, 86400, 172800, 432000, 1, []),
    'r2or_6': ('food', 8, 536870976, 'AR2OR', 6, 36.34615384615385, 0.12931506849315066, 3.0, 10.0, 86400, 172800, 432000, 1, []),
    'r2pa_1': ('food', 4, 536870976, 'AR2PA', 1, 13.846153846153847, 0.1095890410958904, 3.0, 6.0, 86400, 172800, 345600, 1, []),
    'r2pa_2': ('food', 4, 536870976, 'AR2PA', 2, 13.846153846153847, 0.1095890410958904, 3.0, 6.0, 86400, 172800, 345600, 1, []),
    'r2pa_3': ('food', 4, 536870976, 'AR2PA', 3, 13.846153846153847, 0.1095890410958904, 3.0, 6.0, 86400, 172800, 345600, 1, []),
    'r2pa_4': ('food', 4, 536870976, 'AR2PA', 4, 13.846153846153847, 0.1095890410958904, 3.0, 6.0, 86400, 172800, 345600, 1, []),
    'r3av_1': ('food', 3, 536870976, 'AR3AV', 1, 40.38461538461539, 0.4383561643835616, 3.0, 2.0, 86400, 172800, 345600, 1, []),
    'r3av_2': ('food', 3, 536870976, 'AR3AV', 2, 40.38461538461539, 0.4383561643835616, 3.0, 2.0, 86400, 172800, 345600, 1, []),
    'r3av_3': ('food', 3, 536870976, 'AR3AV', 3, 40.38461538461539, 0.4383561643835616, 3.0, 2.0, 86400, 172800, 345600, 1, []),
    'r3av_4': ('food', 3, 536870976, 'AR3AV', 4, 40.38461538461539, 0.4383561643835616, 3.0, 2.0, 86400, 172800, 345600, 1, []),
    'r3eg_1': ('food', 280, 536870977, 'AR3EG', 1, 75.0, 2.191780821917808, 1.0, 2.0, 86400, 172800, 432000, 350, []),
    'r3eg_2': ('food', 280, 536870977, 'AR3EG', 2, 75.0, 2.191780821917808, 1.0, 2.0, 86400, 172800, 432000, 350, []),
    'r3eg_3': ('food', 280, 536870977, 'AR3EG', 3, 75.0, 2.191780821917808, 1.0, 2.0, 86400, 172800, 432000, 350, []),
    'r3eg_4': ('food', 280, 536870977, 'AR3EG', 4, 75.0, 2.191780821917808, 1.0, 2.0, 86400, 172800, 432000, 350, []),
    'r3eg_5': ('food', 280, 536870977, 'AR3EG', 5, 75.0, 2.191780821917808, 1.0, 2.0, 86400, 172800, 432000, 350, []),
    'r3eg_6': ('food', 280, 536870977, 'AR3EG', 6, 75.0, 2.191780821917808, 1.0, 2.0, 86400, 172800, 432000, 350, []),
    'r3p_1': ('food', 324, 536870977, None, 0, 34.61538461538462, 0.547945205479452, 2.0, 2.0, 86400, 172800, 345600, 340, []),
    'r3p_2': ('food', 324, 536870977, None, 0, 34.61538461538462, 0.547945205479452, 2.0, 2.0, 86400, 172800, 345600, 340, []),
    'r3ya_1': ('food', 9, 536870977, 'AR3YA', 1, 48.46153846153847, 0.2191780821917808, 3.0, 1.0, 86400, 172800, 259200, 20, []),
    'r4bf_1': ('food', 20, 536870976, 'AR4BF', 1, 55.38461538461539, 0.2191780821917808, 10.0, 0.0, 86400, 172800, 518400, 1, []),
    'r4bf_2': ('food', 20, 536870976, 'AR4BF', 2, 55.38461538461539, 0.2191780821917808, 10.0, 0.0, 86400, 172800, 518400, 1, []),
    'r4bf_3': ('food', 20, 536870976, 'AR4BF', 3, 55.38461538461539, 0.2191780821917808, 10.0, 0.0, 86400, 172800, 518400, 1, []),
    'r4nv_1': ('food', 11, 536903744, 'AR4NV', 1, 0.0, 0.0, 0.0, 0.0, 86400, 129600, 172800, 280, []),
    'r4nv_2': ('food', 11, 536903744, 'AR4NV', 2, 0.0, 0.0, 0.0, 0.0, 86400, 129600, 172800, 280, []),
    'r4nv_3': ('food', 11, 536903744, 'AR4NV', 3, 0.0, 0.0, 0.0, 0.0, 86400, 129600, 172800, 280, []),
    'r4p_1': ('food', 324, 536870977, None, 0, 34.61538461538462, 0.547945205479452, 2.0, 2.0, 86400, 172800, 345600, 340, []),
    'r4p_2': ('food', 324, 536870977, None, 0, 34.61538461538462, 0.547945205479452, 2.0, 2.0, 86400, 172800, 345600, 340, []),
    'r4ya_1': ('food', 9, 536870977, 'AR4YA', 1, 48.46153846153847, 0.2191780821917808, 3.0, 1.0, 86400, 172800, 259200, 20, []),
    'r5av_1': ('food', 3, 536870976, 'AR5AV', 1, 40.38461538461539, 0.4383561643835616, 3.0, 2.0, 86400, 172800, 345600, 1, []),
    'r5av_2': ('food', 3, 536870976, 'AR5AV', 2, 40.38461538461539, 0.4383561643835616, 3.0, 2.0, 86400, 172800, 345600, 1, []),
    'r5av_3': ('food', 3, 536870976, 'AR5AV', 3, 40.38461538461539, 0.4383561643835616, 3.0, 2.0, 86400, 172800, 345600, 1, []),
    'r5av_4': ('food', 3, 536870976, 'AR5AV', 4, 40.38461538461539, 0.4383561643835616, 3.0, 2.0, 86400, 172800, 345600, 1, []),
    'r5av_5': ('food', 3, 536870976, 'AR5AV', 5, 40.38461538461539, 0.4383561643835616, 3.0, 2.0, 86400, 172800, 345600, 1, []),
    'r5av_6': ('food', 3, 536870976, 'AR5AV', 6, 40.38461538461539, 0.4383561643835616, 3.0, 2.0, 86400, 172800, 345600, 1, []),
    'r5bz_1': ('food', 14, 536870976, 'AR5BZ', 1, 23.076923076923077, 1.095890410958904, 3.0, 0.0, 86400, 172800, 518400, 1, []),
    'r5bz_2': ('food', 14, 536870976, 'AR5BZ', 2, 23.076923076923077, 1.095890410958904, 3.0, 0.0, 86400, 172800, 518400, 1, []),
    'r5bz_3': ('food', 14, 536870976, 'AR5BZ', 3, 23.076923076923077, 1.095890410958904, 3.0, 0.0, 86400, 172800, 518400, 1, []),
    'r5bz_4': ('food', 14, 536870976, 'AR5BZ', 4, 23.076923076923077, 1.095890410958904, 3.0, 0.0, 86400, 172800, 518400, 1, []),
    'r5bz_5': ('food', 14, 536870976, 'AR5BZ', 5, 23.076923076923077, 1.095890410958904, 3.0, 0.0, 86400, 172800, 518400, 1, []),
    'r5ci_1': ('food', 12, 536870976, 'AR5CI', 1, 0.0, 0.0, 1.0, 0.0, 86400, 172800, 259200, 150, []),
    'r5ci_2': ('food', 12, 536870976, 'AR5CI', 2, 0.0, 0.0, 1.0, 0.0, 86400, 172800, 259200, 150, []),
    'r5gu_1': ('food', 13, 536870976, 'AR5GU', 1, 0.0, 0.0, 1.0, 0.0, 86400, 172800, 388800, 50, []),
    'r5gu_2': ('food', 13, 536870976, 'AR5GU', 2, 0.0, 0.0, 1.0, 0.0, 86400, 172800, 388800, 50, []),
    'r5gu_3': ('food', 13, 536870976, 'AR5GU', 3, 0.0, 0.0, 1.0, 0.0, 86400, 172800, 388800, 50, []),
    'r5gu_4': ('food', 13, 536870976, 'AR5GU', 4, 0.0, 0.0, 1.0, 0.0, 86400, 172800, 388800, 50, []),
    'r6an_1': ('food', 15, 536870976, 'AR6AN', 1, 0.0, 0.0, 1.0, 0.0, 86400, 172800, 259200, 140, []),
    'r6an_2': ('food', 15, 536870976, 'AR6AN', 2, 0.0, 0.0, 1.0, 0.0, 86400, 172800, 259200, 140, []),
    'r6an_3': ('food', 15, 536870976, 'AR6AN', 3, 0.0, 0.0, 1.0, 0.0, 86400, 172800, 259200, 140, []),
    'r6an_4': ('food', 15, 536870976, 'AR6AN', 4, 0.0, 0.0, 1.0, 0.0, 86400, 172800, 259200, 140, []),
    'r6an_5': ('food', 15, 536870976, 'AR6AN', 5, 0.0, 0.0, 1.0, 0.0, 86400, 172800, 259200, 140, []),
    'r6ba_1': ('food', 7, 536870976, 'AR6BA', 1, 36.34615384615385, 0.12931506849315066, 3.0, 2.0, 86400, 172800, 302400, 1, []),
    'r6ba_2': ('food', 7, 536870976, 'AR6BA', 2, 36.34615384615385, 0.12931506849315066, 3.0, 2.0, 86400, 172800, 302400, 1, []),
    'r6ba_3': ('food', 7, 536870976, 'AR6BA', 3, 36.34615384615385, 0.12931506849315066, 3.0, 2.0, 86400, 172800, 302400, 1, []),
    'r6p_1': ('food', 324, 536870977, None, 0, 34.61538461538462, 0.547945205479452, 2.0, 2.0, 86400, 172800, 345600, 340, []),
    'r6p_2': ('food', 329, 536870977, None, 0, 34.61538461538462, 0.547945205479452, 2.0, 2.0, 86400, 172800, 345600, 340, []),
    'r6p_3': ('food', 329, 536870977, None, 0, 34.61538461538462, 0.547945205479452, 2.0, 2.0, 86400, 172800, 345600, 340, []),
    'r7ag_1': ('food', 17, 536870976, 'AR7AG', 1, 0.0, 0.0, 0.0, 0.0, 86400, 129600, 172800, 180, []),
    'r7ag_2': ('food', 17, 536870976, 'AR7AG', 2, 0.0, 0.0, 0.0, 0.0, 86400, 129600, 172800, 180, [(20, 4, 67108864, 'Or')]),
    'r7av_1': ('food', 3, 536870976, 'AR7AV', 1, 40.38461538461539, 0.4383561643835616, 3.0, 2.0, 86400, 172800, 345600, 1, []),
    'r7av_2': ('food', 3, 536870976, 'AR7AV', 2, 40.38461538461539, 0.4383561643835616, 3.0, 2.0, 86400, 172800, 345600, 1, []),
    'r7av_3': ('food', 3, 536870976, 'AR7AV', 3, 40.38461538461539, 0.4383561643835616, 3.0, 2.0, 86400, 172800, 345600, 1, []),
    'r7av_4': ('food', 3, 536870976, 'AR7AV', 4, 40.38461538461539, 0.4383561643835616, 3.0, 2.0, 86400, 172800, 345600, 1, []),
    'r7av_5': ('food', 3, 536870976, 'AR7AV', 5, 40.38461538461539, 0.4383561643835616, 3.0, 2.0, 86400, 172800, 345600, 1, []),
    'r7fish_1': ('food', 325, 536871105, None, 0, 34.61538461538462, 0.547945205479452, 2.0, 2.0, 43200, 69120, 259200, 340, []),
    'r7fish_2': ('food', 336, 536871105, None, 0, 34.61538461538462, 0.547945205479452, 2.0, 2.0, 43200, 69120, 259200, 340, []),
    'r7fish_3': ('food', 325, 536871105, None, 0, 34.61538461538462, 0.547945205479452, 2.0, 2.0, 43200, 69120, 259200, 340, []),
    'r7fish_4': ('food', 325, 536871105, None, 0, 34.61538461538462, 0.547945205479452, 2.0, 2.0, 43200, 69120, 259200, 340, []),
    'r7or_1': ('food', 8, 536870976, 'AR7OR', 1, 36.34615384615385, 0.12931506849315066, 3.0, 10.0, 86400, 172800, 432000, 1, []),
    'r7or_2': ('food', 8, 536870976, 'AR7OR', 2, 36.34615384615385, 0.12931506849315066, 3.0, 10.0, 86400, 172800, 432000, 1, []),
    'r7or_3': ('food', 8, 536870976, 'AR7OR', 3, 36.34615384615385, 0.12931506849315066, 3.0, 10.0, 86400, 172800, 432000, 1, []),
    'r7or_4': ('food', 8, 536870976, 'AR7OR', 4, 36.34615384615385, 0.12931506849315066, 3.0, 10.0, 86400, 172800, 432000, 1, []),
    'r7or_5': ('food', 8, 536870976, 'AR7OR', 5, 36.34615384615385, 0.12931506849315066, 3.0, 10.0, 86400, 172800, 432000, 1, []),
    'r7p_1': ('food', 324, 536870977, None, 0, 34.61538461538462, 0.547945205479452, 2.0, 2.0, 86400, 172800, 345600, 340, []),
    'r8ba_1': ('food', 7, 536870976, 'AR8BA', 1, 36.34615384615385, 0.12931506849315066, 3.0, 2.0, 86400, 172800, 302400, 1, []),
    'r8ba_2': ('food', 7, 536870976, 'AR8BA', 2, 36.34615384615385, 0.12931506849315066, 3.0, 2.0, 86400, 172800, 302400, 1, []),
    'r8ba_3': ('food', 7, 536870976, 'AR8BA', 3, 36.34615384615385, 0.12931506849315066, 3.0, 2.0, 86400, 172800, 302400, 1, []),
    'r8ba_4': ('food', 7, 536870976, 'AR8BA', 4, 36.34615384615385, 0.12931506849315066, 3.0, 2.0, 86400, 172800, 302400, 1, []),
    'r8ba_5': ('food', 7, 536870976, 'AR8BA', 5, 36.34615384615385, 0.12931506849315066, 3.0, 2.0, 86400, 172800, 302400, 1, []),
    'r8co_1': ('food', 19, 536870976, 'AR8CO', 1, 20.0, 0.0, 1.0, 0.0, 86400, 172800, 345600, 40, []),
    'r8co_2': ('food', 19, 536870976, 'AR8CO', 2, 20.0, 0.0, 1.0, 0.0, 86400, 172800, 345600, 40, []),
    'r8co_3': ('food', 19, 536870976, 'AR8CO', 3, 20.0, 0.0, 1.0, 0.0, 86400, 172800, 345600, 40, []),
    'r8co_4': ('food', 19, 536870976, 'AR8CO', 4, 20.0, 0.0, 1.0, 0.0, 86400, 172800, 345600, 40, []),
    'r8da_1': ('food', 18, 536870976, 'AR8DA', 1, 11.538461538461538, 0.0, 2.0, 0.0, 86400, 172800, 259200, 160, []),
    'r8da_2': ('food', 18, 536870976, 'AR8DA', 2, 11.538461538461538, 0.0, 2.0, 0.0, 86400, 172800, 259200, 160, []),
    'r8da_3': ('food', 18, 536870976, 'AR8DA', 3, 11.538461538461538, 0.0, 2.0, 0.0, 86400, 172800, 259200, 160, []),
    'r8da_4': ('food', 18, 536870976, 'AR8DA', 4, 11.538461538461538, 0.0, 2.0, 0.0, 86400, 172800, 259200, 160, []),
    'r8da_5': ('food', 18, 536870976, 'AR8DA', 5, 11.538461538461538, 0.0, 2.0, 0.0, 86400, 172800, 259200, 160, []),
    'r8p_1': ('food', 324, 536870977, None, 0, 34.61538461538462, 0.547945205479452, 2.0, 2.0, 86400, 172800, 345600, 340, []),
    'r9ba_1': ('food', 7, 536870976, 'AR9BA', 1, 36.34615384615385, 0.12931506849315066, 3.0, 2.0, 86400, 172800, 302400, 1, []),
    'r9ba_2': ('food', 7, 536870976, 'AR9BA', 2, 36.34615384615385, 0.12931506849315066, 3.0, 2.0, 86400, 172800, 302400, 1, []),
    'r9ba_3': ('food', 7, 536870976, 'AR9BA', 3, 36.34615384615385, 0.12931506849315066, 3.0, 2.0, 86400, 172800, 302400, 1, []),
    'r9bf_1': ('food', 20, 536870976, 'AR9BF', 1, 55.38461538461539, 0.2191780821917808, 10.0, 0.0, 86400, 172800, 518400, 1, []),
    'r9bf_2': ('food', 20, 536870976, 'AR9BF', 2, 55.38461538461539, 0.2191780821917808, 10.0, 0.0, 86400, 172800, 518400, 1, []),
    'r9bf_3': ('food', 20, 536870976, 'AR9BF', 3, 55.38461538461539, 0.2191780821917808, 10.0, 0.0, 86400, 172800, 518400, 1, []),
    'r9bf_4': ('food', 20, 536870976, 'AR9BF', 4, 55.38461538461539, 0.2191780821917808, 10.0, 0.0, 86400, 172800, 518400, 1, []),
    'r9bf_5': ('food', 20, 536870976, 'AR9BF', 5, 55.38461538461539, 0.2191780821917808, 10.0, 0.0, 86400, 172800, 518400, 1, []),
    'r9bf_6': ('food', 20, 536870976, 'AR9BF', 6, 55.38461538461539, 0.2191780821917808, 10.0, 0.0, 86400, 172800, 518400, 1, []),
    'r9ma_1': ('food', 5, 536870977, 'AR9MA', 1, 39.23076923076923, 0.3287671232876712, 3.0, 0.0, 86400, 172800, 432000, 10, []),
    'r9pa_1': ('food', 4, 536870976, 'AR9PA', 1, 13.846153846153847, 0.1095890410958904, 3.0, 6.0, 86400, 172800, 345600, 1, []),
    'r9pa_2': ('food', 4, 536870976, 'AR9PA', 2, 13.846153846153847, 0.1095890410958904, 3.0, 6.0, 86400, 172800, 345600, 1, []),
    'rfmap': ('other', 265, 134217728, []),
    'russkey': ('other', 246, 0, [(24, 4, 4096, 'Set')]),
    'russmemo': ('other', 307, 512, [(24, 4, 8, 'Set')]),
    's1ag_1': ('food', 22, 536920128, 'AS1AG', 1, 11.538461538461538, 0.1095890410958904, 2.0, 2.0, 86400, 172800, 345600, 1, []),
    's1ag_2': ('food', 22, 536920128, 'AS1AG', 2, 11.538461538461538, 0.1095890410958904, 2.0, 2.0, 86400, 172800, 345600, 1, []),
    's1ag_3': ('food', 22, 536920128, 'AS1AG', 3, 11.538461538461538, 0.1095890410958904, 2.0, 2.0, 86400, 172800, 345600, 1, []),
    's1p_1': ('food', 327, 536870977, None, 0, 34.61538461538462, 0.547945205479452, 2.0, 2.0, 86400, 172800, 345600, 340, []),
    's1p_2': ('food', 324, 536870977, None, 0, 34.61538461538462, 0.547945205479452, 2.0, 2.0, 86400, 172800, 345600, 340, []),
    's2ep_1': ('food', 25, 536870977, 'AS2EP', 1, 0.11538461538461539, 0.0, 1.0, 0.0, 86400, 172800, 259200, 261, []),
    's2ep_2': ('food', 25, 536870977, 'AS2EP', 2, 0.11538461538461539, 0.0, 1.0, 0.0, 86400, 172800, 259200, 261, []),
    's2ep_3': ('food', 25, 536870977, 'AS2EP', 3, 0.11538461538461539, 0.0, 1.0, 0.0, 86400, 172800, 259200, 261, []),
    's2fg1_1': ('food', 24, 536870976, 'AS2FG1', 1, 13.846153846153847, 0.0, 2.0, 2.0, 86400, 172800, 259200, 1, []),
    's2fg1_2': ('food', 24, 536870976, 'AS2FG1', 2, 13.846153846153847, 0.0, 2.0, 2.0, 86400, 172800, 259200, 1, []),
    's2fg2_1': ('food', 24, 536870976, 'AS2FG2', 1, 13.846153846153847, 0.0, 2.0, 2.0, 86400, 172800, 259200, 1, []),
    's2fg2_2': ('food', 24, 536870976, 'AS2FG2', 2, 13.846153846153847, 0.0, 2.0, 2.0, 86400, 172800, 259200, 1, []),
    's2p_1': ('food', 327, 536870977, None, 0, 34.61538461538462, 0.547945205479452, 2.0, 2.0, 86400, 172800, 345600, 340, []),
    's3al_1': ('food', 26, 536920128, 'AS3AL', 1, 13.846153846153847, 1.095890410958904, 2.0, 0.0, 86400, 172800, 432000, 1, []),
    's3al_2': ('food', 26, 536920128, 'AS3AL', 2, 13.846153846153847, 1.095890410958904, 2.0, 0.0, 86400, 172800, 432000, 1, []),
    's3al_3': ('food', 26, 536920128, 'AS3AL', 3, 13.846153846153847, 1.095890410958904, 2.0, 0.0, 86400, 172800, 432000, 1, []),
    's3ok_1': ('food', 27, 536920128, 'AS3OK', 1, 13.846153846153847, 0.0, 2.0, 2.0, 86400, 172800, 345600, 1, []),
    's3ok_2': ('food', 27, 536920128, 'AS3OK', 2, 13.846153846153847, 0.0, 2.0, 2.0, 86400, 172800, 345600, 1, []),
    's4p_1': ('food', 324, 536870977, None, 0, 34.61538461538462, 0.547945205479452, 2.0, 2.0, 86400, 172800, 345600, 340, []),
    's5ok_1': ('food', 27, 536920128, 'AS5OK', 1, 13.846153846153847, 0.0, 2.0, 2.0, 86400, 172800, 345600, 1, []),
    's5ok_2': ('food', 27, 536920128, 'AS5OK', 2, 13.846153846153847, 0.0, 2.0, 2.0, 86400, 172800, 345600, 1, []),
    's5ok_3': ('food', 27, 536920128, 'AS5OK', 3, 13.846153846153847, 0.0, 2.0, 2.0, 86400, 172800, 345600, 1, []),
    's5wm_1': ('food', 29, 536911936, 'AS5WM', 1, 46.15384615384615, 0.0, 10.0, 60.0, 86400, 172800, 518400, 1, []),
    's5wm_2': ('food', 29, 536911936, 'AS5WM', 2, 46.15384615384615, 0.0, 10.0, 60.0, 86400, 172800, 518400, 1, []),
    's5wm_3': ('food', 29, 536911936, 'AS5WM', 3, 46.15384615384615, 0.0, 10.0, 60.0, 86400, 172800, 518400, 1, []),
    's5wm_4': ('food', 29, 536911936, 'AS5WM', 4, 46.15384615384615, 0.0, 10.0, 60.0, 86400, 172800, 518400, 1, []),
    's6eg_1': ('food', 280, 536870977, 'AS6EG', 1, 75.0, 2.191780821917808, 1.0, 2.0, 86400, 172800, 432000, 350, []),
    's6eg_2': ('food', 280, 536870977, 'AS6EG', 2, 75.0, 2.191780821917808, 1.0, 2.0, 86400, 172800, 432000, 350, []),
    's6eg_3': ('food', 280, 536870977, 'AS6EG', 3, 75.0, 2.191780821917808, 1.0, 2.0, 86400, 172800, 432000, 350, []),
    's6eg_4': ('food', 280, 536870977, 'AS6EG', 4, 75.0, 2.191780821917808, 1.0, 2.0, 86400, 172800, 432000, 350, []),
    's6eg_5': ('food', 280, 536870977, 'AS6EG', 5, 75.0, 2.191780821917808, 1.0, 2.0, 86400, 172800, 432000, 350, []),
    's6fg_1': ('food', 24, 536870976, 'AS6FG', 1, 13.846153846153847, 0.0, 2.0, 2.0, 86400, 172800, 259200, 1, []),
    's6fg_2': ('food', 24, 536870976, 'AS6FG', 2, 13.846153846153847, 0.0, 2.0, 2.0, 86400, 172800, 259200, 1, []),
    's6fg_3': ('food', 24, 536870976, 'AS6FG', 3, 13.846153846153847, 0.0, 2.0, 2.0, 86400, 172800, 259200, 1, []),
    's6pp_1': ('food', 23, 536879168, 'AS6PP', 1, 13.846153846153847, 0.0, 2.0, 2.0, 86400, 172800, 432000, 1, []),
    's6pp_2': ('food', 23, 536879168, 'AS6PP', 2, 13.846153846153847, 0.0, 2.0, 2.0, 86400, 172800, 432000, 1, []),
    's6pp_3': ('food', 23, 536879168, 'AS6PP', 3, 13.846153846153847, 0.0, 2.0, 2.0, 86400, 172800, 432000, 1, []),
    's6pp_4': ('food', 23, 536879168, 'AS6PP', 4, 13.846153846153847, 0.0, 2.0, 2.0, 86400, 172800, 432000, 1, []),
    's7eg_1': ('food', 280, 536870977, 'AS7EG', 1, 75.0, 2.191780821917808, 1.0, 2.0, 86400, 172800, 432000, 350, []),
    's7eg_2': ('food', 280, 536870977, 'AS7EG', 2, 75.0, 2.191780821917808, 1.0, 2.0, 86400, 172800, 432000, 350, []),
    's7eg_3': ('food', 280, 536870977, 'AS7EG', 3, 75.0, 2.191780821917808, 1.0, 2.0, 86400, 172800, 432000, 350, []),
    's7eg_4': ('food', 280, 536870977, 'AS7EG', 4, 75.0, 2.191780821917808, 1.0, 2.0, 86400, 172800, 432000, 350, []),
    's7eg_5': ('food', 280, 536870977, 'AS7EG', 5, 75.0, 2.191780821917808, 1.0, 2.0, 86400, 172800, 432000, 350, []),
    's7eg_6': ('food', 280, 536870977, 'AS7EG', 6, 75.0, 2.191780821917808, 1.0, 2.0, 86400, 172800, 432000, 350, []),
    's7eg_7': ('food', 280, 536870977, 'AS7EG', 7, 75.0, 2.191780821917808, 1.0, 2.0, 86400, 172800, 432000, 1, []),
    's7eg_8': ('food', 280, 536870977, 'AS7EG', 8, 75.0, 2.191780821917808, 1.0, 2.0, 86400, 172800, 432000, 1, []),
    's7sq_1': ('food', 31, 536920128, 'AS7SQ', 1, 13.846153846153847, 0.0, 2.0, 2.0, 86400, 172800, 432000, 1, []),
    's7sq_2': ('food', 31, 536920128, 'AS7SQ', 2, 13.846153846153847, 0.0, 2.0, 2.0, 86400, 172800, 432000, 1, []),
    's7sq_3': ('food', 31, 536920128, 'AS7SQ', 3, 13.846153846153847, 0.0, 2.0, 2.0, 86400, 172800, 432000, 1, []),
    's7sq_4': ('food', 31, 536920128, 'AS7SQ', 4, 13.846153846153847, 0.0, 2.0, 2.0, 86400, 172800, 432000, 1, []),
    's8he1_1': ('food', 281, 538968128, 'AS8HE1', 1, 0.0, 0.0, 1.0, 0.0, -1, -1, 1, 1, []),
    's8he1_2': ('food', 281, 538968128, 'AS8HE1', 2, 0.0, 0.0, 1.0, 0.0, -1, -1, 1, 1, []),
    's8he1_3': ('food', 281, 538968128, 'AS8HE1', 3, 0.0, 0.0, 1.0, 0.0, -1, -1, 1, 1, []),
    's8he1_4': ('food', 281, 538968128, 'AS8HE1', 4, 0.0, 0.0, 1.0, 0.0, -1, -1, 1, 1, []),
    's8he1_5': ('food', 281, 538968128, 'AS8HE1', 5, 0.0, 0.0, 1.0, 0.0, -1, -1, 1, 1, []),
    's8he1_6': ('food', 281, 538968128, 'AS8HE1', 6, 0.0, 0.0, 1.0, 0.0, -1, -1, 1, 1, []),
    's8he2_1': ('food', 281, 538968128, 'AS8HE2', 1, 0.0, 0.0, 1.0, 0.0, -1, -1, 1, 1, []),
    's8he2_2': ('food', 281, 538968128, 'AS8HE2', 2, 0.0, 0.0, 1.0, 0.0, -1, -1, 1, 1, []),
    's8he2_3': ('food', 281, 538968128, 'AS8HE2', 3, 0.0, 0.0, 1.0, 0.0, -1, -1, 1, 1, []),
    's8he2_4': ('food', 281, 538968128, 'AS8HE2', 4, 0.0, 0.0, 1.0, 0.0, -1, -1, 1, 1, []),
    's8p_1': ('food', 324, 536870977, None, 0, 34.61538461538462, 0.547945205479452, 2.0, 2.0, 86400, 172800, 345600, 340, []),
    's8sg_1': ('food', 33, 536879168, 'AS8SG', 1, 13.846153846153847, 0.0, 2.0, 2.0, 86400, 172800, 432000, 1, []),
    's8sg_2': ('food', 33, 536879168, 'AS8SG', 2, 13.846153846153847, 0.0, 2.0, 2.0, 86400, 172800, 432000, 1, []),
    's8sg_3': ('food', 33, 536879168, 'AS8SG', 3, 13.846153846153847, 0.0, 2.0, 2.0, 86400, 172800, 432000, 1, []),
    's8sg_4': ('food', 33, 536879168, 'AS8SG', 4, 13.846153846153847, 0.0, 2.0, 2.0, 86400, 172800, 432000, 1, []),
    's8sg_5': ('food', 33, 536879168, 'AS8SG', 5, 13.846153846153847, 0.0, 2.0, 2.0, 86400, 172800, 432000, 1, []),
    's8sg_6': ('food', 33, 536879168, 'AS8SG', 6, 13.846153846153847, 0.0, 2.0, 2.0, 86400, 172800, 432000, 1, []),
    's9av_1': ('food', 36, 536870976, 'AS9AV', 1, 0.0, 0.0, 2.0, 0.0, 86400, 172800, 345600, 290, []),
    's9av_2': ('food', 36, 536870976, 'AS9AV', 2, 0.0, 0.0, 2.0, 0.0, 86400, 172800, 345600, 290, []),
    's9av_3': ('food', 36, 536870976, 'AS9AV', 3, 0.0, 0.0, 2.0, 0.0, 86400, 172800, 345600, 290, []),
    's9av_4': ('food', 36, 536870976, 'AS9AV', 4, 0.0, 0.0, 2.0, 0.0, 86400, 172800, 345600, 290, []),
    's9av_5': ('food', 36, 536870976, 'AS9AV', 5, 0.0, 0.0, 2.0, 0.0, 86400, 172800, 345600, 290, []),
    's9av_6': ('food', 36, 536870976, 'AS9AV', 6, 0.0, 0.0, 2.0, 0.0, 86400, 172800, 345600, 290, []),
    's9p_1': ('food', 327, 536870977, None, 0, 34.61538461538462, 0.547945205479452, 2.0, 2.0, 86400, 172800, 345600, 340, []),
    's9p_2': ('food', 327, 536870977, None, 0, 34.61538461538462, 0.547945205479452, 2.0, 2.0, 86400, 172800, 345600, 340, []),
    's9pe_1': ('food', 35, 536879168, 'AS9PE', 1, 0.0, 0.0, 2.0, 0.0, 86400, 172800, 518400, 230, []),
    's9pe_2': ('food', 35, 536879168, 'AS9PE', 2, 0.0, 0.0, 2.0, 0.0, 86400, 172800, 518400, 230, []),
    'savmap': ('other', 266, 268435456, []),
    'sb_blow': ('other', 228, 1024, []),
    'sb_pick': ('other', 305, 263168, []),
    'sleep1': ('consumable', 223, 24, 110, 5, 0.2, 0.0, []),
    'sleep2': ('consumable', 223, 24, 110, 5, 0.2, 0.0, []),
    'sleep3': ('consumable', 223, 24, 110, 5, 0.2, 0.0, []),
    'sleep4': ('consumable', 223, 24, 110, 5, 0.2, 0.0, []),
    'sleep5': ('consumable', 223, 24, 110, 5, 0.2, 0.0, []),
    'statue': ('other', 309, 0, [(24, 4, 512, 'Set')]),
    'survibag': ('other', 254, 0, []),
    'tblue': ('other', 268, 0, []),
    'term_1': ('food', 229, 536870976, None, 0, 2.307692307692308, 0.2191780821917808, 0.0, 1.0, 172800, 190080, 43200, 1, []),
    'term_10': ('food', 229, 536870976, None, 0, 2.307692307692308, 0.2191780821917808, 0.0, 1.0, 172800, 190080, 43200, 1, []),
    'term_2': ('food', 229, 536870976, None, 0, 2.307692307692308, 0.2191780821917808, 0.0, 1.0, 172800, 190080, 43200, 1, []),
    'term_3': ('food', 229, 536870976, None, 0, 2.307692307692308, 0.2191780821917808, 0.0, 1.0, 172800, 190080, 43200, 1, []),
    'term_4': ('food', 229, 536870976, None, 0, 2.307692307692308, 0.2191780821917808, 0.0, 1.0, 172800, 190080, 43200, 1, []),
    'term_5': ('food', 229, 536870976, None, 0, 2.307692307692308, 0.2191780821917808, 0.0, 1.0, 172800, 190080, 43200, 1, []),
    'term_6': ('food', 229, 536870976, None, 0, 2.307692307692308, 0.2191780821917808, 0.0, 1.0, 172800, 190080, 43200, 1, []),
    'term_7': ('food', 229, 536870976, None, 0, 2.307692307692308, 0.2191780821917808, 0.0, 1.0, 172800, 190080, 43200, 1, []),
    'term_8': ('food', 229, 536870976, None, 0, 2.307692307692308, 0.2191780821917808, 0.0, 1.0, 172800, 190080, 43200, 1, []),
    'term_9': ('food', 229, 536870976, None, 0, 2.307692307692308, 0.2191780821917808, 0.0, 1.0, 172800, 190080, 43200, 1, []),
    'torch': ('other', 233, 33556480, []),
    'tracker': ('other', 258, 256, []),
    'video1': ('other', 234, 512, [(164, 4, 256, 'Set')]),
    'video2': ('other', 235, 512, []),
    'video3': ('other', 236, 512, [(164, 4, 1024, 'Set')]),
    'video4': ('other', 237, 512, []),
    'video5': ('other', 238, 512, []),
    'video6': ('other', 239, 512, []),
    'vit1': ('consumable', 226, 24, 120, 5, 0.0, 0.0, []),
    'vit2': ('consumable', 226, 24, 120, 5, 0.0, 0.0, []),
    'vit3': ('consumable', 226, 24, 120, 5, 0.0, 0.0, []),
    'vit4': ('consumable', 226, 24, 120, 5, 0.0, 0.0, []),
    'vit5': ('consumable', 226, 24, 120, 5, 0.0, 0.0, []),
    'wand': ('other', 248, 0, [(24, 4, 2, 'Set')]),
    'wifelet': ('other', 271, 512, []),
}


# ------------------------------------------------------------------------------------------------ Self members
# (name, C++ type, offset) in the order of src/game/self.h
SELF_TYPES = {}
for _n, _t, _o in SELF_MEMBERS:
    SELF_TYPES[_n] = _t

CEL_KEYS = {'airTemp': 0x38, 'humidity': 0x48, 'oxygen': 0x50, 'co2': 0x58, 'soilWater': 0x70, 'wind': 0x80,
            'pollution': 0x98, 'plantHealth': 0xf0, 'flags': 0x108}
CEL_MEMBERS = {'m_dAirCapacity': 0x20, 'm_dAirVolume': 0x28, 'm_dAirTemp': 0x38, 'm_dCO2': 0x58, 'm_dHumidity': 0x48,
               'm_dLight': 0x60, 'm_dLightLetThru': 0x88, 'm_dOxygen': 0x50, 'm_dPollution': 0x98, 'm_dSoilNutrients': 0x90,
               'm_dSoilpH': 0x68, 'm_dSoilTemp': 0x40, 'm_dSoilWater': 0x70, 'm_dwTimeLastRain': 0x78, 'm_dWindSpeed': 0x80,
               'm_dPlantHealthAvg': 0xf0, 'm_dPlantHealthMin': 0xf8, 'm_dPlantHealthMax': 0x100, 'm_dwFlags': 0x108}

# slot key -> member (the saved game names the NamedRef members differently)
SLOT_NAMES = {'m_pSite->name': 'm_pSite', 'previousSite->name': 'm_pPreviousSite', 'lastEatenPlant->name': 'm_pLastEatenPlant',
              'm_pPlant->name': 'm_pPlant', 'm_pFeature->name': 'm_pFeature', 'm_pObjHolding->name': 'm_pObjHolding',
              'm_pAirMask->name': 'm_pAirMask', 'm_pGoggles->name': 'm_pGoggles'}

# values the constructor CSelf::CSelf dll:10063ca0 gives (the members the slot does not carry keep these)
CTOR = {'m_dHydration': 40.0, 'm_dHydrationShown': 40.0, 'm_dNutrition': 75.0, 'm_dNutritionShown': 75.0, 'm_dProtein': 100.0,
        'm_dTemperature': 50.0, 'm_dTemperatureShown': 50.0, 'm_dIdealTemp': 50.0, 'm_dMaxTemp': 73.0, 'm_dHealth': 90.0,
        'm_dHealthShown': 90.0, 'm_dEnergy': 63.0, 'm_dEnergyShown': 63.0, 'm_wHydrationState': 3, 'm_wNutritionState': 3,
        'm_wTempState': 3, 'm_wHealthState': 3, 'm_wEnergyState': 3, 'm_ulCompassHeading': 1, 'm_dwLastWokeUp': 0x7080,
        'm_wWakeReason': 1, 'm_dwDreamsSeen': 0x80000000, 'm_dwLastAte': 1, 'm_wLastDrinkType': 1, 'm_dwTimeLastUpdate': 0x7080,
        'm_lHeartBeatVol': -10000, 'm_dwUnk2a8': 1, 'm_bAwake': 1, 'm_lTopBarHandle': 0x29a, 'm_hDrunkVoice': -1,
        'm_hUnk730': -1, 'm_hBreathingLoop': -1, 'm_hUnk73c': -1, 'm_hUnk740': -1, 'm_hUnk744': -1, 'm_hUnk748': -1,
        'm_hUnk74c': -1, 'm_hHeartBeatLoop': -1, 'm_szLocation': 'R4'}


class SelfState:
    pass


def new_self():
    S = SelfState()
    for n, t, o in SELF_MEMBERS:
        if t == 'double':
            v = 0.0
        elif t in ('NamedRef',) or t.startswith('std::array<u8'):
            v = ''
        elif t in ('Item*', 'const Biome*'):
            v = None
        elif t.startswith('std::array<i32'):
            v = [0] * 24
        else:
            v = 0
        setattr(S, n, v)
    for n, v in CTOR.items():
        setattr(S, n, v)
    S.sleep_gain = 0.0          # +0x108: energy gained by the last sleep (a hidden member)
    return S


def wrap_member(name, v):
    t = SELF_TYPES[name]
    if t == 'double':
        return float(v)
    if t == 'u8':
        return int(v) & 0xff
    if t == 'u16':
        return int(v) & 0xffff
    if t == 'u32':
        return int(v) & U32
    if t == 'i32':
        return i32(int(v))
    raise SystemExit('cannot assign %s' % name)


def cue_index(off):
    return (off - 0x2c0) // 4


# ------------------------------------------------------------------------------------------------ world objects
class Obj:
    """a game object: the GameObj common block of gameobj.h plus the members of its class (obj_*.h)"""
    def __init__(self, name, spec):
        self.name = name
        self.cls = spec[0]
        self.id = spec[1]
        self.flags = spec[2] & U32
        self.pickup_event = 0
        self.unk1c = 0
        self.weight = 0.0
        self.timestamp = 0x7080
        self.in_scene = 1
        self.held = 0
        self.variant = ''
        if self.cls == 'food':
            (_, _, _, self.plant, self.slot, self.nut, self.prot, self.weight_static, self.water, self.a70, self.a74,
             self.regrow, self.code, pokes) = spec
            self.weight = float(self.weight_static)
            self.state = 2          # CFood ctor: state 2 (1 when the flags have 0x8000)
            self.time_picked = 0
            if (self.flags >> 8) & 0x80:
                self.state = 1
                self.time_picked = 0x7080
            self.nut = float(self.nut)
            self.prot = float(self.prot)
            self.water = float(self.water)
        elif self.cls == 'goggles':
            self.charge = 0.0
            self.power = 0
            self.shown = 0
            self.on = 0
            pokes = spec[3]
        elif self.cls == 'mask':
            self.filter = 0.98
            pokes = spec[3]
        elif self.cls == 'cyl':
            self.fx = spec[3]
            self.fx_site = spec[4]
            self.contents = 0.0
            self.valve = 0
            self.unk44 = 0
            self.unk54 = 0
            pokes = spec[5]
        elif self.cls == 'consumable':
            self.code = spec[3]
            self.maxdoses = spec[4]
            self.wfull = spec[5]
            self.wempty = spec[6]
            self.weight = float(spec[5])
            pokes = spec[7]
        elif self.cls == 'single':
            self.code = spec[3]
            pokes = spec[4]
        else:
            pokes = spec[3]
        for off, nb, val, op in pokes:
            if off == 0x14:
                self.flags = (self.flags | val) & U32 if op == 'Or' else val & U32
            elif off == 0x18:
                self.pickup_event = val & U32
        self.torch_cyl = ''         # CTorch +0x44 (the cylinder it burns), only used by "torch"


def parse_value(text):
    text = text.strip()
    if text.startswith("'"):
        return ast.literal_eval(text)
    if text.startswith('0x'):
        return int(text, 16)
    try:
        return int(text)
    except ValueError:
        return float(text)


def load_dump(path):
    out = subprocess.run(['python3', '-I', SCRIPTS + '/biosys_savblob.py', 'dump', path], capture_output=True, text=True,
                         check=True).stdout
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


def lower_in(d, name):
    for k in d:
        if k.lower() == name.lower():
            return k
    return None


# ------------------------------------------------------------------------------------------------ the simulation
class Sim:
    def __init__(self, sav, seed):
        self.rng = Rng(seed)
        self.tick = 0
        self.S = new_self()
        self.cels = {}
        self.sites = {}                       # name -> {'flags': int, 'f8': 0.0}
        self.plants = {}                      # name -> {'health', 'count', 'mask'}
        self.objs = {}
        self.milestones = 0
        self.time = 28800
        self.tod = 28800
        self.ticks = 0x28                     # CTimeline +0x0c  (CTimeline ctor 0x28; the saved game says -1)
        self.last_tick = 0                    # CTimeline +0x28 (set by Load / Advance)
        self.fx_site = {}                     # FX key -> site name (+0x8c)
        self.G = {'dead': 0, 'DAT_100c42e4': 0, 'DAT_100c430c': 0, 'DAT_100c42e0': 0, 'DAT_100c42f8': 0,
                  'DAT_100c4300': 0.0, 'DAT_100c42f0': 0.0, 'DAT_100c42e8': 0.0, 'DAT_100c42d0': 0, 'DAT_1009f838': 0,
                  'DAT_100c2498': 0, 'DAT_100c27e4': 0, 'DAT_100c1c44': 0, 'DAT_100bd308': 0}
        # constants of a fresh game (not printed)
        self.modal = 0                        # DAT_100c275c
        self.cheats = 0                       # DAT_100c2718
        self.last_input_tick = 0              # DAT_1009f834
        self.msg_timer = 0                    # DAT_100bc7b0
        self.msg_timer2 = 0                   # DAT_100c4310
        self.hogweed_busy = 0                 # DAT_100c1f9c is m_pFeature (never set here)
        self.load(sav)

    # ---------------------------------------------------------------------------------------- loading
    def load(self, sav):
        rows = load_dump(sav)
        S = self.S
        items = {}
        for sec, item, member, val in rows:
            if sec == 'header' and member == 'milestones':
                self.milestones = parse_value(val)
            elif sec == 'timeline':
                if member == 'm_dwTime':
                    self.time = parse_value(val)
                elif member == 'm_dwTimeOfDay':
                    self.tod = parse_value(val)
                elif member == 'm_dwTicksPerGameSec':
                    self.ticks = i32(parse_value(val))
            elif sec == 'cels' and member in CEL_MEMBERS:
                c = self.cels.setdefault(item, {})
                v = parse_value(val)
                c[CEL_MEMBERS[member]] = v if CEL_MEMBERS[member] == 0x108 else (float(v) if member != 'm_dwTimeLastRain' else v)
            elif sec == 'sites' and member == 'm_dwFlags':
                self.sites.setdefault(item, {'f8': 0.0})['flags'] = parse_value(val)
            elif sec == 'plants' and member in ('m_dHealth', 'unk_10', 'unk_14'):
                p = self.plants.setdefault(item, {})
                p[{'m_dHealth': 'health', 'unk_10': 'count', 'unk_14': 'mask'}[member]] = parse_value(val)
            elif sec == 'self':
                if member == 'tag' or member.startswith('_pad'):
                    continue
                nm = SLOT_NAMES.get(member, member)
                if nm in SELF_TYPES:
                    v = parse_value(val)
                    if SELF_TYPES[nm] in ('NamedRef',) or SELF_TYPES[nm].startswith('std::array<u8'):
                        setattr(S, nm, v)
                    else:
                        setattr(S, nm, wrap_member(nm, v))
            elif sec == 'objects':
                if item.startswith('_unused') or '#' in item or member.startswith('_pad'):
                    continue
                items.setdefault(item, {})[member] = parse_value(val)
        # sites without a flags row (none in practice) are created on demand
        for name, o in items.items():
            if name not in OBJECTS:
                continue
            ob = Obj(name, OBJECTS[name])
            ob.variant = o.get('m_szHolderName', '')
            ob.in_scene = o.get('unk_2c', 1)
            ob.held = o.get('unk_30', 0)
            if 'm_dWeight' in o:
                ob.weight = float(o['m_dWeight'])
            ob.timestamp = o.get('m_dwTimeStamp', 0x7080)
            if ob.cls == 'food':
                ob.state = o.get('m_byState', ob.state)
                ob.time_picked = o.get('m_dwTimePicked', 0)
                ob.code = o.get('m_wConsumableCode', ob.code)
            elif ob.cls == 'goggles':
                ob.on = o.get('unk_4c', 0)
                ob.power = o.get('unk_48', 0)
                ob.charge = float(o.get('m_dCharge', 0.0))
            elif ob.cls == 'mask':
                ob.filter = float(o.get('m_dFilter', 0.98))
            elif ob.cls == 'cyl':
                ob.contents = float(o.get('m_dContents', 0.0))
                ob.unk54 = o.get('unk_54', 0)
                ob.valve = o.get('unk_50', 0)
            elif ob.cls == 'consumable':
                ob.code = o.get('m_wConsumableCode', ob.code)
            elif ob.cls == 'single':
                ob.code = o.get('m_wDosesLeft', ob.code)
            self.objs[name] = ob
        for ob in self.objs.values():
            if ob.cls == 'cyl':
                self.fx_site[ob.fx] = ob.fx_site
        # CSelf::Load: the pointer members are resolved from the saved names
        self.resolve_site(S.m_pSite)
        S.m_pPage = S.m_szLocation if S.m_szLocation else None
        self.last_tick = 0                    # CTimeline::Load: GetTickCount() == 0 here

    def resolve_site(self, name):
        S = self.S
        if not name:
            S.m_pCel = None
            S.m_pBiome = None
            return
        k = lower_in(SITES, name)
        if k is None:
            raise SystemExit('unknown site %s' % name)
        cel, biome = SITES[k]
        S.m_pCel = cel
        S.m_pBiome = biome
        if k not in self.sites:
            self.sites[k] = {'flags': 0, 'f8': 0.0}

    # ---------------------------------------------------------------------------------------- helpers
    def cel(self):
        return self.cels[self.S.m_pCel]

    def site(self):
        k = lower_in(self.sites, self.S.m_pSite)
        return self.sites[k]

    def find_object(self, name):
        for k, o in self.objs.items():
            if k.lower() == name.lower():
                return o
        return None

    def is_at_page(self, page):                              # 1006ef40: strcmpi(page, m_szLocation) == 0
        return page.lower() == self.S.m_szLocation.lower()

    def holding(self):
        return self.find_object(self.S.m_pObjHolding) if self.S.m_pObjHolding else None

    # ---------------------------------------------------------------------------------------- stat setters (10066730..10066a60)
    def AddHealth(self, v):                                  # 10066850
        S = self.S
        S.m_dHealth = S.m_dHealth + v
        S.m_dHealthDelta = S.m_dHealthDelta + v

    def AddNutrition(self, v):                               # 10066870
        S = self.S
        S.m_dNutrition = S.m_dNutrition + v
        S.m_dNutritionDelta = S.m_dNutritionDelta + v

    def AddProtein(self, v):                                 # 10066890
        S = self.S
        S.m_dProtein = S.m_dProtein + v
        S.m_dProteinDelta = S.m_dProteinDelta + v

    def AddHydration(self, v):                               # 100668b0
        S = self.S
        S.m_dHydration = S.m_dHydration + v
        S.m_dHydrationDelta = S.m_dHydrationDelta + v

    def AddEnergy(self, v):                                  # 100668d0
        S = self.S
        S.m_dEnergy = clamp(S.m_dEnergy + v, 0.0, 100.0)
        S.m_dEnergyDelta = S.m_dEnergyDelta + v

    def AddTemperature(self, v):                             # 10066910
        S = self.S
        S.m_dTemperature = S.m_dTemperature + v
        if S.m_dMaxTemp < S.m_dTemperature and 40.0 < S.m_dHydration:
            S.m_dTemperature = S.m_dMaxTemp
        S.m_dTemperatureDelta = S.m_dTemperatureDelta + v

    def LoseHealth(self, v, cause, die_now):                 # 10066950
        S = self.S
        S.m_dHealth = S.m_dHealth - v
        S.m_dHealthDelta = S.m_dHealthDelta - v
        if cause != 0 and S.m_dLargestHealthLoss < v:
            S.m_wCauseOfDeath = cause
            S.m_dLargestHealthLoss = v
        if S.m_dHealth <= 0.0:
            if die_now:
                self.Die(0)
                return
            S.m_bDieOnUpdate = 1

    def LoseNutrition(self, v):                              # 100669d0
        S = self.S
        S.m_dNutrition = S.m_dNutrition - v
        S.m_dNutritionDelta = S.m_dNutritionDelta - v

    def LoseProtein(self, v):                                # 100669f0
        S = self.S
        S.m_dProtein = S.m_dProtein - v
        S.m_dProteinDelta = S.m_dProteinDelta - v

    def LoseHydration(self, v):                              # 10066a10
        S = self.S
        S.m_dHydration = S.m_dHydration - v
        S.m_dHydrationDelta = S.m_dHydrationDelta - v

    def LoseEnergy(self, v):                                 # 10066a30
        S = self.S
        S.m_dEnergy = S.m_dEnergy - v
        S.m_dEnergyDelta = S.m_dEnergyDelta - v

    def LoseTemperature(self, v):                            # 10066a60
        S = self.S
        S.m_dTemperature = S.m_dTemperature - v
        if S.m_dMaxTemp < S.m_dTemperature and 40.0 < S.m_dHydration:
            S.m_dTemperature = S.m_dMaxTemp
        S.m_dTemperatureDelta = S.m_dTemperatureDelta - v

    def ResetDeltas(self):                                   # 10066820
        S = self.S
        S.m_dHydrationDelta = 0.0
        S.m_dNutritionDelta = 0.0
        S.m_dProteinDelta = 0.0
        S.m_dTemperatureDelta = 0.0
        S.m_dHealthDelta = 0.0
        S.m_dEnergyDelta = 0.0

    def GetMaxHealth(self):                                  # 1006f1c0: 100 + sum of -20/n for the set bits of symptoms & 0xba7c
        n = 0
        v = 100.0
        for bit in range(32):
            if (1 << bit) & self.S.m_dwSymptoms & 0xba7c:
                n += 1
                v = v + (-20.0) / n
        return v

    def Die(self, cause):                                    # 10068f10 (state part)
        S = self.S
        if self.modal == 0 and S.m_pPath is None:
            if cause != 0:
                S.m_wCauseOfDeath = cause & 0xffff
            self.G['dead'] = 1

    def CatchMalaria(self):                                  # 1006df90
        S = self.S
        if S.m_bImmuneToMalaria == 0 and S.m_dwMalaria == 0:
            S.m_dwMalaria = 1
            return 1
        return 0

    def AddWeight(self, w):                                  # 1006de50 (the strain sound is an engine effect)
        S = self.S
        S.m_dWeightCarried = S.m_dWeightCarried + w
        if 10.0 < w and S.m_dEnergy < S.m_dWeightCarried:
            self.snd()
        return 1

    def Drop(self, w):                                       # 1006dea0
        S = self.S
        S.m_dWeightCarried = S.m_dWeightCarried - w
        if S.m_dWeightCarried < 0.0:
            S.m_dWeightCarried = 0.0
        return 1

    def CanPickUp(self, w):                                  # 1006def0 (messages only)
        S = self.S
        if w <= 3.0:
            return 1
        if S.m_dEnergy < w:
            return 0
        if S.m_dEnergy - S.m_dWeightCarried < w:
            return 0
        return 1

    # ---------------------------------------------------------------------------------------- timeline (100154b0 ...)
    def advance_real_time(self):                             # CTimeline::AdvanceRealTime 100153b0
        el = u32(self.tick - self.last_tick)
        if el < 10000:
            self.time = i32(self.time + el // u32(self.ticks))
        self.last_tick = self.tick
        self.tod = self.time % 86400

    def set_ticks(self, v):                                  # CTimeline::SetTicksPerGameSec 10015ab0
        self.advance_real_time()
        self.ticks = i32(v)

    def Advance(self, secs, upd):                            # CTimeline::Advance 100154b0, world simulation left out
        if self.ticks != -1:
            n = (secs + 0x5a) // 0x5a
            for _ in range(n):
                self.UpdateAllObjects()
                if upd:
                    self.Update(0)
                self.time = i32(self.time + 0x5a)
                self.tod = self.time % 86400
            if secs < n * 0x5a:
                self.time = i32(self.time + secs - n * 0x5a)
                self.tod = self.time % 86400
        self.last_tick = self.tick

    # ---------------------------------------------------------------------------------------- CSelf::Update 10066aa0
    def Update(self, full):
        S = self.S
        now = self.time
        iVar5 = i32(now - S.m_dwTimeLastUpdate)
        S.m_dwTimeLastUpdate = now
        if iVar5 == 0:
            return
        if S.m_bAwake == 0:
            return
        if S.m_bInsideAngelSequence != 0:
            S.m_dwWhiteAngelTrip = i32(S.m_dwWhiteAngelTrip + S.m_dwUpdateDt)
            if full == 0:
                return
            if u32(S.m_dwWhiteAngelTrip) < 0x438:
                return
            S.m_bInsideAngelSequence = 0      # JumpToPage(m_szReturnPage) is an engine effect
            return
        if S.m_bInsidePeyoteSequence != 0:
            S.m_dwPeyoteTripTimer = i32(S.m_dwUpdateDt + S.m_dwPeyoteTripTimer)
            if full == 0:
                return
            if u32(S.m_dwPeyoteTripTimer) < 0x2d0:
                return
            S.m_bInsidePeyoteSequence = 0
            return
        S.m_dwUpdateDt = iVar5
        udt = u32(iVar5)
        dtd = float(udt)
        tick = self.tick
        if S.m_pCel is None:
            return
        cel = self.cel()
        airtemp = cel[0x38]
        S.m_wCauseOfDeath = 0
        S.m_dLargestHealthLoss = 0.0
        self.ResetDeltas()
        S.m_bImmuneToMalaria = 0
        S.m_aSoundCue = [0] * 24
        S.m_bAbleToSleep = 1
        S.m_wBreathing &= 0xff
        if u32(now - S.m_dwLastWokeUp) > 0x15180:
            self.LoseEnergy(dtd * 0.004166666666666667)
        self.LoseNutrition(dtd * 0.001736111111111111)
        if S.m_dNutrition < 45.5:
            self.LoseEnergy(dtd * 0.00034722222222222224)
        if S.m_dNutrition < 20.0:
            self.LoseHealth(dtd * 0.0002314814814814815, 4, 0)
        self.LoseProtein(9.259259259259259e-05)
        if S.m_dProtein < 20.0:
            self.LoseHealth(dtd * 3.472222222222222e-05, 0xb, 0)
        self.LoseHydration(dtd * 0.0011574074074074073)
        if 20.0 < airtemp:
            self.LoseHydration((dtd * 0.00011574074074074075) * (airtemp - 20.0))
        if S.m_dHydration < 20.0:
            S.m_wBreathing |= 0x200
            self.LoseHealth(dtd * 0.0005787037037037037, 0xc, 0)
        c9 = self.cels['S9']                                  # DAT_100bcff0 = FindBiomeCel("S9")
        if (not (S.m_dHydration < 20.0)) or S.m_dTemperature <= 65.0 or (c9[0x108] & 0x10) != 0 or c9[0xf0] <= 0.2:
            if S.m_dwUnk2bc != 0:
                S.m_dwUnk2bc = 0                              # CWorld::SetFXState(10000, 0)
        elif S.m_dwUnk2bc == 0:
            S.m_dwUnk2bc = 1                                  # CWorld::SetFXState(10000, 1)
        dVar3 = airtemp
        if S.m_bWearingPoncho != 0:
            dVar3 = dVar3 + 15.0
        if (cel[0x108] & 2) != 0:
            dVar3 = dVar3 - 10.0
        if S.m_pPath is not None:
            dVar3 = dVar3 + 10.0
        target = (self.site()['f8'] + dVar3) * 2.5
        local = airtemp                                        # stays the air temperature when the body is exactly at target
        if target < S.m_dTemperature:
            local = ((target - S.m_dTemperature) * dtd) * 0.00044444444444444447
            if S.m_dIdealTemp < S.m_dTemperature:
                local = ((S.m_dTemperature - S.m_dIdealTemp) * dtd) * -0.0017777777777777779 + local
        elif target > S.m_dTemperature:
            local = ((target - S.m_dTemperature) * dtd) * 0.00044444444444444447
            if S.m_dIdealTemp > S.m_dTemperature:
                local = ((S.m_dTemperature - S.m_dIdealTemp) * dtd) * -0.0017777777777777779 + local
        self.AddTemperature(local)
        if S.m_dTemperature > 73.0:
            self.LoseHealth(((S.m_dTemperature - 73.0) * dtd) * 0.0002777777777777778, 0xd, 0)
        elif S.m_dTemperature < 27.0:
            self.LoseHealth(((27.0 - S.m_dTemperature) * dtd) * 0.0002777777777777778, 1, 0)
        if (S.m_wBreathing & 1) == 0:
            if 50.0 < cel[0x98]:
                S.m_wBreathing |= 0x400
                if S.m_dwFumes == 0:
                    S.m_dwFumes = 1
            if cel[0x50] < 16.0:
                S.m_wBreathing |= 0x100
        if S.m_lHeartBeatVol == 0 and S.m_pPath is None:
            self.Die(0xf)
            return
        step = dtd * -0.00011574074074074075
        S.m_dStimulant = S.m_dStimulant + step
        if S.m_dStimulant < 0.0:
            S.m_dStimulant = 0.0
        S.m_dSedative = S.m_dSedative + step
        if S.m_dSedative <= 0.0:
            S.m_dSedative = 0.0
            S.m_dwSleepingPill = 0
        S.m_dSalt = S.m_dSalt + step
        if S.m_dSalt < 0.0:
            S.m_dSalt = 0.0
        self.AddHealth(dtd * 2.3148148148148147e-05)
        self.UpdateAilments()
        if S.m_szLocation.lower() == 'r4' and (self.milestones & 0x80) != 0 and 30.0 < cel[0x70] \
                and self.rng.chance(dtd * 0.013888888888888888):
            self.CatchMalaria()
        if (self.cheats & 1) != 0:
            S.m_dHealth = 0.0
            S.m_dEnergy = 0.0
            S.m_dTemperature = 0.0
            S.m_dHydration = 0.0
            S.m_dHealth = 100.0
            S.m_dEnergy = 100.0
            S.m_dHydration = 100.0
            S.m_dTemperature = 50.0
            S.m_dNutrition = 100.0
            S.m_dProtein = 100.0
            return
        S.m_dHealth = clamp(S.m_dHealth, 0.0, self.GetMaxHealth())
        cap = (((S.m_dEnergyGainCoca + S.m_dEnergyGainGuarana) + S.m_dEnergyGainCoffee) + S.m_dEnergyGainAdrenaline) + S.m_dHealth
        S.m_dEnergy = clamp(S.m_dEnergy, 0.0, cap)
        S.m_dEnergy = clamp(S.m_dEnergy, 0.0, 100.0)
        S.m_dTemperature = clamp(S.m_dTemperature, 0.0, 100.0)
        S.m_dNutrition = clamp(S.m_dNutrition, 0.0, 100.0)
        S.m_dHydration = clamp(S.m_dHydration, 0.0, 100.0)
        if S.m_dHealth == 0.0 and S.m_pPath is None:
            self.Die(0)
            return
        cue = S.m_aSoundCue
        if S.m_dHealth < 10.0:
            cue[cue_index(0x2c0)] = 1
        if S.m_dHealth < 30.0:
            cue[cue_index(0x2c4)] = 1
        if 80.0 < S.m_dEnergy:
            S.m_bAbleToSleep = 0
        if S.m_dEnergy <= 0.0 and full != 0 and self.last_input_tick != 0 and u32(self.last_input_tick + 1000) < tick \
                and S.m_pPath is None:
            self.Sleep(1, 1, 1)
        else:
            if S.m_dEnergy < 22.5:
                cue[cue_index(0x2c8)] = 1
                if S.m_dEnergy < 10.0:
                    S.m_wBreathing |= 4
                    cue[cue_index(0x2cc)] = 1
            if S.m_dNutrition < 80.0:
                cue[cue_index(0x2d0)] = 1
            if S.m_dHydration < 40.0:
                cue[cue_index(0x2d4)] = 1
            if S.m_dTemperature > 73.0:
                cue[cue_index(0x2f8)] = 1
            elif S.m_dTemperature > 65.0:
                cue[cue_index(0x2d8)] = 1
            elif S.m_dTemperature < 30.0:
                cue[cue_index(0x300)] = 1
            elif S.m_dTemperature < 40.0:
                cue[cue_index(0x2dc)] = 1
        if full != 0:
            self.UpdateLoopSounds()
            self.PlaySymptomSounds()
            # CSelf::ShowMessage(0,0,0,0): message output only

    # ---------------------------------------------------------------------------------------- CSelf::UpdateAilments 10067c80
    def UpdateAilments(self):
        S = self.S
        udt = u32(S.m_dwUpdateDt)
        dtd = float(udt)
        now = self.time
        cel = self.cel()
        cue = S.m_aSoundCue
        S.m_dwSymptoms = S.m_dwSymptoms & 0xfffffefe
        if (S.m_wBreathing & 1) == 0:
            o2 = cel[0x50]
            if o2 < 14.0:
                S.m_dwSymptoms |= 1
                self.LoseHealth(((14.0 - o2) * dtd) * 0.002777777777777778, 0x12, 0)
            elif o2 < 16.0:
                S.m_dwSymptoms |= 0x100
        if S.m_dTemperature <= 73.0:
            S.m_dwSymptoms &= 0xffffff7f
        else:
            S.m_dwSymptoms |= 0x80
        if S.m_dwSpiderBiteTimer != 0:                                      # 0x144
            S.m_dwSpiderBiteTimer = i32(S.m_dwSpiderBiteTimer + udt)
            self.LoseHealth(dtd * 0.001388888888888889, 0x11, 0)
            S.m_dwSymptoms |= 4
            S.m_dwSymptomsCured &= 0xfffffffb
        if S.m_dwHogweedBurnTimer != 0:                                     # 0x148
            t = u32(S.m_dwHogweedBurnTimer + udt)
            S.m_dwHogweedBurnTimer = i32(t)
            if t <= 0x15180:
                cue[cue_index(0x318)] = 1
                S.m_dwSymptoms |= 0x800
                S.m_dwSymptomsCured &= 0xfffff7ff
            else:
                S.m_dwHogweedBurnTimer = 0
                S.m_dwSymptoms &= 0xfffff7ff
        fumes = u32(S.m_dwFumes)                                            # 0x14c
        if fumes != 0:
            if (S.m_wBreathing & 1) != 0 or cel[0x98] < 50.0:
                S.m_dwSymptomsCured &= 0xffff7ff7
                S.m_dwSymptoms = (S.m_dwSymptoms & 0xfffffff7) | 0x8000
                if u32(udt + 1) < fumes:
                    S.m_dwFumes = i32(fumes - udt)
                else:
                    S.m_dwFumes = 1
            else:
                fumes = u32(fumes + udt)
                S.m_dwFumes = i32(fumes)
                if fumes != 0:
                    cue[cue_index(0x2e8)] = 1
                    S.m_dwSymptomsCured &= 0xffff7ff7
                    S.m_dwSymptoms = (S.m_dwSymptoms & 0xffff7fff) | 8
                    if fumes > 0xe10:
                        cue[cue_index(0x31c)] = 1
                        cue[cue_index(0x2ec)] = 1
                        if fumes > 0x2a30:
                            cue[cue_index(0x2f0)] = 1
                        if 50.0 < cel[0x98]:
                            self.LoseHealth(dtd * 0.001388888888888889, 7, 0)
        if S.m_dwCoffee != 0:                                               # 0x1e0
            t = u32(S.m_dwCoffee + udt)
            S.m_dwCoffee = i32(t)
            td = float(t)
            if td > 7200.0:
                S.m_dwCoffee = 0
                S.m_dEnergyGainCoffee = 0.0
                S.m_byDosesCoffee = 0
                S.m_lHeartBeatVol = -10000
            elif td > 3600.0:
                S.m_lHeartBeatVol = hb_volume(td, 3600.0, -0.2777777777777778, 2000.0)
                self.LoseEnergy(float(S.m_byDosesCoffee * udt) * 0.002777777777777778)
        if S.m_dwGuarana != 0:                                              # 0x1f4
            t = u32(S.m_dwGuarana + udt)
            S.m_dwGuarana = i32(t)
            td = float(t)
            if td > 23400.0:
                S.m_byDosesGuarana = 0
                S.m_dwGuarana = 0
                S.m_lHeartBeatVol = -10000
                S.m_dEnergyGainGuarana = 0.0
            elif td > 16200.0:
                S.m_lHeartBeatVol = hb_volume(td, 16200.0, -0.20833333333333334, 1500.0)
                self.LoseEnergy(0.0)
        if S.m_dwCoca != 0:                                                 # 0x204
            t = u32(S.m_dwCoca + udt)
            S.m_dwCoca = i32(t)
            td = float(t)
            if td > 30600.0:
                S.m_dwCoca = 0
                S.m_byDosesCoca = 0
                S.m_lHeartBeatVol = -10000
                S.m_dEnergyGainCoca = 0.0
            elif td > 23400.0:
                S.m_lHeartBeatVol = hb_volume(td, 23400.0, -0.2777777777777778, 1000.0)
                self.LoseEnergy(float(S.m_byDosesCoca * udt) * 0.006944444444444444)
                self.LoseNutrition(dtd * 0.002777777777777778)
        if S.m_dwAdrenaline != 0:                                           # 0x214
            t = u32(S.m_dwAdrenaline + udt)
            S.m_dwAdrenaline = i32(t)
            if u32(S.m_dwSleepingPill) > 1:
                S.m_dwSleepingPill = 1
                S.m_wSleepingPillsPopped = 0
            if t > 0x1c20:
                S.m_lHeartBeatVol = -10000
                S.m_dwAdrenaline = 0
                S.m_dEnergyGainAdrenaline = 0.0
            else:
                td = float(t)
                if td > 3600.0:
                    S.m_lHeartBeatVol = hb_volume(td, 3600.0, -0.825, 30.0)
                    self.LoseEnergy(dtd * 0.009722222222222222)
        if S.m_dwCinchona != 0:                                             # 0x164
            t = u32(S.m_dwCinchona + udt)
            S.m_dwCinchona = i32(t)
            if t <= 0x3f480:
                S.m_bImmuneToMalaria = 1
                if S.m_dwMalaria != 0:
                    S.m_dwMalaria = 0
                    self.snd()
                    self.AddHealth(S.m_dMalariaHealthLoss * 0.5)
                    S.m_dMalariaHealthLoss = 0.0
            else:
                S.m_dwCinchona = 0
        if S.m_dwChloroquine != 0:                                          # 0x160
            t = u32(S.m_dwChloroquine + udt)
            S.m_dwChloroquine = i32(t)
            if t <= 0x15180:
                S.m_bImmuneToMalaria = 1
                if S.m_dwMalaria != 0:
                    S.m_dwMalaria = 0
                    self.snd()
                    self.AddHealth(S.m_dMalariaHealthLoss * 0.5)
                    S.m_dMalariaHealthLoss = 0.0
            else:
                S.m_dwChloroquine = 0
        if S.m_dwMalaria != 0:                                              # 0x150
            t = u32(S.m_dwMalaria + udt)
            S.m_dwMalaria = i32(t)
            if t <= 0x15180 or S.m_bImmuneToMalaria != 0:
                S.m_dwSymptoms &= 0xfffffffd
                S.m_dMaxTemp = 73.0
                S.m_dIdealTemp = 50.0
            else:
                S.m_dMaxTemp = 73.0
                S.m_dIdealTemp = 73.0
                self.LoseHealth(dtd * 0.00011574074074074075, 2, 0)
                S.m_dwSymptomsCured &= 0xfffffffd
                S.m_dwSymptoms |= 2
                cue[cue_index(0x304)] = 1
        if S.m_dwChampagne != 0:                                            # 0x1c0
            t = u32(S.m_dwChampagne + udt)
            S.m_dwChampagne = i32(t)
            td = float(t)
            if not (td < 3600.0):
                if td < 36000.0:
                    if S.m_dwSymptoms & 0x40000000:
                        S.m_dwSymptoms &= 0xbfffffff
                    S.m_dwSymptomsCured &= 0xfffffdff
                    S.m_dwSymptoms |= 0x200
                    cue[cue_index(0x2e4)] = 1
                    S.m_dHydration = dtd * -0.001388888888888889 + S.m_dHydration
                else:
                    S.m_dwChampagne = 0
                    S.m_dwSymptoms &= 0xbffffdff
        if S.m_dwMescal != 0:                                               # 0x1c8
            t = u32(S.m_dwMescal + udt)
            S.m_dwMescal = i32(t)
            td = float(t)
            if not (td < 3600.0):
                if td < 36000.0:
                    if S.m_dwSymptoms & 0x40000000:
                        S.m_dwSymptoms &= 0xbfffffff               # CWorld::SetFXState(-0x3cb0, 0)
                    S.m_dwSymptomsCured &= 0xfffffdff
                    S.m_dwSymptoms |= 0x200
                    cue[cue_index(0x2e4)] = 1
                    S.m_dHydration = dtd * -0.001388888888888889 + S.m_dHydration
                else:
                    S.m_dwSymptoms &= 0xbffffdff
                    S.m_dwMescal = 0
        if S.m_dwSynthAttack != 0:                                          # 0x1a0
            t = u32(S.m_dwSynthAttack + udt)
            S.m_dwSynthAttack = i32(t)
            if t <= 0x7080:
                S.m_dwSymptomsCured &= 0xffffbfff
                S.m_dwSymptoms |= 0x4000
            else:
                S.m_dwSynthAttack = 0
                S.m_dSynthAttackHealthLoss = 0.0
                S.m_dwSymptoms &= 0xffffbfff
        if S.m_dwDioscorin != 0:                                            # 0x190
            t = u32(S.m_dwDioscorin + udt)
            S.m_dwDioscorin = i32(t)
            if t > 0x15180:
                S.m_dwDioscorin = 0
                S.m_dDioscorinHealthLoss = 0.0
                S.m_dwSymptoms &= 0xffffffbf
            elif t > 2:
                S.m_dwSymptomsCured &= 0xffffffbf
                S.m_dwSymptoms |= 0x40
                if t < 0x259 or u32(now - S.m_dwLastBarfed) < 0x15181:
                    cue[cue_index(0x2e4)] = 1
                else:
                    cue[cue_index(0x2e0)] = 1
                loss = dtd * 0.0005555555555555556
                self.LoseHealth(loss, 0xe, 0)
                S.m_dDioscorinHealthLoss = S.m_dDioscorinHealthLoss + loss
        if S.m_dwPrussic != 0:                                              # 0x1b0
            t = u32(S.m_dwPrussic + udt)
            S.m_dwPrussic = i32(t)
            if t > 0x15180:
                S.m_dwPrussic = 0
                S.m_dPrussicHealthLoss = 0.0
                S.m_dwSymptoms &= 0xffffffdf
            elif t > 2:
                S.m_dwSymptomsCured &= 0xffffffdf
                S.m_dwSymptoms |= 0x20
                if t < 0x259 or u32(now - S.m_dwLastBarfed) < 0x15181:
                    cue[cue_index(0x2e4)] = 1
                else:
                    cue[cue_index(0x2e0)] = 1
                loss = dtd * 0.0005555555555555556
                self.LoseHealth(loss, 5, 0)
                S.m_dPrussicHealthLoss = S.m_dPrussicHealthLoss + loss
        S.m_dwSymptoms &= 0xffffefff
        if S.m_dwWhiteAngelTrip != 0:                                       # 0x1d0
            S.m_dwSymptoms |= 0x1000
            S.m_dwWhiteAngelTrip = i32(udt + S.m_dwWhiteAngelTrip)
        if S.m_dwPeyoteTripTimer != 0:                                      # 0x1d8
            S.m_dwSymptoms |= 0x1000
            S.m_dwPeyoteTripTimer = i32(udt + S.m_dwPeyoteTripTimer)
        if S.m_dwIndigestionTimer != 0:                                     # 0x178
            t = u32(S.m_dwIndigestionTimer + udt)
            S.m_dwSymptoms |= 0x10000
            S.m_dwIndigestionTimer = i32(t)
            cue[cue_index(0x2e0)] = 1
            if t > 0x2a30:
                S.m_dwIndigestionTimer = 0
                S.m_dwSymptoms &= 0xfffeffff
        if S.m_dwCactusPoisonTimer != 0:                                    # 0x17c
            cue[cue_index(0x2e0)] = 1
            t = u32(S.m_dwCactusPoisonTimer + udt)
            S.m_dwCactusPoisonTimer = i32(t)
            if t > 0x2a30:
                S.m_dwCactusPoisonTimer = 0
        if S.m_dwEphedrineTimer != 0:                                       # 0x180
            t = u32(S.m_dwEphedrineTimer + udt)
            S.m_dwEphedrineTimer = i32(t)
            if t > 0x1c20:
                S.m_dwEphedrineTimer = 0
            else:
                td = float(t)
                if td > 3600.0:
                    S.m_lHeartBeatVol = hb_volume(td, 3600.0, -0.825, 30.0)
        if S.m_dwWorms != 0:                                                # 0x184
            t = u32(S.m_dwWorms + udt)
            S.m_dwWorms = i32(t)
            if t > 0x15180:
                S.m_dwSymptoms |= 0x10
                cue[cue_index(0x2e0)] = 1
        if S.m_dSalt <= 50.0:
            S.m_dwSymptoms &= 0xffffdfff
        else:
            S.m_dwSymptoms |= 0x2000
        if u32(S.m_dwSleepingPill) > 1:
            S.m_dwSleepingPill = i32(udt + S.m_dwSleepingPill)

    # ---------------------------------------------------------------------------------------- sounds (state parts)
    def UpdateLoopSounds(self):                                             # 10068960
        S = self.S
        if S.m_dwWhiteAngelTrip != 0:
            self.WhiteAngelTrip()
        # UpdateBreathingSound / UpdateHeartBeatSound(0): sound handles only

    def Vomit(self):                                                        # 1006c9c0
        S = self.S
        if u32(S.m_dwLastAte) < u32(S.m_dwLastBarfed):
            self.Retch()
            return
        self.snd()
        S.m_dwLastBarfed = self.time
        self.LoseNutrition(10.0)
        self.LoseHydration(5.0)

    def snd(self):                                                          # FUN_10060820: random-wav sound, draws one rand
        self.rng.rand()

    def Retch(self):                                                        # 1006ca20
        self.snd()

    def PlaySymptomSounds(self):                                            # 100689a0
        S = self.S
        cue = S.m_aSoundCue
        rng = self.rng
        # m_pFeature is never set headless (the feature test at the top is false)
        if S.m_hDrunkVoice != -1:
            return
        c = lambda off: cue[cue_index(off)] != 0
        if c(0x2c0) and rng.chance(10.0):
            self.snd()
            return
        if c(0x2cc) and rng.chance(10.0):
            self.snd()
            return
        if c(0x2e0) and rng.chance(30.0):
            self.Vomit()
        elif c(0x2e4) and rng.chance(40.0):
            self.Retch()
        elif c(0x2e8) and rng.chance(30.0):
            self.snd()
        elif c(0x2ec) and rng.chance(20.0):
            self.snd()
        elif c(0x2f0) and rng.chance(10.0):
            self.snd()
        elif c(0x2dc) and rng.chance(60.0):
            self.snd()
        elif c(0x318) and rng.chance(30.0):
            # FUN_1006f090: the hogweed pain sound; DAT_100c1f9c is m_pFeature
            if not S.m_pFeature:
                rng.rand()
        elif c(0x30c) and rng.chance(20.0):
            pass
        elif c(0x310) and rng.chance(20.0):
            pass
        elif c(0x2c4) and rng.chance(20.0):
            self.snd()
        else:
            if c(0x314) and rng.chance(30.0):
                self.snd()
            if c(0x2c8) and rng.chance(30.0):
                self.snd()
            elif c(0x2d4) and rng.chance(30.0):
                self.snd()
                # FUN_1006cc80: help texts on and 5 s since the last one: a deferred message
                if (S.m_dwDreamsSeen >> 31) != 0 and 5000 < u32(self.tick - self.G['DAT_100bd308']):
                    self.G['DAT_100bd308'] = self.tick
            elif c(0x31c) and rng.chance(30.0):
                self.snd()
            elif ((self.milestones >> 24) & 0x20) != 0 and rng.chance(1.0):
                self.snd()
            elif c(0x300) and rng.chance(20.0):
                self.snd()
            elif c(0x2fc) and rng.chance(20.0):
                pass
            elif c(0x304) and rng.chance(20.0):
                self.snd()
            elif c(0x2f8) and rng.chance(20.0):
                self.snd()
            elif c(0x2d8) and rng.chance(30.0):
                self.snd()
        if c(0x308) and rng.chance(20.0):
            pass

    # ---------------------------------------------------------------------------------------- trips
    def WhiteAngelTrip(self):                                               # 1006dc20
        S = self.S
        t = u32(S.m_dwWhiteAngelTrip)
        if t > 0x7e90:
            S.m_dwWhiteAngelTrip = 0
            return
        if t > 0x437:
            S.m_aSoundCue[cue_index(0x314)] = 1
            return
        if t > 1 and (S.m_szLocation[:1] != 'E'):
            S.m_bInsideAngelSequence = 1
            S.m_bInsidePeyoteSequence = 0
            S.m_szReturnPage = self.S.m_pSite[:11]                          # copy of the site name (site+0xd4)
            S.m_dwUnk2a8 = 0

    def PeyoteTrip(self):                                                   # 1006dd30
        S = self.S
        t = u32(S.m_dwPeyoteTripTimer)
        if t > 0x7e90:
            S.m_dwPeyoteTripTimer = 0
            return
        if t > 0x2cf:
            S.m_aSoundCue[cue_index(0x314)] = 1
            return
        if t > 1 and (S.m_szLocation[:1] != 'E'):
            S.m_bInsidePeyoteSequence = 1
            S.m_bInsideAngelSequence = 0
            S.m_szReturnPage = self.S.m_pSite[:11]
            S.m_dwUnk2a8 = 0

    # ---------------------------------------------------------------------------------------- eating, drinking, drugs
    def Eat(self, nutrition, protein, hydration, code):                     # 10068d70
        S = self.S
        if 99.0 < S.m_dNutrition and nutrition != 0.0:
            return 0
        if code in (0x154, 0x15e):
            self.snd()
        self.AddNutrition(nutrition)
        S.m_dNutrition = clamp(S.m_dNutrition, 0.0, 100.0)
        self.AddProtein(protein)
        S.m_dProtein = clamp(S.m_dProtein, 0.0, 100.0)
        self.AddHydration(hydration)
        S.m_dHydration = clamp(S.m_dHydration, 0.0, 100.0)
        self.Consume(code)
        S.m_dwLastAte = self.time
        return 1

    def Drink(self, code):                                                  # 1006ccd0
        S = self.S
        if S.m_pAirMask:
            return 0
        if 99.0 < S.m_dHydration:
            return 0
        if code == 0xc3:
            self.snd()
        if code in (0xbe, 0xc3, 200):
            if code == 200 and S.m_dwWorms == 0:
                S.m_dwWorms = 1
            self.AddHydration(20.0)
            if 50.0 < S.m_dTemperature:
                self.LoseTemperature(10.0)
            if S.m_dMaxTemp < S.m_dTemperature:
                S.m_dTemperature = S.m_dMaxTemp
        elif code == 0xd2:
            self.AddHydration(10.0)
            if S.m_byDosesCoffee >= 3:
                return 0
            if S.m_dwChampagne != 0 or S.m_dwMescal != 0:
                S.m_dwChampagne = 0
                S.m_dwMescal = 0
                if S.m_dwSymptoms & 0x40000000:
                    S.m_dwSymptoms &= 0xbfffffff
                else:
                    S.m_dwSymptomsCured |= 0x200
                self.snd()
            S.m_byDosesCoffee = (S.m_byDosesCoffee + 1) & 0xff
            if S.m_dTemperature < 50.0:
                S.m_dTemperature = 50.0
            self.AddEnergy(10.0)
            S.m_dwCoffee = 1
            S.m_dEnergyGainCoffee = S.m_dEnergyGainCoffee + 10.0
            if S.m_lHeartBeatVol < -2000:
                S.m_lHeartBeatVol = -2000
        S.m_dHydration = clamp(S.m_dHydration, 0.0, 100.0)
        S.m_wLastDrinkType = code & 0xffff
        S.m_dwLastDrank = self.time
        return 1

    def Consume(self, code):                                                # 1006cf40
        S = self.S
        code &= 0xffff
        if code == 10:
            if S.m_dwPrussic == 0:
                S.m_dwPrussic = 1
            return 1
        if code == 1:
            return 1
        if code == 0x1e:                                                    # adrenaline
            S.m_dStimulant = clamp(S.m_dStimulant + 60.0, 0.0, 100.0)
            self.AddEnergy(100.0)
            S.m_lHeartBeatVol = 0
            S.m_dEnergyGainAdrenaline = S.m_dEnergyGainAdrenaline + 100.0
            if S.m_dEnergyGainAdrenaline < 200.0:
                S.m_lHeartBeatVol = -30
            S.m_dwAdrenaline = 1
            return 1
        if code == 0x14:
            if S.m_dwDioscorin == 0:
                S.m_dwDioscorin = 1
            return 1
        if code == 0x32:                                                    # guarana
            if S.m_byDosesGuarana > 1:
                return 0
            S.m_byDosesGuarana = (S.m_byDosesGuarana + 1) & 0xff
            S.m_dwGuarana = 1
            S.m_dStimulant = clamp(S.m_dStimulant + 20.0, 0.0, 100.0)
            self.AddEnergy(20.0)
            S.m_dEnergyGainGuarana = S.m_dEnergyGainGuarana + 20.0
            if S.m_lHeartBeatVol < -1500:
                S.m_lHeartBeatVol = -1500
            return 1
        if code == 0x28:                                                    # coca
            if S.m_byDosesCoca > 1:
                return 0
            S.m_byDosesCoca = (S.m_byDosesCoca + 1) & 0xff
            S.m_dwCoca = 1
            S.m_dStimulant = clamp(S.m_dStimulant + 30.0, 0.0, 100.0)
            self.AddEnergy(50.0)
            S.m_dEnergyGainCoca = S.m_dEnergyGainCoca + 50.0
            if S.m_lHeartBeatVol < -1000:
                S.m_lHeartBeatVol = -1000
            return 1
        if code == 0x46:
            if S.m_dwFumes != 0:
                self.snd()
                S.m_dwSymptomsCured |= 0x8008
                S.m_dwFumes = 0
            return 1
        if code == 0x3c:                                                    # champagne
            S.m_dwChampagne = 1
            S.m_dwMescal = 0
            S.m_byGlassesDrunk = (S.m_byGlassesDrunk + 1) & 0xff
            if S.m_byGlassesDrunk < 6:                                      # PlayWav of the drunk voice assumed to succeed
                S.m_dwSymptoms |= 0x40000000
                return 1
            self.Vomit()
            self.Sleep(1, 1, 1)
            return 1
        if code == 0x5a:
            S.m_dSalt = clamp(S.m_dSalt + 10.0, 0.0, 100.0)
            return 1
        if code == 0x50:                                                    # chloroquine
            S.m_dwChloroquine = 1
            if S.m_dwSymptoms & 2:
                self.snd()
                S.m_dwSymptomsCured |= 2
            return 1
        if code == 0x6e:                                                    # sleeping pill
            S.m_wSleepingPillsPopped = (S.m_wSleepingPillsPopped + 1) & 0xffff
            S.m_dSedative = S.m_dSedative + 30.0
            if S.m_dwAdrenaline != 0:
                S.m_dwAdrenaline = 0x1c21
            S.m_dwSleepingPill = 2
            self.Sleep(1, 1, 1)
            return 1
        if code == 0x78:                                                    # vitamins
            self.snd()
            S.m_dProtein = clamp((100.0 - S.m_dProtein) * 0.3 + S.m_dProtein, 0.0, 100.0)
            return 1
        if code == 0x82:
            if S.m_dwWorms != 0:
                self.snd()
                S.m_dwSymptomsCured |= 0x10
                S.m_dwWorms = 0
            return 1
        if code == 0x8c:
            if S.m_dwPrussic != 0:
                self.snd()
                self.AddHealth(S.m_dPrussicHealthLoss)
                S.m_dwSymptomsCured |= 0x20
                S.m_dPrussicHealthLoss = 0.0
                S.m_dwPrussic = 0
            return 1
        if code == 0x96:
            S.m_dwCinchona = 1
            if S.m_dwSymptoms & 2:
                self.snd()
                S.m_dwSymptomsCured |= 2
            return 1
        if code == 0xa0:
            if S.m_dwSpiderBiteTimer != 0:
                self.snd()
                S.m_dwSymptomsCured |= 4
                S.m_dwSpiderBiteTimer = 0
            return 1
        if code == 0xb4:
            S.m_dwWhiteAngelTrip = 2
            self.WhiteAngelTrip()
            S.m_dwPeyoteTripTimer = 0
            return 1
        if code in (0xbe, 0xc3, 200, 0xd2):
            return self.Drink(code)
        if code == 0xe6:
            self.snd()
            S.m_dwPeyoteTripTimer = 2
            self.PeyoteTrip()
            S.m_dwWhiteAngelTrip = 0
            return 1
        if code == 0xfa:
            S.m_dwIndigestionTimer = 1
            return 1
        if code == 0x104:                                                   # ephedrine
            if S.m_dwFumes == 0:
                if S.m_dwSynthAttack != 0:
                    self.snd()
                    S.m_dwSymptomsCured |= 0x4000
                    S.m_dwSynthAttack = 0
            else:
                self.snd()
                S.m_dwSymptomsCured |= 0x8008
                S.m_dwFumes = 0
            S.m_dStimulant = clamp(S.m_dStimulant + 20.0, 0.0, 100.0)
            self.AddEnergy(20.0)
            if S.m_lHeartBeatVol < -30:
                S.m_lHeartBeatVol = -30
            S.m_dwEphedrineTimer = 1
            self.AddEnergy(10.0)
            return 1
        if code == 0x105:
            return 1
        if code == 0x10e:
            self.snd()
            S.m_dwCactusPoisonTimer = 1
            return 1
        if code == 0x118:
            self.Vomit()
            self.LoseHealth(10.0, 0x10, 0)
            return 1
        if code == 0x122:
            if S.m_dwHogweedBurnTimer != 0:
                self.snd()
                S.m_dwSymptomsCured |= 0x800
                S.m_dwHogweedBurnTimer = 0
            return 1
        if code == 300:
            self.rng.rand()                                                 # the pain wav number
            S.m_dwHogweedBurnTimer = 1
            return 1
        if code == 0x136:
            self.LoseHealth(10.0, 0x11, 0)
            S.m_dwSpiderBiteTimer = 1
            return 1
        if code == 0x140:                                                   # mescal
            S.m_dwMescal = 1
            S.m_dwChampagne = 0
            S.m_bySwigsDrunk = (S.m_bySwigsDrunk + 1) & 0xff
            if S.m_bySwigsDrunk < 5:
                S.m_dwSymptoms |= 0x40000000
                return 1
            self.Vomit()
            self.Sleep(1, 1, 1)
            return 1
        if code in (0x154, 0x15e):
            return 1
        return 0                                                            # "Dodgy consumable"

    # ---------------------------------------------------------------------------------------- sleep
    def CanSleep(self):                                                     # 10067540
        S = self.S
        G = self.G
        if self.modal != 0:
            return 0
        if S.m_szLocation[:1] in ('U', 'X'):
            return 0
        if S.m_dwSynthAttack == 1:
            return 1
        if (self.site()['flags'] >> 24) & 1:
            return 0
        if 80.0 < S.m_dEnergy and G['DAT_100c42e4'] == 0:
            return 0
        if S.m_bInsideAngelSequence != 0:
            return 0
        if S.m_bInsidePeyoteSequence != 0:
            return 0
        G['DAT_100c42f8'] = 1 if u32(S.m_dwSleepingPill) > 1 else 0
        if self.time == S.m_dwLastWokeUp and G['DAT_100c42f8'] == 0:
            return 0
        return 1

    def Sleep(self, a, b, c):                                               # 10067600
        S = self.S
        G = self.G
        G['DAT_100c42e4'] = a
        G['DAT_100c430c'] = b
        if self.CanSleep() == 0:
            return 0
        G['DAT_100c42e0'] = self.rng.rand_range_ftol(10800.0, 21600.0)
        S.m_dwLastFellAsleep = self.time
        S.m_dwUnk324 = 0
        S.m_wWakeReason = 1
        S.m_dwUnk320 = 0
        S.m_hBreathingLoop = -1
        S.m_hHeartBeatLoop = -1
        S.m_hDrunkVoice = -1
        G['DAT_100c4300'] = 0.05
        G['DAT_100c42f0'] = 0.25
        x1 = 1.5 if S.m_pBiome == 'house' else 1.0                           # biome +0x40
        x2 = 1.5 if S.m_pBiome == 'house' else 1.0                           # biome +0x48
        G['DAT_100c42e8'] = 80.0
        if self.is_at_page('RHUT'):
            x1 = 1.5
            x2 = 1.5
        if S.m_bHasSleepingBag != 0 and a == 0:
            x1 = x1 * 1.5
            x2 = x2 * 1.5
        if self.is_at_page('nPH5BED') or self.is_at_page('nPH17berth'):
            x1 = 2.0
            x2 = 2.0
            G['DAT_100c42e8'] = 100.0
        if (S.m_wBreathing & 1) == 0 and self.cel()[0x50] < 16.0:
            x2 = x2 * 0.5
        if u32(S.m_dwSleepingPill) > 2:
            x2 = x2 * 1.5
            S.m_wSleepingPillsPopped = 0
            S.m_dwSleepingPill = 1
        G['DAT_100c42f0'] = x1 * G['DAT_100c42f0']
        G['DAT_100c4300'] = G['DAT_100c4300'] * x1
        G['DAT_100c42e0'] = ftol(xmul(float(G['DAT_100c42e0']), x2))
        if c != 0:
            S.m_szReturnPage = S.m_szLocation
        return 1

    def WakeUp(self):                                                       # 10067910
        S = self.S
        G = self.G
        S.m_bAwake = 1
        S.m_dwTimeLastUpdate = self.time
        S.m_dwLastWokeUp = self.time
        if S.m_bHasSleepingBag != 0:
            ob = self.find_object('survibag')
            if ob is not None:
                ob.variant = 'rolled'
        if S.m_wWakeReason in (3, 4, 5):
            self.snd()                                                      # the wake-up cough / shiver / pant wav
        # the return page jump, palette and sound calls are engine effects (assumed to succeed)
        self.set_ticks(0x28)
        S.m_dHealth = clamp(S.m_dHealth, 0.0, self.GetMaxHealth())
        S.m_dEnergy = clamp(S.m_dEnergy, 0.0, S.m_dHealth)
        S.m_dNutrition = clamp(S.m_dNutrition, 0.0, 100.0)
        S.m_dHydration = clamp(S.m_dHydration, 0.0, 100.0)
        G['DAT_100c42e4'] = 0
        G['DAT_100c42f8'] = 0
        G['DAT_100c430c'] = 0
        S.m_dwSymptoms = S.m_dwSymptoms & ~S.m_dwSymptomsCured & U32
        S.m_dwSymptomsCured = 0

    def Dream(self):                                                        # 1006dfc0 (the movie cannot be opened headless)
        S = self.S
        self.set_ticks(-1)
        if S.m_dwNextDream == 0:
            avail = S.m_dwDreamsAvailable
            seen = S.m_dwDreamsSeen
            cnt = 0
            for i in range(32):
                bit = 1 << i
                if (bit & avail) != 0 and (seen & bit) == 0:
                    cnt += 1
            n = 0
            r = self.rng.rand()
            for i in range(32):
                bit = 1 << i
                if (bit & avail) != 0 and (seen & bit) == 0:
                    n += 1
                    if (u32(r * cnt)) // 0x7fff + 1 == n:
                        S.m_dwNextDream = bit
        S.m_dwNextDream = 0
        return 0

    def OnIdle(self):                                                       # 10065e80
        S = self.S
        G = self.G
        if S.m_bFallAsleepOnIdle != 0:
            S.m_bFallAsleepOnIdle = 0
            self.Sleep(0, 1, 1)
            return 0
        if S.m_bRunSleepOnIdle == 0:
            if S.m_bAwake == 0 and S.m_dwUnk324 != 0:
                self.WakeUp()
                return 0
            # (+0x760 is never set); hotspot message bookkeeping:
            if self.msg_timer == 0 or u32(self.tick) <= self.msg_timer:
                if S.m_dwHotspotExpiry != 0 and u32(S.m_dwHotspotExpiry) < u32(self.tick) and self.msg_timer2 < u32(self.tick):
                    S.m_dwHotspotExpiry = 0
            return 1
        S.m_bRunSleepOnIdle = 0
        e0 = S.m_dEnergy
        elapsed = 0
        while True:
            self.Advance(0x5a, 0)
            self.ResetDeltas()
            if ((~S.m_dwSymptomsCured) & S.m_dwSymptoms & 0x40000200) == 0 and S.m_dwSynthAttack == 0:
                if S.m_dEnergy - e0 < G['DAT_100c42e8']:
                    self.AddEnergy(G['DAT_100c42f0'])
                self.AddHealth(G['DAT_100c4300'])
            cel = self.cel()
            if (S.m_wBreathing & 1) == 0 and 50.0 < cel[0x98] and S.m_dwFumes == 0:
                S.m_dwFumes = 1
            S.m_dwUpdateDt = 0x5a
            self.UpdateAilments()
            udt = float(u32(S.m_dwUpdateDt))
            self.LoseNutrition(((S.m_dNutrition * 0.01) * udt) * 0.00034722222222222224)
            self.LoseHydration(((S.m_dHydration * 0.01) * udt) * 0.0002314814814814815)
            v = cel[0x38] + self.site()['f8']
            if S.m_bWearingPoncho != 0 and v < 20.0:
                v = v + 15.0
            if S.m_bHasSleepingBag != 0 and G['DAT_100c42e4'] == 0 and v < 20.0:
                v = v + 15.0
            target = 2.5 * v
            local = 0.0
            if target < S.m_dTemperature:
                local = ((target - S.m_dTemperature) * udt) * 0.00044444444444444447
                if S.m_dIdealTemp < S.m_dTemperature:
                    local = ((S.m_dTemperature - S.m_dIdealTemp) * udt) * -0.0017777777777777779 + local
            elif target > S.m_dTemperature:
                local = ((target - S.m_dTemperature) * udt) * 0.00044444444444444447
                if S.m_dIdealTemp > S.m_dTemperature:
                    local = ((S.m_dTemperature - S.m_dIdealTemp) * udt) * -0.0017777777777777779 + local
            self.AddTemperature(local)
            woke = False
            if G['DAT_100c42f8'] == 0 and 20.0 < S.m_dEnergy:
                if 75.0 < S.m_dTemperature:
                    S.m_dwUnk320 = 1
                    S.m_wWakeReason = 5
                if S.m_dTemperature < 25.0:
                    S.m_dwUnk320 = 1
                    S.m_wWakeReason = 4
                if u32(S.m_dwFumes) > 0xe10:
                    S.m_dwUnk320 = 1
                    S.m_wWakeReason = 3
                if (cel[0x108] & 2) != 0 and (self.site()['flags'] & 0x800000) == 0:
                    S.m_dwUnk320 = 1
                    S.m_wWakeReason = 7
                if S.m_dwUnk320 != 0:
                    woke = True
            if woke:
                break
            elapsed = u32(elapsed + 0x5a)
            if not (u32(G['DAT_100c42e0'] - 0x5a) >= elapsed):
                break
        if S.m_dwUnk320 == 0:
            self.Advance(u32(G['DAT_100c42e0'] - elapsed), 0)
        S.m_dEnergy = clamp(S.m_dEnergy, 0.0, S.m_dHealth)
        S.sleep_gain = S.m_dEnergy - e0
        if S.m_dHealth <= 0.0:
            self.Die(0)
            return 0
        if G['DAT_100c430c'] == 0 or self.Dream() == 0:
            self.WakeUp()
        return 0

    # ---------------------------------------------------------------------------------------- game objects
    def UpdateAllObjects(self):                                             # CWorld::UpdateAllObjects 100146a0
        for o in list(self.objs.values()):
            self.vcall(o, 0x68, self.time)

    def plant_cel(self, plant):                                             # plant+0x4c: the cel of the plant's site
        return self.cels[SITES[PLANTS[plant]][0]]

    def vcall(self, o, slot, arg=None):
        """virtual slot `slot` of the class of object o (vtables 10091b70 base, 10093470 CFood, 10091020 CGoggles,
        10094208 COxyMask, 10093248 COxyCylinder)"""
        cls = o.cls
        if cls == 'food':
            if slot == 0x2c:
                return self.food_eat(o)
            if slot == 0x44:
                return self.food_pickup(o)
            if slot == 0x68:
                return self.food_update(o, arg)
            if slot in (0x78, 0x7c):
                return 1
            if slot == 0x94:
                return self.food_remove(o)
        elif cls == 'goggles':
            if slot == 0x44:
                self.base_pickup(o)
                self.goggles_calc(o)
                return 1
            if slot == 0x4c:
                return self.goggles_wear(o)
            if slot == 0x50:
                return self.goggles_remove(o)
            if slot == 0x54:
                o.charge = 100.0
                o.power = 6
                o.shown = 6
                return 1
            if slot == 0x68:
                return self.goggles_update(o, arg)
            if slot == 0x6c:
                return 1                                                    # ShowPower: engine only
        elif cls == 'mask':
            if slot == 0x4c:
                return self.mask_wear(o)
            if slot == 0x50:
                self.S.m_pAirMask = ''
                self.S.m_wBreathing &= 0xfffc
                return 1
            if slot == 0x68:
                return self.mask_update(o, arg)
        elif cls == 'cyl':
            if slot == 0x2c:
                return self.cyl_toggle(o)
            if slot == 0x44:
                return self.cyl_pickup(o)
            if slot == 0x48:
                return self.cyl_dropped(o, arg)
            if slot == 0x54:
                return self.cyl_useon(o, arg)
            if slot == 0x68:
                return self.cyl_update(o, arg)
            if slot == 0x78:
                return self.cyl_hold(o)
        return self.base_vcall(o, slot, arg)

    def base_vcall(self, o, slot, arg):                                     # CObject vtable 10091b70
        S = self.S
        G = self.G
        if slot == 0x2c or slot == 0x3c or slot == 0x50:
            return 1
        if slot == 0x34:                                                    # 10029120
            return self.vcall(o, 0x7c)
        if slot == 0x38:                                                    # 10029130
            G['DAT_100c42d0'] = 1
            return self.vcall(o, 0x78)
        if slot == 0x40:                                                    # 10028ff0
            return self.vcall(o, 0x78)
        if slot == 0x44:
            return self.base_pickup(o)
        if slot == 0x48:
            return self.base_dropped(o)
        if slot == 0x4c:                                                    # 10029160
            if S.m_pObjHolding == o.name:
                self.vcall(o, 0x7c)
            return 1
        if slot == 0x54:                                                    # 10028f10: only the removal of "use-up" objects
            if o.flags & 0x20:
                self.vcall(o, 0x94)
            return 1
        if slot == 0x78:                                                    # 10029190
            S.m_pObjHolding = o.name
            return self.vcall(o, 0x28)
        if slot == 0x7c:                                                    # 100291b0
            S.m_pObjHolding = ''
            return 1
        if slot == 0x28:                                                    # 100291c0 (message only)
            if G['DAT_100c42d0'] != 0:
                G['DAT_100c42d0'] = 0
            return 1
        if slot == 0x94:
            return self.remove_object(o)
        if slot == 0x68:
            return 0
        return 1

    def base_pickup(self, o):                                               # CGameObj::PickUp 10029000
        S = self.S
        if ((o.flags >> 24) & 1) and o.variant.lower() == 'burnt':
            self.vcall(o, 0x94)
            return 1
        self.AddWeight(o.weight)
        o.held = 1
        # pickup_event: CGame::TriggerEvent (story) is not modelled
        if ((o.flags >> 24) & 0x80) and S.m_bInsideAngelSequence != 0:
            self.G['DAT_100c2498'] = 1
        if ((o.flags >> 8) & 2) and S.m_szLocation[:1] in ('N', 'A'):
            self.G['DAT_1009f838'] = 1
        return 1

    def base_dropped(self, o):                                              # FUN_100290a0
        S = self.S
        self.Drop(o.weight)
        o.held = 0
        self.vcall(o, 0x7c)
        if ((o.flags >> 8) & 2) and S.m_szLocation[:1] == 'N':
            self.G['DAT_1009f838'] = 1
        return 1

    def remove_object(self, o):                                             # FUN_10029510 (Eng_RemoveGameObject succeeds)
        o.in_scene = 0
        if o.held != 0:
            o.held = 0
            self.Drop(o.weight)
        if self.S.m_pObjHolding == o.name:
            self.S.m_pObjHolding = ''
        return 1

    # ---- CFood
    def food_remove(self, o):                                               # FUN_10063750
        self.remove_object(o)
        if o.in_scene == 0:
            if o.plant and o.state == 2:
                p = self.plants[o.plant]
                p['mask'] = (p['mask'] & ~(1 << ((o.slot - 1) & 0x1f))) & 0xffff
                p['count'] = p['count'] - 1
            o.state = 1
        return 1

    def food_pickup(self, o):                                               # CFood::PickUp 10063ad0
        if o.id == 0x146:
            pass                                                            # termite sound
        self.AddWeight(o.weight)
        o.held = 1
        if o.state == 2:
            o.state = 3
            o.time_picked = self.time
            if o.plant:
                p = self.plants[o.plant]
                p['mask'] = (p['mask'] & ~(1 << ((o.slot - 1) & 0x1f))) & 0xffff
                p['count'] = p['count'] - 1
        if (o.flags & 0x80) == 0:
            self.vcall(o, 0x28)
        return 1

    def food_update(self, o, now):                                          # CFood::Update 100639b0
        if o.state == 2:
            return 1
        if o.in_scene == 0 and u32(o.regrow) < u32(now - o.time_picked):
            if not o.plant:
                # Eng_Api45_ReturnGameObjectHidden: assumed true for an object that is not in the scene
                o.variant = 'fresh'
                o.in_scene = 1
                o.state = 2
            else:
                p = self.plants[o.plant]
                pcel = self.plant_cel(o.plant)
                if 0.4 < p['health'] and PLANTS[o.plant].lower() != self.S.m_pSite.lower() \
                        and ((o.flags & 0x2000) == 0 or (pcel[0x108] & 0x10) == 0) \
                        and ((o.flags & 0x4000) == 0 or (pcel[0x108] & 0x10) != 0):
                    o.variant = 'fresh'
                    o.in_scene = 1
                    o.state = 2
                    p['count'] = p['count'] + 1
                    p['mask'] = (p['mask'] | (1 << ((o.slot - 1) & 0x1f))) & 0xffff
        return 1

    def food_eat(self, o):                                                  # CFood::Eat 10063790
        S = self.S
        if o.flags & 0x200000:
            return 1
        h = self.holding()
        if (o.flags & 0x400000) and h is not None and (h.flags & 0x800000) and h.variant.lower() == 'nobait':
            h.variant = 'Bait'
            self.vcall(o, 0x94)
            return 1
        if S.m_pAirMask:
            return 1
        if o.state == 4:
            S.m_pLastEatenPlant = o.plant or ''
            self.Vomit()
            self.LoseHealth(5.0, 10, 1)
        else:
            if o.state == 5 and self.Eat(o.nut, o.prot, o.water, o.code) != 0:
                S.m_pLastEatenPlant = o.plant or ''
                self.vcall(o, 0x94)
                if o.id == 0x118 and (S.m_dwChampagne != 0 or S.m_dwMescal != 0):
                    S.m_dwChampagne = 0
                    S.m_dwMescal = 0
                    if (S.m_dwSymptoms & 0x40000000) == 0:
                        S.m_dwSymptomsCured |= 0x200
                    else:
                        S.m_dwSymptoms &= 0xbfffffff
                    self.snd()
            nut = o.nut
            prot = o.prot
            if o.flags & 1:
                nut = nut * 0.2
                prot = prot * 0.2
            if self.Eat(nut, prot, o.water, o.code) == 0:
                return 1 if o.in_scene == 0 else 0
            S.m_pLastEatenPlant = o.plant or ''
        self.vcall(o, 0x94)
        return 1 if o.in_scene == 0 else 0

    # ---- CGoggles
    def goggles_calc(self, o):                                              # CGoggles::CalcPowerState 1001d640
        if o.charge == 0.0:
            o.power = 1
        elif o.charge < 20.0:
            o.power = 2
        elif o.charge < 40.0:
            o.power = 3
        elif o.charge < 60.0:
            o.power = 4
        elif o.charge < 80.0:
            o.power = 5
        else:
            o.power = 6

    def goggles_wear(self, o):                                              # CGoggles::Wear 1001d450
        S = self.S
        S.m_pObjHolding = ''
        S.m_pGoggles = o.name
        if 0.0 < o.charge:
            self.vcall(o, 0x68, self.time)
            o.on = 1
        return 1

    def goggles_remove(self, o):                                            # FUN_1001d4d0
        self.S.m_pGoggles = ''
        self.G['DAT_100c1c44'] = 0
        o.on = 0
        return 1

    def goggles_update(self, o, now):                                       # CGoggles::Update 1001d530
        dt = u32(now - o.timestamp)
        if dt == 0:
            return 0
        o.timestamp = now
        if o.on != 0:
            o.charge = clamp(float(dt) * -0.005555555555555556 + o.charge, 0.0, 100.0)
            self.goggles_calc(o)
            if o.charge == 0.0:
                o.on = 0
            if o.shown != o.power:
                o.shown = o.power
        return 1

    # ---- COxyMask
    def mask_wear(self, o):                                                 # COxyMask::Wear 10076b00
        S = self.S
        S.m_pAirMask = o.name
        if 0.0 < o.filter:
            if o.filter < 0.1:
                S.m_wBreathing |= 2
            S.m_wBreathing |= 1
        return 1

    def mask_update(self, o, now):                                          # COxyMask::Update 10076bc0
        S = self.S
        old = o.timestamp
        o.timestamp = now
        if (S.m_wBreathing & 1) != 0:
            o.filter = float(u32(now - old)) * -1.388888888888889e-05 + o.filter
            if o.filter <= 0.0:
                o.filter = 0.0
                S.m_wBreathing &= 0xfffc
                return 1
            if o.filter < 0.1:
                S.m_wBreathing |= 2
        return 1

    # ---- COxyCylinder
    def cyl_pickup(self, o):                                                # FUN_10062d20
        self.base_pickup(o)
        o.unk44 = 1
        self.fx_site[o.fx] = self.S.m_pSite
        return 1

    def cyl_dropped(self, o, extra):                                        # FUN_10062d80
        self.base_dropped(o)
        o.unk44 = 0
        if (extra or '').lower() == 'dcylinder':
            o.unk54 = 1
            t = self.find_object('torch')
            if t is not None:
                t.torch_cyl = o.name
        return 1

    def cyl_toggle(self, o):                                                # COxyCylinder::ToggleValve 10062e30
        if o.variant.lower() == 'oxyon':
            o.valve = 0
            o.variant = 'oxyoff'
            return 1
        if 0.0 < o.contents:
            o.valve = 1
            o.variant = 'oxyon'
            self.G['DAT_100c27e4'] = 5
        return 1

    def cyl_useon(self, o, extra):                                          # FUN_10062ef0 (m_pFeature is never set)
        o.variant = 'oxyoff'
        return 1

    def cyl_hold(self, o):                                                  # FUN_10062df0
        self.base_vcall(o, 0x78, None)
        o.unk54 = 0
        t = self.find_object('torch')
        if t is not None:
            t.torch_cyl = ''
        self.vcall(o, 0x28)
        return 1

    def cyl_update(self, o, now):                                           # COxyCylinder::Update 10062fa0
        old = o.timestamp
        o.timestamp = now
        if o.valve != 0:
            dd = float(u32(now - old))
            o.contents = dd * -0.0005555555555555556 + o.contents
            cel = self.cels[SITES[self.fx_site[o.fx]][0]]
            cel[0x50] = dd * 0.0005555555555555556 + cel[0x50]
            if o.contents < 0.0:
                o.contents = 0.0
            if o.contents == 0.0:
                o.valve = 0
                o.variant = 'off'
                if self.G['DAT_100c27e4'] == 5:
                    self.G['DAT_100c27e4'] = 1
        return 1

    # ---------------------------------------------------------------------------------------- Event_OnGameObject 1001e380
    def event(self, name, code, extra):
        o = self.find_object(name)
        if o is None:
            return
        if extra is not None:
            extra = extra.upper()
        if code == 1:
            self.vcall(o, 0x44)
        elif code == 2:
            self.vcall(o, 0x34)
        elif code == 3:
            self.vcall(o, 0x3c)
        elif code == 4:
            self.vcall(o, 0x2c)
        elif code == 5:
            pass                                                            # extempty.wav only
        elif code == 6:
            self.vcall(o, 0x48, extra)
        elif code == 7:
            self.vcall(o, 0x54, extra)
        elif code == 8:
            self.vcall(o, 0x4c)
        elif code == 9:
            self.vcall(o, 0x50)
        elif code == 10:
            self.vcall(o, 0x40)
        elif code == 0xc:
            self.vcall(o, 0x38)

    # ---------------------------------------------------------------------------------------- dump
    def fmt_member(self, n, t, v):
        if t == 'double':
            return repr(float(v))
        if t in ('NamedRef',) or t.startswith('std::array<u8'):
            return v
        if t in ('Item*', 'const Biome*'):
            return v if v else ''
        return str(int(v))

    def dump(self, objnames):
        S = self.S
        out = ['== t=%d seed=%d' % (self.time, self.rng.s)]
        for n, t, o in SELF_MEMBERS:
            v = getattr(S, n)
            if n == 'm_aSoundCue':
                for i, x in enumerate(v):
                    out.append('self.%s[%d] = %d' % (n, i, x))
            else:
                out.append('self.%s = %s' % (n, self.fmt_member(n, t, v)))
        for k, v in self.G.items():
            out.append('global.%s = %s' % (k, repr(float(v)) if isinstance(v, float) else str(int(v))))
        for nm in objnames:
            o = self.find_object(nm)
            if o is None:
                raise SystemExit('unknown object %s' % nm)
            p = 'obj.%s.' % o.name
            out.append(p + 'm_dwNameStringId = %d' % o.id)
            out.append(p + 'm_dwFlags = %d' % o.flags)
            out.append(p + 'm_dwPickupEvent = %d' % o.pickup_event)
            out.append(p + 'm_dwUnk1c = %d' % o.unk1c)
            out.append(p + 'm_dWeight = %r' % float(o.weight))
            out.append(p + 'm_dwTimeStamp = %d' % o.timestamp)
            out.append(p + 'm_bInScene = %d' % o.in_scene)
            out.append(p + 'm_bHeld = %d' % o.held)
            out.append(p + 'm_szVariant = %s' % o.variant)
            if o.cls == 'food':
                out.append(p + 'm_pPlant = %s' % (o.plant or ''))
                out.append(p + 'm_nPlantSlot = %d' % o.slot)
                out.append(p + 'm_byState = %d' % o.state)
                out.append(p + 'm_dNutrition = %r' % o.nut)
                out.append(p + 'm_dProtein = %r' % o.prot)
                out.append(p + 'm_dWater = %r' % o.water)
                out.append(p + 'm_wConsumableCode = %d' % o.code)
                out.append(p + 'm_dwTimePicked = %d' % o.time_picked)
                out.append(p + 'm_dwAgeTime70 = %d' % o.a70)
                out.append(p + 'm_dwAgeTime74 = %d' % o.a74)
                out.append(p + 'm_dwRegrowTime = %d' % o.regrow)
            elif o.cls == 'goggles':
                out.append(p + 'm_dCharge = %r' % o.charge)
                out.append(p + 'm_byPowerState = %d' % o.power)
                out.append(p + 'm_byShownPowerState = %d' % o.shown)
                out.append(p + 'm_bOn = %d' % o.on)
            elif o.cls == 'mask':
                out.append(p + 'm_dFilter = %r' % o.filter)
            elif o.cls == 'cyl':
                out.append(p + 'm_pFx = %s' % o.fx)
                out.append(p + 'm_dContents = %r' % o.contents)
                out.append(p + 'm_bValveOpen = %d' % o.valve)
                out.append(p + 'm_dwUnk54 = %d' % o.unk54)
            elif o.cls == 'consumable':
                out.append(p + 'm_wContentCode = %d' % o.code)
                out.append(p + 'm_wMaxDoses = %d' % o.maxdoses)
                out.append(p + 'm_dWeightFull = %r' % float(o.wfull))
                out.append(p + 'm_dWeightEmpty = %r' % float(o.wempty))
            elif o.cls == 'single':
                out.append(p + 'm_wContentCode = %d' % o.code)
        return out


# ------------------------------------------------------------------------------------------------ script
def num(tok):
    tok = tok.strip()
    if tok.lower().startswith('0x'):
        return int(tok, 16)
    try:
        return int(tok)
    except ValueError:
        return float(tok)


def run_script(sim, text):
    S = sim.S
    for ln, raw in enumerate(text.split('\n'), 1):
        line = raw.split('#', 1)[0].strip()
        if not line:
            continue
        t = line.split()
        c = t[0]
        if c == 'seed':
            sim.rng = Rng(int(t[1]))
        elif c == 'tick':
            sim.tick = int(t[1]) & U32
        elif c == 'ticks':                                                  # oracle extension: SetTicksPerGameSec(N)
            sim.set_ticks(int(t[1]))
        elif c == 'time':
            sim.time = i32(int(t[1]))
            sim.tod = sim.time % 86400
        elif c == 'self':
            name = t[1]
            if name not in SELF_TYPES:
                raise SystemExit('line %d: unknown member %s' % (ln, name))
            setattr(S, name, wrap_member(name, num(t[2])))
        elif c == 'cel':
            cel = sim.cels[t[1]]
            for kv in t[2:]:
                k, v = kv.split('=', 1)
                cel[CEL_KEYS[k]] = int(num(v)) if k == 'flags' else float(num(v))
        elif c == 'goto':
            k = lower_in(SITES, t[1])
            if k is None:
                raise SystemExit('line %d: unknown site %s' % (ln, t[1]))
            S.m_pSite = k
            sim.resolve_site(k)
            if len(t) > 2:
                S.m_szLocation = t[2].upper()
                S.m_pPage = S.m_szLocation
        elif c == 'update':
            sim.Update(int(t[1]) if len(t) > 1 else 1)
        elif c == 'run':
            secs = int(t[1])
            step = int(t[2])
            p = int(t[3]) if len(t) > 3 else 1
            start = sim.time
            while True:
                sim.time = i32(sim.time + step)
                sim.tod = sim.time % 86400
                sim.Update(p)
                if sim.G['dead'] or (sim.time - start) >= secs:
                    break
        elif c == 'advance':
            sim.Advance(int(t[1]), int(t[2]) if len(t) > 2 else 1)
        elif c == 'consume':
            sim.Consume(int(num(t[1])))
        elif c == 'drink':
            sim.Drink(int(num(t[1])))
        elif c == 'event':
            sim.event(t[1], int(num(t[2])), t[3] if len(t) > 3 else None)
        elif c == 'sleep':
            if sim.Sleep(int(num(t[1])), int(num(t[2])), int(num(t[3]))):
                S.m_bRunSleepOnIdle = 1
                for _ in range(10):
                    r = sim.OnIdle()
                    if sim.G['dead']:
                        break
                    if r == 0 and S.m_bAwake != 0:
                        break
        elif c == 'idle':
            for _ in range(int(t[1]) if len(t) > 1 else 1):
                sim.OnIdle()
        elif c == 'die':
            sim.Die(int(num(t[1])))
        elif c == 'dump':
            names = []
            i = 1
            while i < len(t):
                if t[i] == 'obj':
                    names.append(t[i + 1])
                    i += 2
                else:
                    raise SystemExit('line %d: bad dump argument %s' % (ln, t[i]))
            print('\n'.join(sim.dump(names)))
        else:
            raise SystemExit('line %d: unknown command %s' % (ln, c))


def main(argv):
    seed = 1
    args = []
    i = 1
    while i < len(argv):
        if argv[i] == '--seed':
            seed = int(argv[i + 1])
            i += 2
        else:
            args.append(argv[i])
            i += 1
    if len(args) != 2:
        raise SystemExit('usage: self_oracle.py <file.sav> <script> [--seed N]')
    sim = Sim(args[0], seed)
    run_script(sim, open(args[1]).read())


if __name__ == '__main__':
    main(sys.argv)
