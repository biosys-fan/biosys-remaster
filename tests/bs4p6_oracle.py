"""Independent statements of the rules of the air / water panels (packet P6), written from the decompile of exe:0042ed70 / 0042ff10 (inits),
exe:004319d0 / 00431c30 (route checks), exe:004327e0 / 00432b50 (setters) and exe:00435e40 / 00436350 (handlers) - not from the C++.

Vocabulary: biomes 's' savannah, 'o' ocean, 'e' evolved (dead biome), 'r' rainforest; CODE = FEATURE_DESTINATION value of the biome.
A lung state is (active, draw, dest) = (FEATURE_ACTIVE, FEATURE_DRAW_AIR_FROM_BIOME, FEATURE_DESTINATION) as the game returns them,
a pump state (active, dest)."""

BIOMES = 'soer'
CODE = {'s': 0x16, 'o': 0x18, 'e': 0x17, 'r': 0x15}
SUFFIX = {'s': 'sav', 'o': 'oc', 'e': 'evo', 'r': 'rf'}
LUNG = {'s': 'fbs2lung', 'o': 'fbo1lung', 'e': 'fbd5lung', 'r': 'fbr2lung'}
TANK = {'s': 'fbs3tank', 'o': 'fbo2tank', 'e': 'fbd3tank', 'r': 'fbr3tank'}
PUMP = {'s': 'fbs3watpmp', 'o': 'fbo2watpmp', 'e': 'fbd3watpmp', 'r': 'fbr3watpmp'}
GAME_TANK = {0x16: 'STANK', 0x18: 'OTANK', 0x17: 'DBTANK', 0x15: 'RFTANK'}
AIR_PAGE = {b: 'panela' + SUFFIX[b] for b in BIOMES}
WATER_PAGE = {b: 'panelw' + SUFFIX[b] for b in BIOMES}
# the item shown for a destination code (0xf s, 0x10 o, 0x11 e, 0x12 r)
ITEM_OF_CODE = {0x16: 0xf, 0x18: 0x10, 0x17: 0x11, 0x15: 0x12}
ITEM_OF_BIOME = {b: ITEM_OF_CODE[CODE[b]] for b in BIOMES}
ROUTE_ITEMS = (0x13, 0x14, 0x15, 0x16)
DYN_AIR = {1, 2, 0x1c, 0x1d, 0x13, 0x14, 0x15, 0x16, 0xf, 0x10, 0x11, 0x12, 0x17, 0x1b}
DYN_WATER = {1, 2, 0x13, 0x14, 0x15, 0x16, 0xf, 0x10, 0x11, 0x12}
# how a panel is opened from its main page: (main page, hotspot that reveals the group, hotspot that opens the panel)
AIR_OPEN = {'s': ('oxyair', 'techair', '0_airps1'), 'o': ('oxyair', 'techair', '0_airpo1'),
            'e': ('oxyair', 'techair', '0_airpe1'), 'r': ('oxyair', 'techair', '0_airpr1')}
WATER_OPEN = {'s': ('watpur', 'techutil', '14_utis1'), 'o': ('watpur', 'techutil', '14_utio1'),
              'e': ('watpur', 'techutil', '14_utie1'), 'r': ('watpur', 'techutil', '14_utir1')}


def pump_receiving(target, pumps):
    """exe:004319d0: pumps = {biome: (active, dest)}; None = the check fails, else 1 / 0"""
    for b in BIOMES:
        a, d = pumps[b]
        if a == 0:
            continue
        if d < 0x15 or d > 0x18:
            return None
        if d == CODE[target]:
            return 1
    return 0


def lung_receiving(target, lungs):
    """exe:00431c30: lungs = {biome: (active, draw, dest)}; None = the check fails, else 1 / 0"""
    for b in BIOMES:
        a, dr, d = lungs[b]
        if a == 0:
            continue
        if dr != 0:
            continue
        if d < 0x15 or (d > 0x18 and d != 0x1a):
            return None
        if d == CODE[target]:
            return 1
    return 0


def air_init(b, broken, lungs):
    """exe:0042ff10: what the init of the lung panel of biome b leaves: items shown among DYN_AIR, overlay / broken ornaments shown,
    the mirrors; ok = False when the init stops with a logged error before the gauges (then dyn / items are what was done so far)"""
    r = dict(dyn=set(), overlay=False, broken=bool(broken), active=0, recv=0, draw=0, dest=-1, ok=True, gauges=False)
    if broken:
        return r
    rc = lung_receiving(b, lungs)
    if rc is None:
        r['ok'] = False
        return r
    a, dr, d = lungs[b]
    if rc == 1:
        r['recv'] = 1
        r['overlay'] = True
    elif a == 0:
        r['dyn'] = set(ROUTE_ITEMS)
    else:
        r['active'] = 1
        if dr != 1:
            if d in ITEM_OF_CODE:
                r['dyn'] = {ITEM_OF_CODE[d], 2}
                r['dest'] = d
            elif d == 0x1a:
                r['dyn'] = {ITEM_OF_BIOME[b], 0x1d}
                r['dest'] = d
            else:
                r['ok'] = False
                return r
        else:
            r['draw'] = 1
            r['dyn'] = {ITEM_OF_BIOME[b], 0x1b}
    r['gauges'] = True
    return r


def water_init(b, broken, pumps):
    """exe:0042ed70 for the panel of biome b (broken: FEATURE_MALFUNCTION of the tank)"""
    r = dict(dyn=set(), overlay=False, broken=bool(broken), active=0, recv=0, dest=-1, ok=True, gauges=False)
    if broken:
        return r
    rc = pump_receiving(b, pumps)
    if rc is None:
        r['ok'] = False
        r['dyn'] = None                    # the init stops before it touches an item (the water init hides its items only after the check)
        return r
    a, d = pumps[b]
    if rc == 1:
        r['recv'] = 1
        r['overlay'] = True
    elif a == 0:
        r['dyn'] = set(ROUTE_ITEMS)
    else:
        r['active'] = 1
        if d in ITEM_OF_CODE:
            r['dyn'] = {2, ITEM_OF_CODE[d]}
            r['dest'] = d
        else:
            r['ok'] = False
            r['dyn'] = {2}                 # shown / hidden so far: the running pump's own button
            return r
    r['gauges'] = True
    return r


def air_setter(b, new, old):
    """exe:00432b50: the events (feature, id, a) the lung setter sends when the item mask goes from old to new, and what it does to the
    page mirrors: (events, active mirror or None, dest or None, items dict or None)"""
    def up(bit):
        return bool(new & bit) and not (old & bit)
    f = LUNG[b]
    if up(2) or up(1 << 28) or up(1 << 23):
        ev = [(f, 2, 0)]
        if up(1 << 23):
            ev.append((f, 0x14, 0x1a))
        return ev
    if up(4) or up(1 << 29) or up(1 << 27):
        ev = []
        if up(1 << 27):
            ev.append((f, 0x11, 1))
        ev.append((f, 2, 1))
        return ev
    for bit, code in ((0x8000, 0x16), (0x10000, 0x18), (0x20000, 0x17), (0x40000, 0x15)):
        if up(bit):
            return [(f, 0x14, 0x1a if code == CODE[b] else code)]
    return []


def air_after_route(dest):
    """the six items the lung setter sets after a route button: {item: shown}"""
    if dest == 0x1a:
        return {1: False, 2: False, 0x1c: True, 0x1d: False, 0x17: True, 0x1b: False}
    return {1: True, 2: False, 0x1c: False, 0x1d: False, 0x17: False, 0x1b: False}


def water_setter(b, new, old):
    """exe:004327e0: the events of the pump setter"""
    def up(bit):
        return bool(new & bit) and not (old & bit)
    f = PUMP[b]
    if up(2):
        return [(f, 2, 0)]
    if up(4):
        return [(f, 2, 1)]
    for bit, code in ((0x8000, 0x16), (0x10000, 0x18), (0x20000, 0x17), (0x40000, 0x15)):
        if up(bit):
            return [(f, 0x14, code)]
    return []


def gauge_percent(v, mn, mx):
    """ftol(((v - min) * 100 / (max - min)) * 151.0 * 0.01)"""
    frac = ((v - mn) * 100.0) / (mx - mn)
    return int(frac * 151.0 * 0.01)


def clamp(v, mn, mx):
    if v < mn:
        v = mn
    if mx < v:
        v = mx
    return v


# the gauges of the panels: (slot, feature kind, parameter id, min, max); kind 'self' = the lung / the tank, 'pump' = the pump (water slot 3)
AIR_GAUGES = [(0, 0x1e, -10.0, 40.0), (1, 0x22, 0.0, 100.0), (2, 0x23, 0.0, 100.0), (3, 0x26, 0.0, 100.0), (4, 0x1c, 0.0, 100.0), (5, 0x1b, 0.0, 100.0)]
WATER_GAUGES = [(0, 0x2b, 0.0, 100.0), (1, 0x2c, 0.0, 100.0), (2, 0x28, -10.0, 40.0), (3, 0x2b, 0.0, 100.0)]
