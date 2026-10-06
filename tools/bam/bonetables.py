"""The bone tables borrowed moves are posed and placed with.

Generated at build time from the player's own disc into
build/generated/engine/bone_tables.h and .inc (never committed: data from the
game's files). `bam.py gen-tables` writes it on its own.

Read from the disc:
    PlCo.dat       body part <-> joint per fighter, and the joint-skip
                   table (Kirby's copy-ability hats are spliced into his
                   joint list, so his file's joints are numbered past them)
    Pl<Xx>Nr.dat   each fighter's skeleton and meshes
    Pl<Xx>.dat     each fighter's move scripts (which bones hitboxes use)

Tables written (C: bone_tables.h declares them, src/engine/bone_tables.c holds them):
    bam_rest_world / bam_rest_extra*   rest rotation of every body part
        (and the finger parts), so a borrowed animation can be retargeted
        from the donor's bone orientations to the borrower's
    bam_part_parent / bam_part_mid     each part's parent part, and the
        rest rotation of body-less joints in between
    bam_prop                             body-less bones (swords, tails,
        props) that hitboxes ride on, rebuilt on the borrower
    bam_weapon                           held items drawn for a donor's
        weapon (Beam Sword, hammer, parasol)
    bam_mesh / _show / _hide             the donor's own meshes drawn on
        the borrower for rebuilt bones, and when
    bam_chain                            donor joints posed through the
        donor's own skeleton from the root (Mr. Game & Watch)

Character-specific choices (which weapons, extra meshes, root chains) are in
donor_notes.py.
"""
import math
import struct
from pathlib import Path

from .hsd import Archive, joint_walk
from .donor_notes import KINDS, KIND_NAMES, WEAPONS, ITEMS, ON_BONE, EXTRA_PARTS, ROOT_CHAINS, CORE_PARTS, AS_PROPS

KIND_COUNT = 27
PART_COUNT = 53
# Body parts that are retargeted: every body and limb bone, both hands,
# neck, head, throw and second trans node; not the finger bones.
PARTS = list(range(0, 22)) + list(range(32, 40)) + [50, 51, 52]
# Finger parts: not retargeted, but props and hitboxes hanging from them
# need their rest rotation and offset to move onto another bone.
EXTRA_PARTS_REST = [22, 25, 31, 40, 43, 49]
INVALID = 0xFF

def _matrix(rx, ry, rz):
    """HSD_MtxSRT rotation (Rz * Ry * Rx), row-major 3x3."""
    sx, cx, sy, cy, sz, cz = math.sin(rx), math.cos(rx), math.sin(ry), math.cos(ry), math.sin(rz), math.cos(rz)
    return [[cz * cy, cz * sx * sy - cx * sz, cz * cx * sy + sx * sz],
            [sz * cy, sz * sx * sy + cx * cz, sz * cx * sy - sx * cz],
            [-sy, cy * sx, cy * cx]]


def _mul(a, b):
    return [[sum(a[i][k] * b[k][j] for k in range(3)) for j in range(3)] for i in range(3)]


def _quat(m):
    """Unit quaternion (x, y, z, w) of a rotation matrix."""
    t = m[0][0] + m[1][1] + m[2][2]
    if t > 0:
        s = math.sqrt(t + 1.0) * 2
        q = ((m[2][1] - m[1][2]) / s, (m[0][2] - m[2][0]) / s, (m[1][0] - m[0][1]) / s, 0.25 * s)
    elif m[0][0] > m[1][1] and m[0][0] > m[2][2]:
        s = math.sqrt(1.0 + m[0][0] - m[1][1] - m[2][2]) * 2
        q = (0.25 * s, (m[0][1] + m[1][0]) / s, (m[0][2] + m[2][0]) / s, (m[2][1] - m[1][2]) / s)
    elif m[1][1] > m[2][2]:
        s = math.sqrt(1.0 + m[1][1] - m[0][0] - m[2][2]) * 2
        q = ((m[0][1] + m[1][0]) / s, 0.25 * s, (m[1][2] + m[2][1]) / s, (m[0][2] - m[2][0]) / s)
    else:
        s = math.sqrt(1.0 + m[2][2] - m[0][0] - m[1][1]) * 2
        q = ((m[0][2] + m[2][0]) / s, (m[1][2] + m[2][1]) / s, 0.25 * s, (m[1][0] - m[0][1]) / s)
    n = math.sqrt(sum(v * v for v in q)) or 1.0
    q = tuple(v / n for v in q)
    return q if q[3] >= 0 else tuple(-v for v in q)


# A skipped joint (no joint in the file at that number): no parent, no
# rest transform. The game splices one in later (Kirby's hats).
SKIPPED = (-1, (0.0, 0.0, 0.0), (0.0, 0.0, 0.0), (1.0, 1.0, 1.0))


def skip_table(common, kind):
    """Joint numbers the game leaves empty for this fighter when it builds
    its joint list (ftParts_8007506C: Kirby's hat joints)."""
    data = common.roots['ftLoadCommonData']
    tables = common.ptr(data + 20)
    if tables is None:
        return set()
    entry = common.ptr(tables + 4 * kind)
    if entry is None:
        return set()
    items, count = common.ptr(entry), common.u32(entry + 4)
    return {common.data[items + 4 * i] for i in range(count)} if items is not None else set()


def fighter_numbering(archive, skips=()):
    """Walk index of each of the model's joints -> the fighter's joint
    number (what parts tables, hitboxes and animations use)."""
    walk = joint_walk(archive, archive.root('_joint'))
    number, n = [], 0
    for _ in walk:
        while n in skips:
            n += 1
        number.append(n)
        n += 1
    return walk, number


def _joints(archive, skips=()):
    """[(parent joint, rot, pos, scale)] by the fighter's joint number;
    SKIPPED where the game leaves a number empty."""
    walk, number = fighter_numbering(archive, skips)
    out = [SKIPPED] * ((max(number) + 1) if number else 0)
    for w, (j, parent) in enumerate(walk):
        out[number[w]] = (number[parent] if parent >= 0 else -1, archive.unpack('3f', j + 0x14),
                          archive.unpack('3f', j + 0x2C), archive.unpack('3f', j + 0x20))
    return out


def skeleton(joints):
    """World rest rotation matrix of every joint (None where skipped)."""
    worlds = []
    for j, entry in enumerate(joints):
        if entry is SKIPPED:
            worlds.append(None)
            continue
        parent, rot = entry[0], entry[1]
        base = worlds[parent] if parent >= 0 and worlds[parent] is not None else [[1, 0, 0], [0, 1, 0], [0, 0, 1]]
        worlds.append(_mul(base, _matrix(*rot)))
    return worlds


def prop_bones(joints, joint_part):
    """Bones with no body part (sword, cape, cannon...): for each, the nearest
    ancestor with a body part and the rest transform from that ancestor to
    the bone, so a borrowed move can rebuild the bone on a recipient's own
    body part. -> [(joint, part, quaternion, translation)]"""
    out = []
    for j, (parent, _rot, _pos, _scl) in enumerate(joints):
        if joint_part.get(j) is not None:
            continue
        chain, a = [], j
        while a >= 0 and joint_part.get(a) is None:
            chain.append(a)
            a = joints[a][0]
        if a < 0:
            continue
        full = rot = [[1, 0, 0], [0, 1, 0], [0, 0, 1]]
        t = [0.0, 0.0, 0.0]
        for c in reversed(chain):
            _p, angles, pos, scl = joints[c]
            t = [t[i] + sum(full[i][k] * pos[k] for k in range(3)) for i in range(3)]
            full = _mul(full, _matrix(*angles))
            full = [[full[i][k] * scl[k] for k in range(3)] for i in range(3)]
            rot = _mul(rot, _matrix(*angles))
        out.append((j, joint_part[a], _quat(rot), t))
    return out


# Script command lengths in words: 0x00-0x09 generic, then the fighter
# commands from 0x0A, as the game's own table (ftAction_803C0870) has them.
_GENERIC_WORDS = [1, 1, 1, 1, 1, 2, 1, 2, 1, 1]
_FIGHTER_WORDS = [5, 5, 1, 1, 1, 1, 1, 3, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 3, 1, 1, 1, 7, 4, 1, 1, 1, 1,
                  1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 3, 3, 2, 1, 4]


def _command_bytes(op):
    if op < 0x0A:
        return 4 * _GENERIC_WORDS[op]
    i = op - 0x0A
    return 4 * (_FIGHTER_WORDS[i] if i < len(_FIGHTER_WORDS) else 1)


def hitbox_bones(fighter):
    """{joint: [b_offset, ...]} of every hitbox command on a joint (not a
    common body part) in the fighter's move scripts."""
    out = {}
    for _index, _name, joint, offset in hitbox_list(fighter):
        out.setdefault(joint, []).append(offset)
    return out


def motion_names(fighter):
    """{motion table index: name} (PlXxDat ftData +0x0C)."""
    data = next(o for n, o in fighter.roots.items() if n.startswith('ftData'))
    table = fighter.u32(data + 0x0C)
    out = {}
    for i in range(1024):
        e = table + i * 0x18
        if e + 0x18 > len(fighter.data):
            break
        if e in fighter.relocations:
            at = fighter.u32(e)
            name = fighter.data[at:fighter.data.find(b'\0', at)].decode('ascii', 'replace')
            if '_ACTION_' in name:
                out[i] = name.split('_ACTION_')[1].replace('_figatree', '')
    return out


def hitbox_list(fighter):
    """[(motion index, motion name, joint, b_offset)] of every hitbox on a
    joint (not a common body part) in the fighter's move scripts."""
    names = motion_names(fighter)
    data = next(o for n, o in fighter.roots.items() if n.startswith('ftData'))
    table = fighter.u32(data + 0x0C)
    out = []
    for i in range(1024):
        e = table + i * 0x18
        if e + 0x18 > len(fighter.data):
            break
        if (e + 0x0C) not in fighter.relocations:
            if i > 400 and e not in fighter.relocations:
                break
            continue
        pos, guard = fighter.u32(e + 0x0C), 0
        while pos + 4 <= len(fighter.data) and guard < 600:
            guard += 1
            op = fighter.data[pos] >> 2
            if op == 0:
                break
            if op == 0x0B and pos + 20 <= len(fighter.data):
                w = struct.unpack_from('>5I', fighter.data, pos)
                if not (w[0] >> 10) & 1:
                    def s16(v):
                        return (v - 0x10000 if v & 0x8000 else v) / 256.0
                    out.append((i, names.get(i, ''), (w[0] >> 11) & 0xFF,
                                (s16(w[1] & 0xFFFF), s16(w[2] >> 16), s16(w[2] & 0xFFFF))))
            pos += _command_bytes(op)
    return out


def props(joints, joint_part, bones, forced=()):
    """Body-less bones a move's hitboxes ride on (Marth's sword, Pikachu's
    tail...), with every body-less ancestor up to the body part they hang
    from. -> [(joint, parent joint or None, root part, rot, pos, scale)],
    parents before children."""
    need = set(j for j in forced if 0 <= j < len(joints))
    for b in bones:
        a = b
        while 0 <= a < len(joints) and joint_part.get(a) is None:
            need.add(a)
            a = joints[a][0]
        if a < 0:
            need -= {b}
    out = []
    for j in sorted(need):
        a = joints[j][0] if j in forced else j
        while a >= 0 and joint_part.get(a) is None:
            a = joints[a][0]
        if a < 0:
            continue
        parent = joints[j][0]
        _p, rot, pos, scl = joints[j]
        out.append((j, parent if parent in need else None, joint_part[a], rot, pos, scl))
    return out


def root_chain(joints, wanted):
    """[(joint, parent joint or None, rot, pos, scale)]: the wanted joints
    and every ancestor, parents before children."""
    need = set()
    for j in wanted:
        while 0 <= j < len(joints) and j not in need:
            need.add(j)
            j = joints[j][0]
    return [(j, joints[j][0] if joints[j][0] >= 0 else None, joints[j][1], joints[j][2], joints[j][3])
            for j in sorted(need)]


def _chain_to_part(joints, joint_part, joint):
    """Rest transform (matrix, translation) from the body part a weapon bone
    hangs from to the bone (a body-part weapon bone: from its parent part)."""
    m, t, a = [[1, 0, 0], [0, 1, 0], [0, 0, 1]], [0.0, 0.0, 0.0], joint
    chain = [joint]
    a = joints[joint][0]
    while a >= 0 and joint_part.get(a) is None:
        chain.append(a)
        a = joints[a][0]
    for c in reversed(chain):
        _p, angles, pos, scl = joints[c]
        t = [t[i] + sum(m[i][k] * pos[k] for k in range(3)) for i in range(3)]
        m = _mul(m, _matrix(*angles))
        m = [[m[i][k] * scl[k] for k in range(3)] for i in range(3)]
    return m, t


def weapons(code, joints, joint_part, hits, names):
    """[(joint, item, first motion, last motion, grip, axis, reach)]: where a
    donor's weapon lies (along its hitboxes' axis), where it is held (the
    point on that line nearest the body part it hangs from) and how far the
    farthest hitbox centre is from the grip."""
    out = []
    for item, joint, prefixes in WEAPONS.get(code, ()):
        if prefixes is None:
            lo, hi = 0, 0xFFFF
        else:
            picked = [i for i, name in names.items() if name.startswith(prefixes)]
            if not picked:
                continue
            lo, hi = min(picked), max(picked)
        if item in ON_BONE:
            if joint is not None and joint < len(joints):
                out.append((joint, ITEMS[item], lo, hi, [0.0, 0.0, 0.0], [0.0, 1.0, 0.0], 1.0))
            continue
        sel = [h for h in hits if prefixes is None or h[1].startswith(prefixes)]
        if joint is None:
            counts = {}
            for _i, _n, j, _o in sel:
                if joint_part.get(j) is None:
                    counts[j] = counts.get(j, 0) + 1
            if not counts:
                continue
            joint = max(counts, key=counts.get)
        offsets = [o for _i, _n, j, o in sel if j == joint and any(abs(v) > 1e-3 for v in o)]
        if not offsets or joint >= len(joints):
            continue
        sx = [sum(o[k] for o in offsets) for k in range(3)]
        n = math.sqrt(sum(v * v for v in sx))
        if n < 1e-3:
            continue
        axis = [v / n for v in sx]
        m, t = _chain_to_part(joints, joint_part, joint)
        inv = [[m[k][i] / max(1e-6, sum(m[r][i] ** 2 for r in range(3))) for k in range(3)] for i in range(3)]
        h = [-sum(inv[i][k] * t[k] for k in range(3)) for i in range(3)]
        d = sum(h[k] * axis[k] for k in range(3))
        head = max(sum(o[k] * axis[k] for k in range(3)) for o in offsets)
        out.append((joint, ITEMS[item], lo, hi, [d * v for v in axis], axis, max(0.5, head - d)))
    return out


def _model_meshes(archive, skips=()):
    """[(owner joint, [(kind, {joint: weight})] per PObj)] for every DObj, in
    the order the fighter numbers them (joint order, then DObj chain).
    Joints are the fighter's joint numbers."""
    walk, number = fighter_numbering(archive, skips)
    joints = [j for j, _parent in walk]
    index = {j: number[w] for w, j in enumerate(joints)}
    out = []
    for w, j in enumerate(joints):
        i = number[w]
        if archive.u32(j + 4) & 0x4020:
            continue
        d, seen = archive.u32(j + 16), set()
        while d and d not in seen:
            seen.add(d)
            pobjs, po, pseen = [], archive.u32(d + 12), set()
            while po and po not in pseen:
                pseen.add(po)
                flags = struct.unpack_from('>H', archive.data, po + 0xC)[0]
                kind = (flags >> 12) & 3
                u = archive.u32(po + 0x14)
                weights = {}
                if kind == 2 and u:
                    k = 0
                    while archive.u32(u + 4 * k):
                        e, m = archive.u32(u + 4 * k), 0
                        while archive.u32(e + 8 * m):
                            target = index.get(archive.u32(e + 8 * m), -1)
                            weights[target] = weights.get(target, 0.0) + archive.unpack('f', e + 8 * m + 4)[0]
                            m += 1
                        k += 1
                elif kind == 0:
                    weights[index.get(u, i) if u else i] = 1.0
                pobjs.append((kind, weights))
                po = archive.u32(po + 4)
            out.append((i, pobjs))
            d = archive.u32(d + 4)
    return out


def _main_dobjs(fighter):
    """DObjs the game draws in play: set 1 (the full-detail model), first
    variant of each group (group 0 is the body; later groups hold a held
    sword or hammer and its alternatives), and DObjs in no set at all."""
    data = next(o for n, o in fighter.roots.items() if n.startswith('ftData'))
    x8 = fighter.u32(data + 8)
    count, table = fighter.u32(x8), fighter.u32(x8 + 4)
    listed, main = set(), set()
    for which in range(4):
        if (table + 4 * which) not in fighter.relocations:
            continue
        lookup = fighter.u32(table + 4 * which)
        for group in range(count):
            variants, entries = fighter.u32(lookup + 8 * group), fighter.u32(lookup + 8 * group + 4)
            for v in range(variants):
                n, at = fighter.u32(entries + 8 * v), fighter.u32(entries + 8 * v + 4)
                ids = set(fighter.data[at:at + n])
                listed |= ids
                if which == 1 and v == 0:
                    main |= ids
    return main, listed




def mesh_groups(fighter, model, entries, kit, hits, joint_part, extra=(), names=None, skips=()):
    """Which of the donor's own meshes show a rebuilt bone (a sword, a tail):
    -> ([(group, dobj, pobj mask)], [(group, first motion, last motion, item or 255)],
        [(group, joint collapsed while it shows)]).
    The mask picks the DObj's PObjs to draw (0: all), for a tail that is
    part of a body DObj (Yoshi's).
    A group is one chain of rebuilt bones; it shows in the motions whose
    hitboxes use it, and a weapon group in its weapon's motions."""
    props = {e[0]: e for e in entries}
    root_of = {}
    for joint, parent, _part, *_rest in entries:
        r = joint
        while props[r][1] is not None:
            r = props[r][1]
        root_of[joint] = r
    groups = sorted(set(root_of.values()))
    gid = {r: n for n, r in enumerate(groups)}
    try:
        main, listed = _main_dobjs(fighter)
    except Exception:
        return [], [], []
    extra_groups, hides = {}, []
    for joint, prefixes in extra:
        if joint not in root_of:
            continue
        picked = [i for i, name in (names or {}).items() if name.startswith(prefixes)]
        if picked:
            extra_groups[gid[root_of[joint]]] = (joint, min(picked), max(picked))
    meshes = []
    all_meshes = _model_meshes(model, skips)
    for g, (joint, _lo, _hi) in extra_groups.items():
        for d, (_owner, pobjs) in enumerate(all_meshes):
            if len(pobjs) > 16:
                continue
            mask = sum(1 << i for i, (_k, weights) in enumerate(pobjs) if joint in weights)
            if not mask:
                continue
            meshes.append((g, d, 0 if mask == (1 << len(pobjs)) - 1 else mask))
            for i, (_k, weights) in enumerate(pobjs):
                if mask & (1 << i):
                    hides += [(g, j) for j in weights if root_of.get(j) != root_of[joint] and (g, j) not in hides]
    taken = {d for _g, d, _m in meshes}
    for d, (_owner, pobjs) in enumerate(all_meshes):
        if d > 0xFFFF or d in taken or (d in listed and d not in main) or not pobjs:
            continue
        best, mask = None, 0
        for g, r in enumerate(groups):
            on = []
            for _kind, weights in pobjs:
                total = sum(weights.values()) or 1.0
                on.append(sum(w for j, w in weights.items() if root_of.get(j) == r) / total >= 0.5)
            if all(on):
                best, mask = g, 0
                break
            if any(on) and len(on) <= 16 and best is None:
                best, mask = g, sum(1 << i for i, v in enumerate(on) if v)
        if best is not None:
            meshes.append((best, d, mask))
    # Only appendages the recipient has no counterpart of: those hanging
    # from the hips or torso (tails), and held weapons. A rebuilt foot or
    # hand (Mewtwo's down-air foot) would just be a second, floating limb.
    weapon_groups = {gid[root_of[j]] for j, *_rest in (kit or ()) if j in root_of}
    core = {gid[r] for r in groups if props[r][2] in CORE_PARTS}
    meshes = [(g, d, m) for g, d, m in meshes if g in weapon_groups or g in core or g in extra_groups]
    used = {g for g, _d, _m in meshes}
    shows = []
    weapon_of = {}
    for joint, item, lo, hi, *_rest in kit or ():
        if joint in root_of and gid[root_of[joint]] in used:
            weapon_of[gid[root_of[joint]]] = item
            shows.append((gid[root_of[joint]], lo, hi, item))
    motions = {}
    for motion, _name, joint, _o in hits:
        if joint in root_of and gid[root_of[joint]] in used and gid[root_of[joint]] not in weapon_of:
            motions.setdefault(gid[root_of[joint]], set()).add(motion)
    for g, (_joint, lo, hi) in extra_groups.items():
        if g in used:
            shows.append((g, lo, hi, 255))
    for g, ms in motions.items():
        ms = sorted(ms)
        start = prev = ms[0]
        for m in ms[1:] + [None]:
            if m is not None and m == prev + 1:
                prev = m
                continue
            shows.append((g, start, prev, 255))
            if m is not None:
                start = prev = m
    return meshes, shows, hides


class Disc:
    """The game files the tables are read from, each parsed once."""

    def __init__(self, read):
        self.read = read
        self._arc = {}
        self.common = self.archive('PlCo.dat')
        data = self.common.roots['ftLoadCommonData']
        self._tables = self.common.ptr(data + 16)
        self._joints = {}

    def archive(self, name):
        if name not in self._arc:
            self._arc[name] = Archive(self.read(name))
        return self._arc[name]

    def parts(self, kind):
        """(joint -> part, part -> joint, joint count) of a fighter, or None."""
        entry = self.common.ptr(self._tables + 4 * kind)
        if entry is None:
            return None
        c = self.common
        j2p, p2j, count = c.ptr(entry), c.ptr(entry + 4), c.u32(entry + 8)
        return list(c.data[j2p:j2p + count]), list(c.data[p2j:p2j + PART_COUNT]), count

    def skips(self, kind):
        return skip_table(self.common, kind)

    def joints(self, kind):
        """The fighter's skeleton by joint number (bones and rest transforms)."""
        key = (KINDS[kind], frozenset(self.skips(kind)))
        if key not in self._joints:
            self._joints[key] = _joints(self.archive(f'Pl{KINDS[kind]}Nr.dat'), key[1])
        return self._joints[key]


def rest_quaternions(disc, parts=PARTS):
    """{kind: [world rest rotation quaternion or None per part]}"""
    out = {}
    for kind in KINDS:
        table = disc.parts(kind)
        if table is None:
            continue
        worlds = skeleton(disc.joints(kind))
        row = []
        for part in parts:
            joint = table[1][part]
            ok = joint != INVALID and joint < len(worlds) and worlds[joint] is not None
            row.append(_quat(worlds[joint]) if ok else None)
        out[kind] = row
    return out


def finger_rest(disc):
    """{kind: [(local rot, local pos) or None per finger part]}: the files'
    own rest transform of each finger joint."""
    out = {}
    for kind in KINDS:
        table = disc.parts(kind)
        if table is None:
            continue
        joints = disc.joints(kind)
        row = []
        for part in EXTRA_PARTS_REST:
            joint = table[1][part]
            ok = joint != INVALID and joint < len(joints) and joints[joint] is not SKIPPED
            row.append((joints[joint][1], joints[joint][2]) if ok else None)
        out[kind] = row
    return out


def part_parents(disc):
    """{kind: ([body part of the nearest ancestor joint that has one, per part],
    {part: rest rotation of the body-less joints in between, when not none})}:
    the donor's body hierarchy, so a recipient missing a torso bone (Mario has
    no BustN) can fold the donor's rotation of it into the next bone."""
    out = {}
    for kind in KINDS:
        table = disc.parts(kind)
        if table is None:
            continue
        j2p, p2j, count = table
        joints = disc.joints(kind)
        row, mids = [], {}
        for part in range(PART_COUNT):
            joint = p2j[part]
            parent = INVALID
            between = []
            if joint != INVALID and joint < len(joints):
                a = joints[joint][0]
                while 0 <= a < len(joints):
                    if a < count and j2p[a] != INVALID:
                        parent = j2p[a]
                        break
                    between.append(a)
                    a = joints[a][0]
            row.append(parent)
            m = [[1, 0, 0], [0, 1, 0], [0, 0, 1]]
            for a in reversed(between):
                m = _mul(m, _matrix(*joints[a][1]))
            q = _quat(m)
            if parent != INVALID and abs(q[3]) < 0.9999:
                mids[part] = q
        out[kind] = (row, mids)
    return out


def prop_tables(disc, log=print):
    """{kind: (props, weapons, (meshes, shows, hides), root chain)} from the
    fighters' models and move scripts."""
    out, moves = {}, {}
    for kind, code in KINDS.items():
        table = disc.parts(kind)
        if table is None:
            continue
        j2p, _p2j, count = table
        joint_part = {j: j2p[j] for j in range(count) if j2p[j] != INVALID and j not in AS_PROPS.get(code, ())}
        if code not in moves:
            fighter = disc.archive(f'Pl{code}.dat')
            moves[code] = (hitbox_list(fighter), motion_names(fighter))
        hits, names = moves[code]
        joints = disc.joints(kind)
        bones = {}
        for _i, _n, j, o in hits:
            bones.setdefault(j, []).append(o)
        for j, _prefixes in EXTRA_PARTS.get(code, ()):
            bones.setdefault(j, [])
        forced = [j for _item, j, _p in WEAPONS.get(code, ()) if j is not None and joint_part.get(j) is not None]
        entries = props(joints, joint_part, bones, forced)
        kit = weapons(code, joints, joint_part, hits, names)
        meshes = mesh_groups(disc.archive(f'Pl{code}.dat'), disc.archive(f'Pl{code}Nr.dat'), entries, kit, hits,
                             joint_part, EXTRA_PARTS.get(code, ()), names, disc.skips(kind))
        out[kind] = (entries, kit, meshes, root_chain(joints, ROOT_CHAINS.get(code, ())))
    return out


def _s16(v, scale):
    return max(-32767, min(32767, int(round(v * scale))))


def _cells(values):
    return '{ ' + ', '.join(str(v) for v in values) + ' }'


def write_include(destination, quats, fingers, parents, prop_data, note):
    names = KIND_NAMES
    lines = [f'/* {note} */',
             f'#define BAM_REST_KINDS {KIND_COUNT}',
             f'#define BAM_REST_PARTS {len(PARTS)}',
             'static const unsigned char bam_rest_part[BAM_REST_PARTS] = ' + _cells(PARTS) + ';',
             '/* World rest rotation quaternion (x, y, z, w) x 32767; all zero = no such bone. */',
             'static const short bam_rest_world[BAM_REST_KINDS][BAM_REST_PARTS][4] = {']
    for kind in range(KIND_COUNT):
        row = quats.get(kind) or [None] * len(PARTS)
        lines.append('    { ' + ', '.join(_cells((0, 0, 0, 0) if q is None else (int(round(v * 32767)) for v in q))
                                         for q in row) + f' }}, /* {names[kind]} */')
    lines += ['};']
    world, local = fingers
    lines += ['/* The same for the finger parts props and hitboxes hang from (not retargeted). */',
              f'#define BAM_REST_EXTRA {len(EXTRA_PARTS_REST)}',
              'static const unsigned char bam_rest_extra_part[BAM_REST_EXTRA] = ' + _cells(EXTRA_PARTS_REST) + ';',
              'static const short bam_rest_extra[BAM_REST_KINDS][BAM_REST_EXTRA][4] = {']
    for kind in range(KIND_COUNT):
        row = world.get(kind) or [None] * len(EXTRA_PARTS_REST)
        lines.append('    { ' + ', '.join(_cells((0, 0, 0, 0) if q is None else (int(round(v * 32767)) for v in q))
                                         for q in row) + f' }}, /* {names[kind]} */')
    lines += ['};', '/* Their own (local) rest rotation, as the files store it (rad x 4096). */',
              'static const short bam_rest_extra_rot[BAM_REST_KINDS][BAM_REST_EXTRA][3] = {']
    for kind in range(KIND_COUNT):
        row = local.get(kind) or [None] * len(EXTRA_PARTS_REST)
        lines.append('    { ' + ', '.join(_cells((0, 0, 0) if r is None else (_s16(v, 4096) for v in r[0]))
                                         for r in row) + f' }}, /* {names[kind]} */')
    lines += ['};', '/* Their rest position in their parent bone (x 256). */',
              'static const short bam_rest_extra_pos[BAM_REST_KINDS][BAM_REST_EXTRA][3] = {']
    for kind in range(KIND_COUNT):
        row = local.get(kind) or [None] * len(EXTRA_PARTS_REST)
        lines.append('    { ' + ', '.join(_cells((0, 0, 0) if r is None else (_s16(v, 256) for v in r[1]))
                                         for r in row) + f' }}, /* {names[kind]} */')
    lines += ['};']
    lines += ['/* Body part of each part\'s nearest ancestor with one (0xFF: none); all 0xFF = unknown. */',
              f'#define BAM_PART_COUNT {PART_COUNT}',
              'static const unsigned char bam_part_parent[BAM_REST_KINDS][BAM_PART_COUNT] = {']
    mids = []
    for kind in range(KIND_COUNT):
        row, mid = (parents or {}).get(kind) or ([INVALID] * PART_COUNT, {})
        lines.append('    { ' + ', '.join(str(v) for v in row) + ' },')
        for part, q in sorted(mid.items()):
            mids.append('    { %d, %d, { %s } },' % (kind, part, ', '.join(str(_s16(v, 32767)) for v in q)))
    lines += ['};',
              '/* Rest rotation of body-less joints between a part and its parent part (none if absent). */',
              'typedef struct BamPartMid { unsigned char kind, part; short q[4]; } BamPartMid;',
              f'#define BAM_PART_MID_COUNT {len(mids)}',
              'static const BamPartMid bam_part_mid[BAM_PART_MID_COUNT + 1] = {'] + mids + ['    { 255, 255, { 0 } },', '};']
    # Body-less bones borrowed hitboxes ride on, rebuilt on the recipient.
    lines += ['/* Body-less bones (sword, tail...) that move hitboxes ride on: kind, joint, parent entry (0xFF: hangs',
              ' * from the body part), body part, rest rotation (rad x 4096), position (x 256), scale (x 4096). */',
              'typedef struct BamProp { unsigned char kind, joint, parent, part; short rot[3], pos[3], scale[3]; } BamProp;']
    rows, arms = [], []
    meshes, shows, hides, chains = [], [], [], []
    for kind in sorted(prop_data or {}):
        entries, kit, (mesh, show, hide), chain = prop_data[kind]
        at = {}
        for joint, parent, rot, pos, scl in chain:
            if joint > 255:
                continue
            at[joint] = len(chains)
            chains.append('    { %d, %d, %d, 0, { %s }, { %s }, { %s } },' % (
                kind, joint, at.get(parent, 255) if parent is not None else 255,
                ', '.join(str(_s16(v, 4096)) for v in rot), ', '.join(str(_s16(v, 256)) for v in pos),
                ', '.join(str(_s16(v, 4096)) for v in scl)))
        hides += ['    { %d, %d, %d },' % (kind, g, j) for g, j in hide if j <= 255]
        meshes += ['    { %d, %d, %d, %d },' % (kind, g, d, m) for g, d, m in mesh]
        shows += ['    { %d, %d, %d, 0, %d, %d },' % (kind, g, item, lo, hi) for g, lo, hi, item in show]
        index = {}
        for joint, parent, part, rot, pos, scl in entries:
            if joint > 255 or len(rows) >= 250:
                continue
            index[joint] = len(rows)
            rows.append('    { %d, %d, %d, %d, { %s }, { %s }, { %s } },' % (
                kind, joint, index.get(parent, 255) if parent is not None else 255, part,
                ', '.join(str(_s16(v, 4096)) for v in rot), ', '.join(str(_s16(v, 256)) for v in pos),
                ', '.join(str(_s16(v, 4096)) for v in scl)))
        for joint, item, lo, hi, grip, axis, reach in kit or ():
            if joint not in index:
                continue
            # Shortest turn of the item model's long axis (+Y) onto the weapon axis.
            c = axis[1]
            x, y, z, w = axis[2], 0.0, -axis[0], 1.0 + c
            if w < 1e-6:
                x, y, z, w = 1.0, 0.0, 0.0, 0.0
            n = math.sqrt(x * x + y * y + z * z + w * w)
            arms.append('    { %d, %d, %d, 0, %d, %d, { %s }, { %s }, %d },' % (
                kind, index[joint], item, lo, hi, ', '.join(str(_s16(v, 256)) for v in grip),
                ', '.join(str(_s16(v / n, 32767)) for v in (x, y, z, w)), _s16(reach, 256)))
    lines += [f'#define BAM_PROP_COUNT {len(rows)}',
              'static const BamProp bam_prop[BAM_PROP_COUNT + 1] = {'] + rows + ['    { 255, 0, 255, 255, { 0 }, { 0 }, { 0 } },', '};']
    lines += ['/* Borrowed weapons: donor kind, prop entry the weapon lies along, item (0 Beam Sword, 1 Hammer, 2 parasol),',
              ' * donor motions it shows in (first..last), grip point on the prop (x 256), turn (x 32767) from the',
              ' * item model\'s long axis (+Y) onto the weapon, and grip-to-farthest-hitbox reach (x 256). */',
              'typedef struct BamWeapon { unsigned char kind, prop, item, pad; unsigned short first, last;',
              '    short grip[3], align[4], reach; } BamWeapon;',
              f'#define BAM_WEAPON_COUNT {len(arms)}',
              'static const BamWeapon bam_weapon[BAM_WEAPON_COUNT + 1] = {'] + arms + [
              '    { 255, 255, 0, 0, 0, 0, { 0 }, { 0 }, 0 },', '};']
    lines += ['/* The donor\'s own meshes for rebuilt bones (Marth\'s sword, Mewtwo\'s tail): kind, group (one chain',
              ' * of rebuilt bones), DObj index in the donor\'s model, PObjs of it drawn (bit each, 0 all); and when each group shows: kind, group,',
              ' * weapon item it replaces (255: none), first..last donor motion. */',
              'typedef struct BamMesh { unsigned char kind, group; unsigned short dobj, pobjs; } BamMesh;',
              'typedef struct BamMeshShow { unsigned char kind, group, item, pad; unsigned short first, last; } BamMeshShow;',
              f'#define BAM_MESH_COUNT {len(meshes)}',
              'static const BamMesh bam_mesh[BAM_MESH_COUNT + 1] = {'] + meshes + ['    { 255, 255, 0, 0 },', '};',
              f'#define BAM_MESH_SHOW_COUNT {len(shows)}',
              'static const BamMeshShow bam_mesh_show[BAM_MESH_SHOW_COUNT + 1] = {'] + shows + [
              '    { 255, 255, 255, 0, 0, 0 },', '};',
              '/* Donor joints collapsed (drawn at no size) while a mesh group shows: kind, group, joint. */',
              'typedef struct BamMeshHide { unsigned char kind, group, joint; } BamMeshHide;',
              f'#define BAM_MESH_HIDE_COUNT {len(hides)}',
              'static const BamMeshHide bam_mesh_hide[BAM_MESH_HIDE_COUNT + 1] = {'] + hides + [
              '    { 255, 255, 255 },', '};',
              '/* Donor joints posed through the donor\'s own skeleton from its root (ROOT_CHAINS): kind, joint,',
              ' * parent entry (255: the root), rest rotation (rad x 4096), position (x 256), scale (x 4096). */',
              'typedef struct BamChain { unsigned char kind, joint, parent, pad; short rot[3], pos[3], scale[3]; } BamChain;',
              f'#define BAM_CHAIN_COUNT {len(chains)}',
              'static const BamChain bam_chain[BAM_CHAIN_COUNT + 1] = {'] + chains + [
              '    { 255, 0, 255, 0, { 0 }, { 0 }, { 0 } },', '};', '']
    split_include(lines, destination)


def split_include(lines, destination):
    """Write the tables as a header (types, sizes, `extern` declarations;
    `destination` with .h) and the definitions (`destination` itself), so
    one object holds them and every engine file can read them."""
    header, data, i = [], [], 0
    stem = Path(destination).name.split('.')[0].upper()
    header += [lines[0], f'#ifndef BAM_{stem}_H', f'#define BAM_{stem}_H']
    data += [lines[0], f'#include <engine/{Path(destination).with_suffix(".h").name}>']
    i = 1
    while i < len(lines):
        line = lines[i]
        if line.startswith('static const '):
            decl = line[len('static '):]
            name_part = decl.split(' = ')[0]
            header.append('extern ' + name_part + ';')
            body = [decl]
            while not lines[i].startswith('};') and not lines[i].endswith('};'):
                i += 1
                body.append(lines[i])
            data += body
        else:
            header.append(line)
        i += 1
    header.append('#endif')
    Path(destination).with_suffix('.h').write_text('\n'.join(header) + '\n', encoding='utf-8')
    Path(destination).write_text('\n'.join(data) + '\n', encoding='utf-8')


def mesh_dobjs(read):
    """{kind: DObj indices of the donor's model drawn for rebuilt bones}
    (bam_mesh): the meshes the trimmed models keep (parts.py)."""
    out = {}
    for kind, (_entries, _kit, (meshes, _shows, _hides), _chain) in prop_tables(Disc(read)).items():
        if meshes:
            out[kind] = {d for _g, d, _m in meshes}
    return out


NOTE = 'Generated at build time from the local Melee ISO by tools/bam/bonetables.py; not committed or distributed.'


def generate(read, destination, log=print):
    """Write the tables from a disc (`read(name)` -> file bytes). Returns the
    number of body parts with rest data."""
    disc = Disc(read)
    quats = rest_quaternions(disc)
    fingers = (rest_quaternions(disc, EXTRA_PARTS_REST), finger_rest(disc))
    write_include(destination, quats, fingers, part_parents(disc), prop_tables(disc, log), NOTE)
    return sum(1 for row in quats.values() for q in row if q)
