"""Source transforms applied to copies of retail translation units.

Two kinds of edit are supported, both pinned to the decomp revision:

* line edits  (line_edits.json): {line, before[], after[]} against a base
  file whose sha256 is checked first, or {rule, lines: [[line, count], ...]}
  for lines rewritten by one of the RULES below.
* anchor edits (TOML): {anchor, replacement, occurrences=1}.

Additionally, `externize` turns top-level *definitions* of named objects into
`extern` declarations. A recompiled unit must not carry its own copy of
mutable retail state (allocator records, counters, caches), otherwise retail
code and overlay code would read two different variables. The retail object
stays where it is and the overlay binds to it through the absolute symbol
table.
"""
from __future__ import annotations
import hashlib
import re
from dataclasses import dataclass
from typing import Dict, Iterable, List, Sequence


class TransformError(ValueError):
    pass


@dataclass
class LineEdit:
    line: int
    before: List[str]
    after: List[str]


@dataclass
class AnchorEdit:
    anchor: str
    replacement: str
    occurrences: int = 1
    id: str = ''


def check_sha256(text: str, expected: str, label: str) -> None:
    actual = hashlib.sha256(text.encode('utf-8')).hexdigest()
    if actual != expected:
        raise TransformError(f'{label}: base file differs from the pinned revision ({actual} != {expected})')


def apply_line_edits(text: str, edits: Sequence[LineEdit], label: str) -> str:
    lines = text.splitlines()
    occupied = set()
    for e in edits:
        if lines[e.line:e.line + len(e.before)] != e.before:
            raise TransformError(f'{label}: original mismatch at line {e.line}')
        for i in range(e.line, e.line + max(1, len(e.before))):
            if i in occupied:
                raise TransformError(f'{label}: overlapping edits at line {i}')
            occupied.add(i)
    for e in sorted(edits, key=lambda e: e.line, reverse=True):
        lines[e.line:e.line + len(e.before)] = e.after
    return '\n'.join(lines) + '\n'


# ---- rules --------------------------------------------------------------------
# A rule rewrites the listed lines of the pinned file by a fixed pattern, so
# the hundreds of mechanical edits of the borrowed-move engine read as one
# rule and a list of lines. Edits that do anything else are written out.

# Fighter_FighterVars union members (fp->u.XX) and the kind that owns each.
DONOR_VARS = {
    'ca': 'Captain', 'dk': 'Donkey', 'fx': 'Fox', 'gw': 'GameWatch', 'kb': 'Kirby', 'kp': 'Koopa',
    'lg': 'Luigi', 'lk': 'Link', 'mr': 'Mario', 'ms': 'Mars', 'mt': 'Mewtwo', 'ns': 'Ness',
    'pe': 'Peach', 'pp': 'Popo', 'pr': 'Purin', 'sk': 'Seak', 'ss': 'Samus', 'ys': 'Yoshi', 'zd': 'Zelda',
}


def _donor_access(line: str) -> str:
    """`fp->u.kb.x` -> `Bam_DonorVars(fp, Ft_Kind_Kirby)->kb.x` (the donor's
    variables, not the borrower's), and `fp->parts[FtPart_X]` ->
    `fp->parts[Bam_DonorBoneJoint(fp, FtPart_X)]` (the borrower's bone for
    the donor's body part)."""
    line = re.sub(r'\b(\w+)->u\.(\w+)\.',
                  lambda m: f'Bam_DonorVars({m[1]}, Ft_Kind_{DONOR_VARS[m[2]]})->{m[2]}.' if m[2] in DONOR_VARS else m[0],
                  line)
    return re.sub(r'\b(\w+)->parts\[(FtPart_\w+)\]', r'\1->parts[Bam_DonorBoneJoint(\1, \2)]', line)


RULES = {'donor-access': _donor_access}


def line_edits_for(entry: dict, text: str, label: str) -> List[LineEdit]:
    """The line edits of one file's manifest entry, rules expanded against
    `text` (the pinned file)."""
    lines = text.splitlines()
    out = []
    for e in entry['edits']:
        if 'rule' not in e:
            out.append(LineEdit(e['line'], e['before'], e['after']))
            continue
        rule = RULES.get(e['rule'])
        if rule is None:
            raise TransformError(f'{label}: unknown rule {e["rule"]!r}')
        for at, count in e['lines']:
            before = lines[at:at + count]
            after = [rule(l) for l in before]
            if after == before:
                raise TransformError(f'{label}: rule {e["rule"]} changes nothing at line {at}')
            out.append(LineEdit(at, before, after))
    return out


def apply_anchor_edits(text: str, edits: Sequence[AnchorEdit], label: str) -> str:
    for e in edits:
        n = text.count(e.anchor)
        if n != e.occurrences:
            raise TransformError(f'{label}: anchor {e.id or e.anchor[:40]!r} found {n} times, expected {e.occurrences}')
        text = text.replace(e.anchor, e.replacement)
    return text


# ---- externize ---------------------------------------------------------------

def _top_level_statements(text: str):
    """Yield (start, end, body_is_function) spans of top-level statements.
    Comments, strings and character literals are skipped for brace counting.
    Preprocessor lines are treated as separators."""
    i, n = 0, len(text)
    depth = 0
    paren = 0
    start = 0
    saw_paren_at_depth0 = False
    first_brace = -1
    while i < n:
        c = text[i]
        if c == '/' and i + 1 < n and text[i + 1] == '/':
            j = text.find('\n', i)
            i = n if j < 0 else j
            continue
        if c == '/' and i + 1 < n and text[i + 1] == '*':
            j = text.find('*/', i + 2)
            i = n if j < 0 else j + 2
            continue
        if c in '"\'':
            q = c
            i += 1
            while i < n and text[i] != q:
                i += 2 if text[i] == '\\' else 1
            i += 1
            continue
        if c == '#' and depth == 0 and (i == 0 or text[i - 1] == '\n'):
            j = text.find('\n', i)
            while j >= 0 and text[j - 1] == '\\':
                j = text.find('\n', j + 1)
            i = n if j < 0 else j + 1
            start = i
            first_brace = -1
            continue
        if c == '(' and depth == 0:
            paren += 1
            saw_paren_at_depth0 = True
        elif c == ')' and depth == 0:
            paren -= 1
        elif c == '{':
            if depth == 0 and first_brace < 0:
                first_brace = i
            depth += 1
        elif c == '}':
            depth -= 1
            if depth == 0:
                # A '}' closing a function body ends the statement. For
                # struct/initializer bodies the statement continues to ';'.
                # A function body followed by a stray ';' ("};", common in
                # some decomp files) is still a function: its parameter list
                # comes before the body and no '=' does.
                j = i + 1
                while j < n and text[j] in ' \t\r\n':
                    j += 1
                head = text[start:first_brace] if first_brace >= 0 else ''
                is_body = '(' in head and '=' not in head and not re.match(r'\s*(typedef|struct|union|enum)\b', head)
                if j >= n or text[j] != ';' or is_body:
                    if saw_paren_at_depth0:
                        yield start, i + 1, True
                        start = i + 1
                        saw_paren_at_depth0 = False
                        first_brace = -1
                    i += 1
                    continue
        elif c == ';' and depth == 0 and paren == 0:
            yield start, i + 1, False
            start = i + 1
            saw_paren_at_depth0 = False
            first_brace = -1
        i += 1


def externize(text: str, names: Iterable[str], label: str, strict: bool = True) -> str:
    """Replace top-level definitions of `names` with extern declarations."""
    todo = set(names)
    if not todo:
        return text
    spans = list(_top_level_statements(text))
    out = text
    # Apply from the end so earlier offsets stay valid.
    for start, end, is_func in reversed(spans):
        if is_func:
            continue
        stmt = text[start:end]
        if 'typedef' in stmt.split('=')[0]:
            continue
        for name in list(todo):
            m = re.search(r'(?<![\w.])' + re.escape(name) + r'\s*(?:\)\s*)*(?:\([^;={}]*\)\s*)?((?:\[[^\]]*\]\s*)*)(=|;)', stmt)
            if not m:
                continue
            head = stmt[:m.end() - 1].rstrip()  # up to (but excluding) '=' or ';'
            if re.search(r'\[\s*\]', head) and m.group(2) == '=':  # noqa
                # `T x[] = {...}`: the size comes from the initializer and other
                # code may use sizeof(x). Keep the (read-only) copy.
                todo.discard(name)
                break
            # Skip leading whitespace and comments; drop a leading `static`.
            lead = 0
            while True:
                mm = re.match(r'\s+|//[^\n]*\n?|/\*.*?\*/', head[lead:], re.S)
                if not mm or not mm.group(0):
                    break
                lead += mm.end()
            body = head[lead:]
            body = re.sub(r'^static\s+', '', body)
            if re.search(r'^\s*extern\b', body):
                continue
            new = head[:lead] + 'extern ' + body + ';'
            out = out[:start] + new + out[end:]
            todo.discard(name)
            break
    if todo and strict:
        raise TransformError(f'{label}: definitions not found for externize: {sorted(todo)}')
    return out


def function_spans(text: str):
    """{name: (start, end)} of top-level function definitions."""
    out = {}
    for start, end, is_func in _top_level_statements(text):
        if not is_func:
            continue
        head = text[start:end]
        brace = head.find('{')
        # Comments before the definition ("/// 8013ADB4 - 8013AE30 (124
        # bytes)") are not part of the declarator.
        decl = re.sub(r'/\*.*?\*/|//[^\n]*', ' ', head[:brace], flags=re.S)
        m = re.findall(r'([A-Za-z_][A-Za-z0-9_]*)\s*\(', decl)
        if m:
            name = m[0] if not decl.strip().startswith('(') else m[-1]
            # The declarator name is the identifier right before the first
            # '(' that opens the parameter list.
            out[name] = (start, end)
    return out


def strip_functions(text: str, keep, label: str) -> str:
    """Replace every function definition not in `keep` with its prototype,
    so references bind to the retail function through the absolute symbol
    table."""
    spans = function_spans(text)
    out = text
    for name, (start, end) in sorted(spans.items(), key=lambda kv: -kv[1][0]):
        if name in keep:
            continue
        body = text[start:end]
        header = body[:body.find('{')].rstrip()
        lead = header[:len(header) - len(header.lstrip())]
        header = header.lstrip()
        if header.startswith('static inline') or header.startswith('inline'):
            continue      # leave inline helpers; unused ones emit nothing
        header = re.sub(r'//[^\n]*', '', header)
        header = re.sub(r'/\*.*?\*/', '', header, flags=re.S).rstrip()
        proto = re.sub(r'^static\s+', '', header) + ';'
        out = out[:start] + lead + proto + out[end:]
    return out


def changed_by_text(original: str, edited: str):
    """Functions whose definition text differs (or that are new), plus every
    function that calls a changed static/inline helper, iterated to a fixed
    point. Returns (changed: set, outside_edits: bool) where outside_edits says
    whether file-scope text outside functions changed too."""
    a, b = function_spans(original), function_spans(edited)
    norm = lambda s: re.sub(r'\s+', ' ', s).strip()  # noqa: E731
    changed = set()
    for name, (s, e) in b.items():
        if name not in a or norm(original[a[name][0]:a[name][1]]) != norm(edited[s:e]):
            changed.add(name)
    # Propagate through callers of changed *static/inline* helpers only: those
    # get inlined (or are file-local) so the caller must be recompiled too. A
    # changed global function is patched at its own entry, so its callers can
    # keep calling the retail address.
    def is_local(name):
        s, e = b[name]
        head = edited[s:e]
        head = head[:head.find('{')]
        head = re.sub(r'/\*.*?\*/|//[^\n]*', '', head, flags=re.S).lstrip()
        return head.startswith('static') or head.startswith('inline')
    grew = True
    while grew:
        grew = False
        for name, (s, e) in b.items():
            if name in changed:
                continue
            body = edited[s:e]
            if any(c in b and is_local(c) and re.search(r'\b' + re.escape(c) + r'\s*\(', body) for c in changed):
                changed.add(name)
                grew = True
    def strip_funcs(text, spans):
        out, last = [], 0
        for _, (s, e) in sorted(spans.items(), key=lambda kv: kv[1][0]):
            out.append(text[last:s]); last = e
        out.append(text[last:])
        return norm(''.join(out))
    outside = strip_funcs(original, a) != strip_funcs(edited, b)
    return changed, outside
