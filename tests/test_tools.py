"""Host tests for the build tooling (no compiler needed): python3 -m unittest discover tests"""
import struct
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))

from bam import ppc  # noqa: E402
from bam.dol import Dol, Section  # noqa: E402
from bam.hooks import Hook, Trampolines, build_patches, FRAME  # noqa: E402
from bam.transform import externize, apply_line_edits, LineEdit, TransformError  # noqa: E402
from bam import iso  # noqa: E402


class PpcTests(unittest.TestCase):
    def test_branch_roundtrip(self):
        w = ppc.b(0x80069388, 0x80BD6360)
        self.assertEqual(w, 0x48B6CFD8)
        self.assertEqual(ppc.branch_target(w, 0x80069388), 0x80BD6360)
        self.assertTrue(ppc.is_relative_branch(w))
        self.assertFalse(ppc.is_relative_branch(0x7FE3FB78))

    def test_bl_rebase(self):
        w = ppc.bl(0x80069384, 0x800867E8)
        r = ppc.rebase_relative_branch(w, 0x80069384, 0x80BD6000)
        self.assertEqual(ppc.branch_target(r, 0x80BD6000), 0x800867E8)
        self.assertEqual(r & 1, 1)

    def test_known_encodings(self):
        self.assertEqual(ppc.stwu(1, -0x120, 1), 0x9421FEE0)
        self.assertEqual(ppc.mflr(0), 0x7C0802A6)
        self.assertEqual(ppc.mtlr(0), 0x7C0803A6)
        self.assertEqual(ppc.mfcr(0), 0x7C000026)
        self.assertEqual(ppc.mtcr(0), 0x7C0FF120)
        self.assertEqual(ppc.mfctr(0), 0x7C0902A6)
        self.assertEqual(ppc.mfxer(0), 0x7C0102A6)
        self.assertEqual(ppc.stmw(3, 12, 1), 0xBC61000C)
        self.assertEqual(ppc.BCTRL, 0x4E800421)
        self.assertEqual(ppc.lis(12, 0x80BD), 0x3D8080BD)
        self.assertEqual(ppc.ori(12, 12, 0x61F0), 0x618C61F0)

    def test_not_displaceable(self):
        self.assertFalse(ppc.is_displaceable(0x4E800020))   # blr
        self.assertFalse(ppc.is_displaceable(0x41820010))   # beq
        self.assertTrue(ppc.is_displaceable(0x7FE3FB78))    # mr


class DolTests(unittest.TestCase):
    def make(self):
        d = Dol(bss_address=0x80400000, bss_size=0x100, entry=0x80003100)
        d.sections.append(Section(0, 0x80003100, bytearray(struct.pack('>4I', 1, 2, 3, 4))))
        d.sections.append(Section(7, 0x80300000, bytearray(16)))
        return d

    def test_roundtrip_and_patch(self):
        d = self.make()
        self.assertEqual(d.write_u32(0x80003104, 0xDEADBEEF), 2)
        self.assertEqual(d.read_u32(0x80003104), 0xDEADBEEF)
        sec = d.add_section(0x80BD5C40, b'\x01' * 10)
        self.assertEqual(sec.index, 1)
        self.assertEqual(len(sec.data), 0x20)
        with tempfile.TemporaryDirectory() as td:
            p = Path(td) / 'x.dol'
            d.save(p)
            e = Dol.load(p)
        self.assertEqual(e.read_u32(0x80003104), 0xDEADBEEF)
        self.assertEqual(e.read_bytes(0x80BD5C40, 10), b'\x01' * 10)
        self.assertEqual(e.bss_address, 0x80400000)
        self.assertEqual(e.entry, 0x80003100)

    def test_overlap_refused(self):
        d = self.make()
        with self.assertRaises(ValueError):
            d.add_section(0x80003100, b'\0' * 32)
        with self.assertRaises(ValueError):
            d.add_section(0x80400000, b'\0' * 32)


class HookTests(unittest.TestCase):
    def test_inject_trampoline(self):
        tramp = Trampolines(0x80BD6360)
        mem = {0x80069388: 0x7FE3FB78}
        ovl = {'BAM_OnFighterCreated': 0x80BD61F0}
        patches = build_patches([Hook('t', 'inject', 'Fighter_Create+0x4F0', 'BAM_OnFighterCreated', ['r31'])],
                                mem.__getitem__, {'Fighter_Create': 0x80068E98}.get, ovl.get, tramp)
        self.assertEqual(len(patches), 1)
        self.assertEqual(patches[0].addr, 0x80069388)
        self.assertEqual(ppc.branch_target(patches[0].value, 0x80069388), 0x80BD6360)
        words = tramp.words
        self.assertEqual(words[0], ppc.stwu(1, -FRAME, 1))
        self.assertEqual(words[-2], 0x7FE3FB78)                       # displaced instruction
        self.assertEqual(ppc.branch_target(words[-1], tramp.cursor - 4), 0x8006938C)
        self.assertIn(ppc.lwz(3, 0x0C + 28 * 4, 1), words)            # r31 slot -> r3
        self.assertIn(ppc.BCTRL, words)

    def test_override_and_call(self):
        tramp = Trampolines(0x80BD7000)
        mem = {0x80342EBC: 0x7C0802A6, 0x80069384: ppc.bl(0x80069384, 0x800867E8)}
        ovl = {'BAM_ClearArena': 0x80BD5D70, 'BAM_X': 0x80BD5E00}
        ps = build_patches([Hook('a', 'override', 'ClearArena', 'BAM_ClearArena'),
                            Hook('b', 'call', '0x80069384', 'BAM_X')],
                           mem.__getitem__, {'ClearArena': 0x80342EBC}.get, ovl.get, tramp)
        self.assertEqual(ppc.branch_target(ps[0].value, 0x80342EBC), 0x80BD5D70)
        self.assertEqual(ppc.branch_target(ps[1].value, 0x80069384), 0x80BD5E00)
        self.assertEqual(ps[1].value & 1, 1)
        self.assertEqual(tramp.size, 0)

    def test_duplicate_site_refused(self):
        tramp = Trampolines(0x80BD7000)
        with self.assertRaises(ValueError):
            build_patches([Hook('a', 'word', '0x80003100', value=1), Hook('b', 'word', '0x80003100', value=2)],
                          lambda a: 0, {}.get, {}.get, tramp)


class TransformTests(unittest.TestCase):
    def test_externize(self):
        src = 'static int counter = 0;\nstatic HSD_ObjAllocData alloc;\nint t[2] = {1, 2};\nvoid f(void) { static int l; counter++; }\n'
        out = externize(src, ['counter', 'alloc', 't'], 'x')
        self.assertIn('extern int counter;', out)
        self.assertIn('extern HSD_ObjAllocData alloc;', out)
        self.assertIn('extern int t[2];', out)
        self.assertIn('static int l;', out)
        with self.assertRaises(TransformError):
            externize(src, ['missing'], 'x')

    def test_line_edits(self):
        text = 'a\nb\nc\n'
        out = apply_line_edits(text, [LineEdit(1, ['b'], ['B', 'B2']), LineEdit(3, [], ['d'])], 'x')
        self.assertEqual(out, 'a\nB\nB2\nc\nd\n')
        with self.assertRaises(TransformError):
            apply_line_edits(text, [LineEdit(1, ['zzz'], [])], 'x')


class IsoTests(unittest.TestCase):
    def make_image(self):
        """Minimal disc: header, apploader, DOL, FST with one file, padding."""
        img = bytearray(0x20000)
        img[0:6] = b'GALE01'; img[7] = 2
        dol_off, fst_off, fst_size = 0x3000, 0x8000, 0x30
        struct.pack_into('>4I', img, 0x420, dol_off, fst_off, fst_size, fst_size)
        struct.pack_into('>II', img, 0x2454, 0x100, 0x20)   # apploader sizes
        dol = Dol(bss_address=0x80400000, bss_size=0x10, entry=0x80003100)
        dol.sections.append(Section(0, 0x80003100, bytearray(b'\x60\0\0\0' * 8)))
        d = dol.to_bytes()
        img[dol_off:dol_off + len(d)] = d
        # FST: root dir entry + one file at 0x10000 size 0x100, string table
        root = struct.pack('>III', 1 << 24, 0, 2)
        f = struct.pack('>III', 0, 0x10000, 0x100)
        names = b'\0file\0'
        fst = (root + f + names).ljust(fst_size, b'\0')
        img[fst_off:fst_off + fst_size] = fst
        return bytes(img), d

    def test_assemble_keeps_fst(self):
        img, dol = self.make_image()
        with tempfile.TemporaryDirectory() as td:
            td = Path(td)
            (td / 'src.iso').write_bytes(img)
            new_dol = dol + b'\xAA' * 0x4000     # grown executable
            (td / 'new.dol').write_bytes(new_dol)
            off = iso.assemble(td / 'src.iso', td / 'new.dol', td / 'out.iso')
            out = (td / 'out.iso').read_bytes()
            self.assertEqual(out[:6], b'GALE01')
            self.assertEqual(struct.unpack_from('>I', out, 0x420)[0], off)
            self.assertEqual(out[off:off + len(new_dol)], new_dol)
            self.assertEqual(out[0x8000:0x8030], img[0x8000:0x8030])       # FST untouched
            self.assertEqual(out[0x10000:0x10100], img[0x10000:0x10100])   # file data untouched


if __name__ == '__main__':
    unittest.main()
