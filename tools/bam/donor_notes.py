"""Per-character choices for borrowed moves, in one place.

Most of what a borrowed move needs is read from the game's own files
(bonetables.py, parts.py). What cannot be read, because it is a judgment
about how a character's move should look on someone else, is listed here.
Each entry says why it exists. The C side's character-specific handling is
listed in docs/DONOR_NOTES.md, which points back here.
"""

# Fighter kind (internal order, enum FighterKind) -> two-letter file code
# (Pl<code>.dat moves, Pl<code>Nr.dat neutral costume).
FILE_CODES = {0: 'Mr', 1: 'Fx', 2: 'Ca', 3: 'Dk', 4: 'Kb', 5: 'Kp', 6: 'Lk', 7: 'Sk', 8: 'Ns', 9: 'Pe',
              10: 'Pp', 11: 'Nn', 12: 'Pk', 13: 'Ss', 14: 'Ys', 15: 'Pr', 16: 'Mt', 17: 'Lg', 18: 'Ms',
              19: 'Zd', 20: 'Cl', 21: 'Dr', 22: 'Fc', 23: 'Pc', 24: 'Gw', 25: 'Gn', 26: 'Fe'}
# For the bone tables: Nana's moves and skeleton are Popo's.
KINDS = {**FILE_CODES, 11: 'Pp'}
KIND_NAMES = ['Mario', 'Fox', 'Captain Falcon', 'Donkey Kong', 'Kirby', 'Bowser', 'Link', 'Sheik', 'Ness',
              'Peach', 'Popo', 'Nana', 'Pikachu', 'Samus', 'Yoshi', 'Jigglypuff', 'Mewtwo', 'Luigi', 'Marth',
              'Zelda', 'Young Link', 'Dr. Mario', 'Falco', 'Pichu', 'Mr. Game & Watch', 'Ganondorf', 'Roy']

# Held items drawn in the borrower's hand for a donor's weapon:
# code -> [(item, donor joint its hitboxes ride on or None for the most-used
# body-less one, motion name prefixes or None for every motion)].
# Kirby's hammer and Final Cutter sword are articles the game spawns itself,
# so Kirby has none. Peach's parasol is an article other fighters do not
# spawn; it is drawn on the hand bone she holds it (and Toad) by.
WEAPONS = {
    'Ms': [('sword', None, None)], 'Fe': [('sword', None, None)],
    'Lk': [('sword', None, None)], 'Cl': [('sword', None, None)],
    'Pp': [('hammer', 15, None)],
    'Pe': [('parasol', 109, ('SpecialHi', 'SpecialAirHi'))],
}
# Item numbers the C side uses (sword_visual.c weapon_model).
ITEMS = {'sword': 0, 'hammer': 1, 'parasol': 2}
# Items drawn exactly on their bone (articles), not laid along hitboxes.
ON_BONE = {'parasol'}

# Donor-only bones a move animates without hitboxes on them, whose mesh is
# drawn on the borrower during those motions: code -> [(joint, motion name
# prefixes)].
EXTRA_PARTS = {
    # Yoshi's tongue (joint 41, stretched by its scale track) shares a PObj
    # with bits of his mouth bound to his head; those joints are collapsed
    # while it shows, so only the tongue is drawn.
    'Ys': [(41, ('SpecialN', 'SpecialAirN'))],
    # Peach's dress: eight chains of skirt bones under joint 17 that her
    # down smash spins out (the tips; their DObjs 44/45 join by weight).
    'Pe': [(j, ('AttackLw4',)) for j in (23, 29, 35, 41, 47, 53, 59, 65)],
}

# Donor joints posed through the donor's own whole skeleton from its root,
# not through the borrower's limbs: Mr. Game & Watch's hitboxes all hang
# from his root, so what he holds has to be where his own arm puts it.
ROOT_CHAINS = {
    # 1: parachute; 17: box, key, pan, turtle (aerials); 21: Sparky;
    # 18 (left hand): torch, manhole, greenhouse; 32 (right thumb): Judge.
    'Gw': [1, 17, 21, 18, 32],
}

# Joints rebuilt as props although the game lists them as a body part:
# code -> [joint]. Kirby's hammer and Final Cutter hitboxes ride on joint 44,
# his right-hand finger part; as a finger it would keep the borrower's own
# finger pose, so it is rebuilt from his hand with his pose instead.
AS_PROPS = {
    'Kb': [44],
}

# Body parts a rebuilt bone's mesh may hang from and still be drawn: only
# appendages the borrower has no counterpart of (tails, hanging from the
# hips or torso) and held weapons. A rebuilt hand or foot would be a second,
# floating limb.
CORE_PARTS = {0, 1, 2, 3, 4, 5, 16, 17}

# Model-part groups drawn from the donor's model for its moves (sword_visual.c
# vis_donors): kind -> [(group, lowest variant drawn)]. Their meshes go into
# the trimmed models (parts.py).
VIS_DONORS = {
    24: [(5, 0), (6, 0), (7, 0), (8, 0)],  # Mr. Game & Watch's props
    9: [(4, 1), (3, 0)],                   # Peach's crown in hand; forward smash items
    4: [(0, 2)],                           # Kirby's stone
    14: [(0, 1)],                          # Yoshi's egg
    13: [(0, 2)],                          # Samus's Morph Ball
}
