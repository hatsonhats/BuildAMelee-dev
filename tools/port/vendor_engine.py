#!/usr/bin/env python3
"""Vendor the borrowed-move engine from a rogueMelee-v2 checkout into src/engine.

The engine files keep their upstream names and internal `Rogue_*` API so
fixes can be ported back and forth with small diffs. What changes:

* run/director/play-context dependencies -> src/engine/bam_fighter.h
  (per-slot loadouts, fighter identity, per-match state block)
* `rogue_fighter_states[]` / `ROGUE_BUILD_FIGHTERS` -> `bam_match->fighters[]`
  / BAM_FIGHTERS (per-match heap block, so Slippi rollback restores it)
* file-level mutable statics -> members of BamMatchState (same reason), the
  original names kept through #defines so the code bodies are untouched
* RogueRuntime trace/accounting -> no-ops

Usage: vendor_engine.py <rogueMelee-v2/src> [<repo root>]
"""
from __future__ import annotations
import re
import sys
from pathlib import Path

SRC = Path(sys.argv[1]).resolve()
ROOT = Path(sys.argv[2]).resolve() if len(sys.argv) > 2 else Path(__file__).resolve().parents[2]
DST = ROOT / 'src/engine'
DST.mkdir(parents=True, exist_ok=True)

SPECIALS = SRC / 'platform/melee/specials'

# Per-match mutable statics moved into BamMatchState: (file, declaration regex, member decl)
MATCH_STATE = {
    'anim_scale.c': [
        (r'static Scaled scaled\[ROGUE_BUILD_FIGHTERS \* 6\];', 'Scaled scaled[BAM_FIGHTERS * 6];'),
        (r'static unsigned scaled_count;', 'unsigned scaled_count;'),
        (r'static RotFix rotfix\[ROGUE_BUILD_FIGHTERS \* ROTFIX_PER_FIGHTER\];', 'RotFix rotfix[BAM_FIGHTERS * ROTFIX_PER_FIGHTER];'),
        (r'static unsigned char rotfix_part\[ROGUE_BUILD_FIGHTERS \* ROTFIX_PER_FIGHTER\];', 'unsigned char rotfix_part[BAM_FIGHTERS * ROTFIX_PER_FIGHTER];'),
        (r'static unsigned short rotfix_hash\[ROTFIX_HASH\];[^\n]*', 'unsigned short rotfix_hash[ROTFIX_HASH];'),
        (r'static unsigned char joint_parent\[ROGUE_BUILD_FIGHTERS\]\[POSE_JOINTS\];', 'unsigned char joint_parent[BAM_FIGHTERS][POSE_JOINTS];'),
        (r'static unsigned char pose_on\[ROGUE_BUILD_FIGHTERS\];', 'unsigned char pose_on[BAM_FIGHTERS];'),
        (r'static PropTrack prop_tracks\[ROGUE_BUILD_FIGHTERS\]\[PROP_TRACKS\];', 'PropTrack prop_tracks[BAM_FIGHTERS][PROP_TRACKS];'),
        (r'static unsigned char prop_track_count\[ROGUE_BUILD_FIGHTERS\];', 'unsigned char prop_track_count[BAM_FIGHTERS];'),
        (r'static PropHit prop_hits\[ROGUE_BUILD_FIGHTERS \* 4\];', 'PropHit prop_hits[BAM_FIGHTERS * 4];'),
        (r'static short last_prop\[ROGUE_BUILD_FIGHTERS\], last_root\[ROGUE_BUILD_FIGHTERS\];', 'short last_prop[BAM_FIGHTERS], last_root[BAM_FIGHTERS];'),
        (r'static PropAnchor anchors\[ROGUE_BUILD_FIGHTERS\]\[ANCHORS\];', 'PropAnchor anchors[BAM_FIGHTERS][ANCHORS];'),
        (r'static unsigned char anchor_next\[ROGUE_BUILD_FIGHTERS\];', 'unsigned char anchor_next[BAM_FIGHTERS];'),
    ],
    'sword_visual.c': [
        (r'static HSD_JObj\* weapons\[ROGUE_BUILD_FIGHTERS\]\[WEAPON_ITEMS\];', 'HSD_JObj* weapons[BAM_FIGHTERS][WEAPON_ITEMS];'),
        (r'static Fighter\* weapon_owner\[ROGUE_BUILD_FIGHTERS\];', 'Fighter* weapon_owner[BAM_FIGHTERS];'),
        (r'static DonorModel\* donor_models\[ROGUE_BUILD_FIGHTERS\];', 'DonorModel* donor_models[BAM_FIGHTERS];'),
        (r'static signed char donor_vis\[ROGUE_BUILD_FIGHTERS\]\[VIS_GROUPS\];', 'signed char donor_vis[BAM_FIGHTERS][VIS_GROUPS];'),
        (r'static unsigned char donor_vis_kind\[ROGUE_BUILD_FIGHTERS\];', 'unsigned char donor_vis_kind[BAM_FIGHTERS];'),
        (r'static unsigned char parasol_float\[ROGUE_BUILD_FIGHTERS\];[^\n]*', 'unsigned char parasol_float[BAM_FIGHTERS];'),
        (r'static Mtx weapon_attach\[ROGUE_BUILD_FIGHTERS\]\[WEAPON_ITEMS\];', 'Mtx weapon_attach[BAM_FIGHTERS][WEAPON_ITEMS];'),
        (r'static Mtx parasol_rel\[ROGUE_BUILD_FIGHTERS\];', 'Mtx parasol_rel[BAM_FIGHTERS];'),
    ],
    'rest_sleep.c': [
        (r'static Fighter\* sleepers\[ROGUE_BUILD_FIGHTERS\];', 'Fighter* sleepers[BAM_FIGHTERS];'),
    ],
}

# Private typedefs/constants that the moved statics need; hoisted to the top
# of the file so the per-file state struct can be defined early.
HOIST = {
    'anim_scale.c': [
        r'typedef struct Scaled \{.*?\} Scaled;\n',
        r'typedef struct Quat \{[^\n]*\n',
        r'typedef struct RotFix \{.*?\} RotFix;\n',
        r'#define ROTFIX_FOLDS 4\n', r'#define ROTFIX_PER_FIGHTER \(ROGUE_REST_PARTS \* 2\)\n', r'#define ROTFIX_HASH 1024U\n',
        r'#define POSE_JOINTS 192\n',
        r'#include <sysdolphin/baselib/mtx.h>\n',
        r'typedef struct PropTrack \{.*?\} PropTrack;\n', r'#define PROP_TRACKS 32\n',
        r'typedef struct PropHit \{.*?\} PropHit;\n',
        r'typedef struct PropAnchor \{[^\n]*\n', r'#define ANCHORS 4\n',
    ],
    'sword_visual.c': [
        r'#define WEAPON_ITEMS 3\n',
        r'#define DONOR_JOINTS 192\n', r'#define DONOR_MESHES 16\n',
        r'typedef struct DonorModel \{.*?\} DonorModel;\n',
        r'#define VIS_GROUPS 12\n',
    ],
    'rest_sleep.c': [],
}
STATE_NAME = {'anim_scale.c': 'AnimScale', 'sword_visual.c': 'SwordVisual', 'rest_sleep.c': 'RestSleep'}

COMMON_REPL = [
    (r'#include "\.\./melee_fighter\.h"', '#include "bam_fighter.h"'),
    (r'#include "\.\./\.\./\.\./director/rogue_director\.h"\n', ''),
    (r'#include "\.\./\.\./\.\./core/play_context\.h"\n', ''),
    (r'#include "\.\./debug_launch\.h"\n', ''),
    (r'#include "\.\./encounter_assets\.h"\n', ''),
    (r'\bROGUE_BUILD_FIGHTERS\b', 'BAM_FIGHTERS'),
    (r'\brogue_fighter_states\b', 'bam_match->fighters'),
    (r'\brogue_donor_attrs\b', 'bam_match->donor_attrs'),
    (r'RoguePlay_Special\(RogueDirector_Run\(\),\s*(\w+)\)', r'Rogue_EquippedSpecial(fp, \1)'),
    (r'RoguePlay_Aerial\(RogueDirector_Run\(\),\s*(\w+)\)', r'Rogue_EquippedAerial(fp, \1)'),
    (r'RogueRuntime_Trace\([^;]*\);', '/* trace removed */'),
    (r'RogueRuntime_MatchAcquire\([^;]*\);', '/* match accounting removed */'),
    (r'RogueRuntime_MatchRelease\([^;]*\);', '/* match accounting removed */'),
    (r'RogueRuntime_Get\(\)->match_generation', 'bam_match->generation'),
    (r'RogueRuntime_IsActive\(\)', 'Bam_Active()'),
    (r'RogueRuntime_IsPractice\(\)', '0'),
    (r'\bROGUE_ENABLE_AERIALS\b', 'BAM_ENABLE_AERIALS'),
    (r'\bROGUE_DEBUG\b', 'BAM_DEBUG'),
    (r'\bROGUE_QA_EVENT_RELEASE\b', '0'),
    (r'\bROGUE_QA_NATIVE_CAMP\b', '0'),
    (r'\(ROGUE_QA_MODE == 5\)|ROGUE_QA_MODE == 5', '0'),
    (r'"\[rogue\]', '"[bam]'),
]

EXTRA = {
    'special_engine.h': [
        (r'void Rogue_AbilityFrame\(void\);', 'void Rogue_AbilityFighterFrame(Fighter* fp);'),
        (r'#endif\s*$', '''/* Entry points called from pinned edits in retail units (overrides/). */
struct FigaTrack;
struct HitCapsule;
void Rogue_LinkAerialDownEnter(Fighter_GObj* gobj);
void Rogue_PropTrack(Fighter* fp, int joint, struct FigaTrack* track, int count);
void Rogue_HitboxCreated(Fighter* fp, struct HitCapsule* hit, int bone);
void Rogue_HitboxRefresh(Fighter* fp, struct HitCapsule* hit);
void Rogue_AnimPostStep(Fighter* fp);
bool Rogue_RestSleep(Fighter_GObj* gobj);
bool Rogue_RestSleeping(Fighter* fp);
void Rogue_RestSleepClear(Fighter* fp);
bool Rogue_ParasolFloat(HSD_GObj* gobj);
#endif
'''),
    ],
    'special_internal.h': [
        (r'typedef union \{ double align; unsigned char bytes\[0x424\]; \} RogueDonorAttrs;\nextern RogueDonorAttrs bam_match->donor_attrs\[Ft_Kind_Max\];\n',
         'typedef union { double align; unsigned char bytes[0x424]; } RogueDonorAttrs;\n'),
        (r'/\* Borrowers: \[0\] the run player.*?#define BAM_FIGHTERS 4\nextern RogueFighterState bam_match->fighters\[BAM_FIGHTERS\];\n',
         '#include "bam_match.h"\n'),
        (r'    bool shares_partner;\n\} RogueFighterState;',
         '    bool shares_partner;\n    /* This fighter\'s kit (copied from its slot\'s loadout at creation). */\n    unsigned char specials[BAM_SPECIAL_SLOTS];\n    unsigned char aerials[BAM_AERIAL_SLOTS];\n} RogueFighterState;'),
    ],
    'special_runtime.c': [
        (r'void Rogue_AbilityFrame\(void\)\n\{.*?\n\}\n',
         '''void Rogue_AbilityFighterFrame(Fighter* fp)
{
    RogueFighterState* const S = Rogue_FighterCtx(fp);
    const ftKoopaAttributes* da = (const ftKoopaAttributes*) bam_match->donor_attrs[Ft_Kind_Koopa].bytes;
    struct ftKoopa_FighterVars* fuel;
    if (S->fighter != fp || fp->kind == Ft_Kind_Koopa || !S->loaded_sources[Ft_Kind_Koopa]) return;
    if (Rogue_IsAbilityState(fp) && Rogue_AbilitySourceKind(fp) == Ft_Kind_Koopa) {
        /* Borrowed Bowser move in progress: its vars are installed. */
        if (fp->motion_id >= 0x155 && fp->motion_id < 0x15B) return;
        fuel = &fp->u.kp;
    } else fuel = &S->source_vars[Ft_Kind_Koopa].kp;
    fuel->x222C += da->x8;
    if (fuel->x222C > da->x10) fuel->x222C = da->x10;
    fuel->x2230 += da->xC;
    if (fuel->x2230 > da->x18) fuel->x2230 = da->x18;
}
'''),
        (r'RogueFighterState bam_match->fighters\[BAM_FIGHTERS\];\nRogueDonorAttrs bam_match->donor_attrs\[Ft_Kind_Max\];\n', ''),
        (r'RogueFighterState\* Rogue_FighterCtx\(const Fighter\* fp\)\n\{\n    if \(fp\) \{.*?\n    \}\n#if BAM_DEBUG',
         'RogueFighterState* Rogue_FighterCtx(const Fighter* fp)\n{\n    if (fp && bam_match) {\n        RogueFighterState* S = *Bam_FighterExtSlot(fp);\n        if (S && S->fighter == fp) return S;\n    }\n#if BAM_DEBUG'),
        (r'RogueDirector_Run\(\)->specials\[def->native_slot\] = def->id;', 'bam_loadouts[0].specials[def->native_slot] = (unsigned char) def->id; bam_loadouts[0].enabled = 1;'),
    ],
    'special_preload.c': [
        (r'    /\* \[0\] the run player, \[1\] the final boss mirror of the run\'s build;\n     \* \+2 for the Ice Climbers\' Nana, who borrows the same moves. \*/\n    index = \(Rogue_IsRunPlayer\(fp\) \? 0 : 1\) \+ \(fp->is_sub_fighter \? 2 : 0\);\n    S = &bam_match->fighters\[index\];\n    if \(S == &bam_match->fighters\[0\]\) skipped_donors = 0;',
         '    index = (unsigned) Bam_FighterIndex(fp);\n    S = &bam_match->fighters[index];\n    *Bam_FighterExtSlot(fp) = S;\n    if (index == 0) skipped_donors = 0;'),
        (r'    S->match_generation = bam_match->generation;\n',
         '    S->match_generation = bam_match->generation;\n    memcpy(S->specials, bam_loadouts[fp->player_id].specials, sizeof(S->specials));\n    memcpy(S->aerials, bam_loadouts[fp->player_id].aerials, sizeof(S->aerials));\n'),
        (r'if \(index >= 2 && bam_match->fighters\[index - 2\]\.fighter\) \{\n        const RogueFighterState\* P = &bam_match->fighters\[index - 2\];',
         'if ((index & 1) && bam_match->fighters[index - 1].fighter) {\n        const RogueFighterState* P = &bam_match->fighters[index - 1];'),
    ],
    'special_transform.c': [
        (r'    if \(S == &bam_match->fighters\[0\]\) for \(slot = 0; slot < 4; \+\+slot\) \{\n        const RogueAbilityDefinition\* equipped = Rogue_GetAbility\(Rogue_EquippedSpecial\(fp, slot\)\);\n        if \(equipped && equipped->internal_kind == old_kind\)\n            RogueDirector_Run\(\)->specials\[slot\] = 1 \+ next_kind \* 4 \+ slot;\n    \}',
         '    for (slot = 0; slot < 4; ++slot) {\n        const RogueAbilityDefinition* equipped = Rogue_GetAbility(Rogue_EquippedSpecial(fp, slot));\n        if (equipped && equipped->internal_kind == old_kind)\n            Rogue_SetEquippedSpecial(fp, slot, 1 + next_kind * 4 + slot);\n    }'),
    ],
    'aerial_runtime.c': [
        (r'    scale = 1\.0f - RogueDirector_Run\(\)->stacks\[ROGUE_STACK_LANDING\] \* 0\.12f;\n    if \(scale < 0\.5f\) scale = 0\.5f;\n', '    scale = 1.0f;\n'),
    ],
}


def vendor(name: str, extra=()):
    text = (SPECIALS / name).read_text(encoding='utf-8')
    members = []
    for pat, member in MATCH_STATE.get(name, []):
        text, n = re.subn(pat, '', text)
        if n != 1:
            raise SystemExit(f'{name}: expected one match for {pat}, got {n}')
        members.append(member)
    hoisted = []
    for pat in HOIST.get(name, []):
        m = re.search(pat, text, re.S)
        if not m:
            raise SystemExit(f'{name}: hoist pattern not found: {pat}')
        hoisted.append(m.group(0))
        text = text[:m.start()] + text[m.end():]
    for pat, rep in COMMON_REPL:
        text = re.sub(pat, rep, text)
    for pat, rep in extra:
        text, n = re.subn(pat, rep, text, flags=re.S)
        if n == 0:
            raise SystemExit(f'{name}: pattern not found: {pat}')
    if members:
        sname = STATE_NAME[name] + 'State'
        var = 'bam_' + re.sub(r'(?<!^)(?=[A-Z])', '_', STATE_NAME[name]).lower()
        idents = []
        for m in members:
            for ident in re.findall(r'\b([a-z_][a-z0-9_]*)\s*(?:\[|;|,)', m):
                if ident not in ('unsigned', 'char', 'short', 'static', 'signed'):
                    idents.append(ident)
        block = ['', '/* ---- per-match state (vendor_engine.py) ----',
                 ' * These were file-level statics upstream. They live in a match-heap block',
                 ' * so Slippi rollback restores them; the pointer is set once per match. */']
        block += [h.rstrip('\n') for h in hoisted]
        block += [f'typedef struct {sname} {{'] + ['    ' + m for m in members] + [f'}} {sname};',
                  f'static {sname}* {var};']
        block += [f'#define {i} ({var}->{i})' for i in sorted(set(idents))]
        block.append('')
        # Insert after the file's leading #include block.
        lines = text.split('\n')
        end = 0
        for i, line in enumerate(lines):
            if line.startswith('#include'):
                end = i + 1
            elif line.strip() and end:
                break
        lines[end:end] = block
        text = '\n'.join(lines)
        text += f'''
#include <sysdolphin/baselib/memory.h>
/* Called from Bam_MatchBegin / Bam_MatchEnd (bam_fighter.c). */
void Rogue_{STATE_NAME[name]}MatchBegin(void)
{{
    {var} = HSD_MemAlloc(sizeof(*{var}));
    memset({var}, 0, sizeof(*{var}));
}}
void Rogue_{STATE_NAME[name]}MatchEnd(void)
{{
    {var} = NULL;
}}
'''
    (DST / name).write_text(text, encoding='utf-8')
    return members


def main():
    all_members = {}
    for name in ('special_engine.h', 'special_internal.h', 'special_runtime.c', 'special_registry.c',
                 'special_preload.c', 'aerial_runtime.c', 'special_transform.c', 'anim_scale.c',
                 'sword_visual.c', 'rest_sleep.c', 'anim_rest.inc'):
        all_members[name] = vendor(name, EXTRA.get(name, ()))
    # Catalogs (generated upstream from data/*.json; vendored as data).
    for rel, out in (('combat/aerials/aerial_catalog.h', 'aerial_catalog.h'),
                     ('combat/aerials/aerial_catalog.c', 'aerial_catalog.c'),
                     ('combat/aerials/aerial_catalog_data.c', 'aerial_catalog_data.c'),
                     ('combat/specials/special_catalog.h', 'special_catalog.h'),
                     ('combat/specials/special_catalog.c', 'special_catalog.c'),
                     ('combat/specials/special_catalog_data.c', 'special_catalog_data.c'),
                     ('core/catalog_counts.h', 'catalog_counts.h')):
        t = (SRC / rel).read_text(encoding='utf-8')
        t = t.replace('#include "../../core/catalog_counts.h"', '#include "catalog_counts.h"')
        (DST / out).write_text(t, encoding='utf-8')
    # Emit the match-state member list for bam_match_state.inc
    lines = ['/* generated by tools/port/vendor_engine.py: file-level state moved into BamMatchState */']
    for name, members in all_members.items():
        if members:
            lines.append(f'    /* {name} */')
            lines += ['    ' + m for m in members]
    (DST / 'bam_match_state.inc').write_text('\n'.join(lines) + '\n', encoding='utf-8')
    normalize_includes()
    print('vendored into', DST)


def normalize_includes():
    """MWCC (-cwd source) resolves nested quote-includes relative to the
    translation unit, not the including header. Use rooted <engine/...> and
    <bam/...> paths for everything we own."""
    for f in list(DST.glob('*.c')) + list(DST.glob('*.h')) + list(DST.glob('*.inc')):
        text = f.read_text(encoding='utf-8')
        def fix(m):
            name = m.group(1)
            if (DST / name).is_file():
                return f'#include <engine/{name}>'
            if name == '../bam/bam.h':
                return '#include <bam/bam.h>'
            return m.group(0)
        new = re.sub(r'#include "([^"]+)"', fix, text)
        if new != text:
            f.write_text(new, encoding='utf-8')


if __name__ == '__main__':
    main()
