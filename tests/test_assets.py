"""Host tests for the tools that read the player's game files (no ISO needed):
python3 -m unittest discover tests"""
import struct
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))

from bam.hsd import Archive, joint_walk  # noqa: E402
from bam import bonetables, parts, fingerprint  # noqa: E402
from bam.project import load_fixes  # noqa: E402


def archive(data: bytes, relocs, roots):
    """An HSD archive from a data block, pointer field offsets and {name: offset}."""
    data = bytes(data) + b'\0' * (-len(data) % 4)
    names = b''.join(n.encode() + b'\0' for n in roots)
    table, at = b'', 0
    for n, off in roots.items():
        table += struct.pack('>II', off, at)
        at += len(n) + 1
    body = data + b''.join(struct.pack('>I', r) for r in relocs) + table + names
    size = 0x20 + len(body)
    return struct.pack('>5I', size, len(data), len(relocs), len(roots), 0) + b'\0' * 12 + body


def joints_block(tree):
    """Joint structs (0x40 bytes: flags +4, child +8, next +12, dobj +0x10,
    rotation +0x14, scale +0x20, translation +0x2C) for a list of
    (child index or None, next index or None, (rx, ry, rz)); joint i at i*0x40."""
    data, relocs = bytearray(), []
    for i, (child, nxt, rot) in enumerate(tree):
        j = bytearray(0x40)
        if child is not None:
            struct.pack_into('>I', j, 8, child * 0x40)
            relocs.append(i * 0x40 + 8)
        if nxt is not None:
            struct.pack_into('>I', j, 12, nxt * 0x40)
            relocs.append(i * 0x40 + 12)
        struct.pack_into('>3f', j, 0x14, *rot)
        struct.pack_into('>3f', j, 0x20, 1.0, 1.0, 1.0)
        data += j
    return data, relocs


class ArchiveTests(unittest.TestCase):
    def test_pointer_to_offset_zero_is_not_null(self):
        data = struct.pack('>III', 0, 0, 8)   # +0 points at 0, +4 unrelocated zero, +8 points at 8
        a = Archive(archive(data, [0, 8], {'root': 0}))
        self.assertEqual(a.ptr(0), 0)
        self.assertIsNone(a.ptr(4))
        self.assertEqual(a.ptr(8), 8)

    def test_header_size_must_match(self):
        raw = archive(b'\0' * 8, [], {'root': 0})
        with self.assertRaises(ValueError):
            Archive(raw + b'\0' * 32)

    def test_joint_walk_is_depth_first(self):
        # 0 -> child 1 (next 3), 1 -> child 2
        data, relocs = joints_block([(1, None, (0, 0, 0)), (2, 3, (0, 0, 0)), (None, None, (0, 0, 0)),
                                     (None, None, (0, 0, 0))])
        a = Archive(archive(data, relocs, {'x_joint': 0}))
        self.assertEqual(joint_walk(a, 0), [(0, -1), (0x40, 0), (0x80, 1), (0xC0, 0)])


class BoneTableTests(unittest.TestCase):
    def test_skipped_joint_numbers(self):
        data, relocs = joints_block([(1, None, (0, 0, 0)), (2, None, (0, 0, 0.5)), (None, None, (0, 0, 0))])
        a = Archive(archive(data, relocs, {'x_joint': 0}))
        _walk, number = bonetables.fighter_numbering(a, {1})
        self.assertEqual(number, [0, 2, 3])
        joints = bonetables._joints(a, {1})
        self.assertIs(joints[1], bonetables.SKIPPED)
        self.assertEqual(joints[2][0], 0)      # its parent is joint 0
        self.assertEqual(joints[3][0], 2)
        worlds = bonetables.skeleton(joints)
        self.assertIsNone(worlds[1])
        self.assertAlmostEqual(worlds[3][1][0], __import__('math').sin(0.5), places=6)

    def test_split_include(self):
        lines = ['/* note */', '#define N 2', 'static const short a[N] = { 1, 2 };',
                 'typedef struct T { int x; } T;', 'static const T t[2] = {', '    { 1 },', '    { 2 },', '};']
        with tempfile.TemporaryDirectory() as d:
            dest = Path(d) / 'tables.inc'
            bonetables.split_include(lines, dest)
            header = dest.with_suffix('.h').read_text()
            body = dest.read_text()
        self.assertIn('extern const short a[N];', header)
        self.assertIn('extern const T t[2];', header)
        self.assertIn('typedef struct T', header)
        self.assertNotIn('static', body)
        self.assertIn('const T t[2] = {', body)
        self.assertIn('#include <engine/tables.h>', body)


class TrimTests(unittest.TestCase):
    def test_trimmed_archive_parses(self):
        # Root joint with one DObj (+0x10); the DObj (at 0x40) has a material
        # pointer (+8) to a 16-byte block at 0x60 and no polygons.
        data, relocs = joints_block([(None, None, (0, 0, 0))])
        struct.pack_into('>I', data, 0x10, 0x40)
        relocs.append(0x10)
        dobj = bytearray(0x20)
        struct.pack_into('>I', dobj, 8, 0x60)
        data += dobj + b'\x11' * 16
        relocs.append(0x48)
        raw = archive(data, relocs, {'PlyX_Share_joint': 0})
        for wanted in (set(), {0}):
            out, kept, total = parts.trim(raw, 'PlyX_Share_joint', wanted)
            a = Archive(out)   # the header size equals the file length
            self.assertEqual((kept, total), (len(wanted), 1))
            dobj_at = a.ptr(a.roots['PlyX_Share_joint'] + 0x10)
            self.assertIsNotNone(dobj_at)
            material = a.ptr(dobj_at + 8)
            if wanted:
                self.assertEqual(a.data[material:material + 16], b'\x11' * 16)
            else:
                self.assertIsNone(material)


class FingerprintTests(unittest.TestCase):
    def test_addresses_masked(self):
        a = [0x3C608045, 0x38631234, 0x80830010, 0x4800ABCD]   # lis r3; addi r3,r3; lwz r4,16(r3); b
        b = [0x3C608046, 0x38639990, 0x80830010, 0x48001235]
        self.assertEqual(fingerprint._masked(a), fingerprint._masked(b))

    def test_code_change_seen(self):
        a = [0x38600001]   # li r3, 1
        b = [0x38600002]   # li r3, 2
        self.assertNotEqual(fingerprint._masked(a), fingerprint._masked(b))


class FixTests(unittest.TestCase):
    def test_duplicate_ids_refused(self):
        fix = '[[fix]]\nid = "x"\nowner = "o"\nfile = "f.c"\nanchor = "a"\nreplacement = "b"\n'
        with tempfile.TemporaryDirectory() as d:
            (Path(d) / '1.toml').write_text(fix)
            (Path(d) / '2.toml').write_text(fix)
            with self.assertRaises(ValueError):
                load_fixes(Path(d))

    def test_fixes_in_name_order(self):
        with tempfile.TemporaryDirectory() as d:
            for name, fid in (('20-b.toml', 'b'), ('10-a.toml', 'a')):
                (Path(d) / name).write_text(f'[[fix]]\nid = "{fid}"\nowner = "o"\nfile = "f.c"\nanchor = "a"\n'
                                            'replacement = "b"\n')
            self.assertEqual([f['id'] for f in load_fixes(Path(d))], ['a', 'b'])


if __name__ == '__main__':
    unittest.main()
