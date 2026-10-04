#!/usr/bin/env python3
"""Analyse a move sweep (tools/qa/sweep.py): results per move and hitbox
checks against each donor's own move. sweep.py runs this itself; by hand:

  analyze.py TAG [--csv out.csv] [--all]      (TAG: a folder in build-qa/sweep)

A borrowed move's hitboxes should be the donor's, sized by the borrow scale:
radius = donor radius * scale, position (relative to the fighter, facing
right) = donor position * scale, same damage. Hitboxes are paired in order of
appearance per hitbox slot.
"""
from __future__ import annotations
import argparse
import collections
import math
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
QA = ROOT / 'build-qa' / 'sweep'

CK = ['Falcon', 'DK', 'Fox', 'G&W', 'Kirby', 'Bowser', 'Link', 'Luigi', 'Mario', 'Marth', 'Mewtwo', 'Ness',
      'Peach', 'Pikachu', 'ICs', 'Puff', 'Samus', 'Yoshi', 'Zelda', 'Sheik', 'Falco', 'YLink', 'Doc', 'Roy',
      'Pichu', 'Ganon']
NORMALS = ['jab', 'dash', 'ftilt', 'utilt', 'dtilt', 'fsmash', 'usmash', 'dsmash', 'fthrow', 'bthrow', 'uthrow',
           'dthrow']
SPECIALS = ['neutralB', 'sideB', 'upB', 'downB']
AERIALS = ['nair', 'fair', 'bair', 'uair', 'dair']


def load(logdir):
    matches = {}          # m -> (R, D, nsteps)
    res = {}              # (m, s) -> dict
    hits = collections.defaultdict(list)  # (m, s) -> [(f, ms, i, r, x, y, dmg)]
    scale = {}
    mscale = {}           # (m, s) -> port 1 model scale
    crashes = {}          # (m, s) -> text
    for f in sorted(Path(logdir).glob('*.log')):
        for ln in f.read_text(errors='replace').splitlines():
            p = ln.split()
            if len(p) < 2 or p[0] != '[qa]':
                continue
            k = p[1]
            try:
                if k == 'M':
                    matches[int(p[2])] = (int(p[3]), int(p[4]), int(p[5]))
                elif k == 'R':
                    m, s = int(p[2]), int(p[3])
                    res[(m, s)] = dict(res=p[4], frames=int(p[5]), bor=int(p[6]), dmg=float(p[7]),
                                       ms=[int(x) for x in p[8:12]])
                    crashes.pop((m, s), None)
                elif k == 'H':
                    m, s = int(p[2]), int(p[3])
                    if len(p) < 17:
                        continue  # older log format
                    # rf ms slot r x y dmg part boff anc ax ay af
                    hits[(m, s)].append((int(p[4]), int(p[5]), int(p[6]), float(p[7]), float(p[8]), float(p[9]),
                                         float(p[10]), int(p[11]), float(p[12]), int(p[13]), float(p[14]),
                                         float(p[15]), float(p[16])))
                elif k == 'S':
                    # A step that ran again (a rerun) replaces the earlier one.
                    hits[(int(p[2]), int(p[3]))] = []
                    if len(p) > 7:
                        mscale[(int(p[2]), int(p[3]))] = float(p[7])
                elif k == 'B':
                    scale[(int(p[2]), int(p[3]))] = float(p[4])
                elif k == 'X':
                    if p[2] == 'boot':
                        continue
                    m, s = int(p[2]), int(p[3])
                    crashes[(m, s)] = ' '.join(p[4:])
            except (ValueError, IndexError):
                pass
    return matches, res, hits, scale, crashes, mscale


def special_table():
    """character -> [(slot, name)] as qa_moves.c's special_of picks them."""
    import re
    out = collections.defaultdict(dict)
    src = (ROOT / 'src/engine/special_catalog_data.c').read_text()
    for m in re.finditer(r'\{ (\d+), (\d+), (\d+), (\d+), "([^"]*)" \}', src):
        i, ch, _, slot, name = int(m.group(1)), int(m.group(2)), m.group(3), int(m.group(4)), m.group(5)
        if i in (17, 32, 80):
            continue
        out[ch].setdefault(slot, name)
    return {ch: [d[k] for k in sorted(d)] for ch, d in out.items()}


SPECIAL_NAMES = None


def step_names(from_ck, matches, res):
    global SPECIAL_NAMES
    if SPECIAL_NAMES is None:
        SPECIAL_NAMES = special_table()
    return list(NORMALS) + AERIALS + ['B:' + n for n in SPECIAL_NAMES.get(from_ck, [])]


# Body heights in skeleton units (src/engine/anim_scale.c body_size: top of
# the hurtboxes over model scale), by CharacterKind.
BODY = dict(zip(CK, [19.19, 17.09, 16.46, 13.06, 10.57, 32.59, 14.86, 12.96, 13.23, 16.67, 18.75, 13.26, 15.84, 13.22, 11.46, 13.20, 21.17, 16.02, 14.89, 12.71, 16.89, 15.64, 13.27, 17.36, 19.50, 20.27]))


def keep(hs, name):
    """The hitboxes of the move itself: not the grab before a throw, nor an
    attack after it."""
    if name.endswith('throw'):
        return [h for h in hs if 219 <= h[1] <= 222 or h[1] >= 341]
    return [h for h in hs if not 212 <= h[1] <= 218]


def region(part):
    """Body parts that stand in for one another when a fighter lacks one
    (special_runtime.c Rogue_AbilityFallbackJoint): a hand and its fingers,
    the hip and waist, the neck and head. Other parts are their own."""
    if part is None or part < 0:
        return None
    for lo, hi in ((21, 32), (39, 50), (4, 5), (33, 34)):
        if lo <= part <= hi:
            return lo
    return part


def compare(nat, bor, s, world, body):
    """Problems of a borrowed move's hitboxes against the donor's own, paired
    by motion, animation frame and hitbox slot.
    s: borrow scale (skeleton units); world: s times the model scale ratio;
    body: the recipient's height in world units."""
    def index(hs):
        d = {}
        for h in hs:
            d.setdefault((h[1], round(h[12]), h[2]), h)
        return d
    n, b = index(nat), index(bor)
    variant = False
    if n and b and not set(n) & set(b):
        # The same move in another of its states: Ice Climbers' Squall
        # Hammer without Nana, a different Judgment number. Pair by frame.
        def loose(hs):
            d = {}
            for h in hs:
                d.setdefault((0, round(h[12]), h[2]), h)
            return d
        n, b, variant = loose(nat), loose(bor), True
    if n and not b:
        return ['no hitboxes (donor has %d active frames)' % len(n)], 99.0
    probs = []
    worst = 0.0
    shared = sorted(set(n) & set(b))
    for key in shared:
        a, c = n[key], b[key]
        # Radii are in world units (anim_scale.c Rogue_HitboxScale); a
        # throw's hitboxes never shrink below the donor's.
        r_exp = a[3] * (max(world, 1.0) if 219 <= key[0] <= 222 else world)
        if r_exp > 0 and abs(c[3] / r_exp - 1) > 0.15:
            probs.append((0, f'slot{key[2]} f{key[1]} radius {c[3]:.2f} expected {r_exp:.2f}'))
        rw = max(c[3], 1.0)
        if a[9] >= 0 and region(a[9]) == region(c[9]):
            ex, ey = (a[4] - a[10]) * world, (a[5] - a[11]) * world
            gx, gy = c[4] - c[10], c[5] - c[11]
            rel = f'from part {a[9]}'
            tol = max(1.5 * rw, 0.4 * body)
        else:
            ex, ey = a[4] * world, a[5] * world
            gx, gy = c[4], c[5]
            rel = f'from fighter (parts {c[9]} vs donor {a[9]})'
            tol = max(1.5 * rw, 0.6 * body)
        err = math.hypot(gx - ex, gy - ey)
        worst = max(worst, err / body)
        if err > tol:
            probs.append((err / body, f'slot{key[2]} ms{key[0]} f{key[1]} at ({gx:.1f},{gy:.1f}) expected ({ex:.1f},{ey:.1f}) {rel}'))
        if not variant and a[6] > 2 and abs(c[6] / a[6] - 1) > 0.4:
            probs.append((0, f'slot{key[2]} damage {c[6]} expected {a[6]}'))
    # Active frames in total (a state may differ: a borrowed Peach parasol
    # floats in the common special fall, Peach in her own parasol fall).
    fn, fb = len({h[0] for h in nat}), len({h[0] for h in bor})
    # Both counts low: a move whose hitboxes end when they connect (a grab,
    # a dive that catches) can have fewer frames on one side.
    if n and fb < 0.5 * fn and len(shared) < 0.5 * len(n):
        probs.append((9, f'hitboxes active {fb} of the donor\'s {fn} frames'))
    probs.sort(key=lambda x: -x[0])
    if variant and probs:
        probs.insert(0, (99, 'played in another state than the donor\'s own (e.g. Squall Hammer without Nana)'))
    return [p[1] for p in probs], worst


def analyze(logdir):
    """[(match, step, recipient, donor, move, result, note)], Counter of results."""
    matches, res, hits, scale, crashes, mscale = load(logdir)
    counts = collections.Counter()
    rows = []
    for m, (R, D, n) in sorted(matches.items()):
        donor = R if D < 0 else D
        names = step_names(donor, matches, res)
        for s in range(n):
            name = names[s] if s < len(names) else f'step{s}'
            key = (m, s)
            r = res.get(key)
            crash = crashes.get(key)
            status = crash.split()[0] if crash else (r['res'] if r else 'MISSING')
            note = ''
            if crash:
                note = crash[:300]
            if D >= 0 and r and status in ('OK', 'NOGRAB') and (donor, s) in res:
                nat = keep(hits.get((donor, s), []), name)
                bor = keep(hits.get(key, []), name)
                sc = scale.get(key, 1.0)
                msr = mscale.get(key, 1.0)
                msd = mscale.get((donor, s), 1.0)
                probs, worst = compare(nat, bor, sc, sc * msr / msd, BODY[CK[R]] * msr)
                if probs:
                    variant = probs[0].startswith('played in another state')
                    status = ('VARIANT' if variant else 'HITBOX') if status == 'OK' else status
                    note = '; '.join(probs[:4])
            if D >= 0 and r and r['res'] == 'OK' and (donor, s) in res and res[(donor, s)]['res'] == 'OK':
                if r['dmg'] == 0 and res[(donor, s)]['dmg'] > 0 and status == 'OK':
                    status = 'NOHIT'
                    note = f"dummy took 0% (donor's own move: {res[(donor, s)]['dmg']}%)"
            counts[status] += 1
            rows.append((m, s, CK[R], CK[D] if D >= 0 else '-', name, status, note))
    return rows, counts


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('tag')
    ap.add_argument('--csv')
    ap.add_argument('--all', action='store_true')
    a = ap.parse_args()
    rows, counts = analyze(QA / a.tag / 'logs')
    print('results:', dict(counts))
    for row in rows:
        if a.all or row[5] != 'OK':
            print('%4d %2d %-8s %-8s %-9s %-8s %s' % row)
    if a.csv:
        import csv
        with open(a.csv, 'w', newline='') as f:
            w = csv.writer(f)
            w.writerow(['match', 'step', 'recipient', 'donor', 'move', 'result', 'note'])
            w.writerows(rows)


if __name__ == '__main__':
    main()
