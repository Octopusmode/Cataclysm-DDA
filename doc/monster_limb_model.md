# Monster limb model

Monsters in Cataclysm have a single hit point pool and no anatomy. This
document describes the optional limb model which gives them named hit zones,
localised damage and a fluid (blood) channel that converts blood loss into hit
point damage.

The feature is data driven and opt-in per bodytype, so monsters that are not
covered keep the classic behaviour.

## Data types

### `monster_limb_map`

```json
{
  "type": "monster_limb_map",
  "id": "mlm_humanoid",
  "fluid": "mf_blood",
  "bodytypes": [ "human" ],
  "death_conditions": [ "structure_zero", "head_destroyed" ],
  "limbs": [
    { "id": "torso", "hp_frac": 0.40, "blood_frac": 0.55, "vital": true,
      "scores": { "balance": 0.7, "breathing": 1.0 } },
    { "id": "head", "hp_frac": 0.19, "blood_frac": 0.15, "parent": "torso",
      "weakpoint": "head", "is_head": true, "vital": true, "bleed_mult": 3.0,
      "scores": { "senses": 0.5, "bite": 1.0, "reaction": 0.15 } },
    { "id": "arm_l", "hp_frac": 0.10, "weakpoint": "arm", "parent": "torso",
      "severable": true, "severable_bleed_mult": 3, "bleed_mult": 1.5,
      "scores": { "manip": 0.5, "attack": 0.5, "block": 0.5 } }
  ]
}
```

| Field | Meaning |
| --- | --- |
| `fluid` | Fluid the creature circulates. Omit for creatures that do not bleed. |
| `bodytypes` | Bodytypes that use this map when a monster does not set `limb_map`. |
| `death_conditions` | Which events kill this creature. See below. |
| `limbs[].id` | Limb id, unique within the map. |
| `limbs[].hp_frac` | Share of `type->hp` stored in this limb; all shares must sum to 1.0. |
| `limbs[].blood_frac` | Share of the fluid volume stored here; must sum to 1.0. |
| `limbs[].hit_weight` | Relative chance to be hit by an unaimed attack. Derived from weakpoint coverage. |
| `limbs[].weakpoint` | Weakpoint zone id that feeds this limb. This is how hits are localised. |
| `limbs[].parent` | Parent limb, used by the body graph UI. |
| `limbs[].vital` | Limb is listed in a `*_destroyed` death condition. |
| `limbs[].is_head` | Limb counts as a head for headshot purposes. |
| `limbs[].severable`, `severable_bleed_mult` | Limb can be severed; bleeding multiplier when it is. |
| `limbs[].bleed_mult` | Bleeding multiplier while the limb is merely wounded. |
| `limbs[].scores` | Capability contribution, keyed by `limb_score` id. |
| `limbs[].graph_sym` | Single character that draws this limb in `rows`. |
| `rows` | ASCII figure of the creature, one character per cell. Optional. |

### `monster_fluid`

```json
{
  "type": "monster_fluid",
  "id": "mf_blood",
  "name": "blood",
  "hp_per_unit": 25,
  "color": "red",
  "field_on_ground": "fd_blood",
  "clot_factor": 0.985,
  "bleed_factor": 1.0,
  "debuff_curve": [ 0.7, 0.5, 0.3 ]
}
```

| Field | Meaning |
| --- | --- |
| `hp_per_unit` | Hit points lost per 100 units of fluid drained. |
| `field_on_ground` | Field dropped where the creature bleeds. Omit for fluids that do not pool. |
| `clot_factor` | Per-turn wound decay; `1.0` means the wound never clots. |
| `bleed_factor` | How readily this fluid opens wounds, relative to blood. |
| `debuff_curve` | Reserved for the fluid-index debuffs. |

`data/json/monster_limb_maps/monster_limb_scores.json` adds the `attack`,
`bite` and `senses` scores used by monster limb maps; the rest (`move_speed`,
`reaction`, `manip`, `block`, `balance`) are the shared character scores.

## Resolution

A monster's limb map is chosen in this order:

1. the `limb_map` field of the monster definition;
2. the `bodytypes` list of a limb map;
3. `mlm_single`, a single `body` limb, which reproduces the old behaviour.

`mlm_single` is a valid map, not a special case: monsters using it never enter
the limb code paths.

### Shipped maps

| Map | Bodytypes | Fluid | Death conditions |
| --- | --- | --- | --- |
| `mlm_humanoid` | `human` | `mf_blood` | `structure_zero`, `head_destroyed` |
| `mlm_quadruped` | `dog`, `horse`, `pig`, `cow`, `bear`, `kangaroo`, `lizard`, `turtle` | `mf_blood` | `structure_zero`, `head_destroyed` |
| `mlm_arthropod` | `insect`, `flying insect`, `spider`, `crab` | `mf_ichor` | `structure_zero` |
| `mlm_serpent` | `snake` | `mf_blood` | `structure_zero`, `head_destroyed` |
| `mlm_blob` | `blob` | `mf_ichor` | `structure_zero` |
| `mlm_amphibian` | `frog` | `mf_blood` | `structure_zero`, `head_destroyed` |
| `mlm_bird` | `bird` | `mf_blood` | `structure_zero`, `head_destroyed` |
| `mlm_migo` | `migo` | `mf_spore` | `structure_zero`, `head_destroyed` |
| `mlm_single` | everything else | `mf_blood` | `structure_zero` |

Arthropods and blobs deliberately do not die when a limb is destroyed: a roach
keeps running without its head and a blob has no head at all, so their maps only
list `structure_zero`. Their fluid is ichor rather than blood, and blobs lose
hit points as they leak without that being a death condition on its own — this
is what makes the model data driven rather than a reskin of the human rules.

Not every weakpoint zone becomes a limb. Zones that represent armour
(`armor_thick`), transient conditions (`part_grabbing`, `wings_down`) or a body
region that already belongs to the torso (`abdomen`, `the chest`, `the vitals`)
are left out: a hit on them resolves to no limb and therefore lands on the
default torso limb, exactly as intended.

> **Gotcha.** A weakpoint without an explicit `id` takes its `name` as the id,
> verbatim and un-slugged (`weakpoint::load()`). `wps_animal_quadruped` and
> `wps_bird_body` are written that way, so their zones are called `the head`,
> `the leg`, `the wing`. The generator mirrors this rule; a limb map that
> references `head` against those sets silently matches nothing.

`fish`, `trifacet` and `apeirohedra` deliberately stay on `mlm_single`: their
monsters carry no weakpoint sets with named zones, so there is nothing to
localise damage onto. Giving them a limb map would be cosmetic only.

A wound that keeps losing fluid also pools on the ground, using the fluid's
`field_on_ground` (blood for most creatures). The rate is sparse — one field
roughly every third turn of active bleeding — so it reads as a trail without
turning combat into field spam. Fluids without `field_on_ground` (spores) leave
nothing behind.

## Damage flow

1. `monster::absorb_hit()` selects a weakpoint, records the limb that matches
   that weakpoint in `pending_hit_limb`, and applies armour multipliers.
2. `monster::apply_damage()` subtracts the damage from that limb. If the limb
   would go below zero, the surplus spills onto the torso first and then onto
   the remaining limbs, so hacking at an arm can still kill a creature.
3. `hp` is always recomputed as the sum of limb hit points, which keeps every
   existing `get_hp()` user (AI threat evaluation, health bars, revival)
   working unchanged.

Damage also opens a wound:

```
wound += damage * fluid.bleed_factor * limb.bleed_mult / 10
```

## Bleeding

Once per turn `monster::process_bleeding()`:

```
lost   = sum of all limb wounds
fluid -= lost
wound  = floor( wound * fluid.clot_factor )
hp    -= lost * fluid.hp_per_unit / 100
```

Fractions are carried between turns, so slow bleeds still hurt. When the
fluid reservoir is empty the wounds close; the creature has bled out.

Only monsters with a multi-limb map bleed. `mlm_single` creatures take damage
exactly as before.

## Capability penalties

`monster::limb_score( id )` sums the limb contributions scaled by limb health,
using the same curve as `bodypart::wound_adjusted_limb_value()`: no penalty
above 75% health, then linear down to zero. It returns `1.0` when the monster
has no anatomy or the map does not define that score, so nothing changes for
unaffected monsters.

Current consumers:

| Score | Effect |
| --- | --- |
| `move_speed` | `monster::get_speed()` |
| `reaction` | `monster::hit_roll()` |
| `attack` | melee damage in `monster::melee_attack()` |
| `balance` | `monster::get_dodge_base()` |
| `bite` | special attacks whose name contains `bite` are disabled below 0.5 |
| `manip` | special attacks whose name contains `grab`/`grasp` are disabled below 0.5 |

## Death conditions

`structure_zero` is always implied: when the sum of the limbs reaches zero the
creature dies. Additional conditions are opt-in per map:

| Condition | Meaning |
| --- | --- |
| `<limb>_destroyed` | The named limb was destroyed, e.g. `head_destroyed`. |
| `fluid_zero` | The fluid reservoir emptied. |
| `incapacitated` | Reserved. |

Meeting any listed condition sets the creature's hp to zero, so the regular
death handling (corpse, drops, events) runs unchanged. This keeps
non-anthropomorphic creatures alive when a "head" is destroyed: they simply do
not list that condition.

## Aimed strikes

A player can deliberately aim at one zone instead of letting the weakpoint roll
decide. The choice travels as `Character::aimed_zone` and is consumed by the
next attack:

* **Guns** — cycle the fire mode to *Targeted*, then fire. The zone menu appears
  before the shot and also shows guns' real hit chance per zone.
* **Melee and reach weapons** — press the `Attack a chosen body part` hotkey
  (default `Ctrl`+`f`). If several creatures are in reach the game asks which
  one first.

`weakpoint_attack::forced_zone` makes `weakpoints::select_weakpoint()` return
the named weakpoint unconditionally, so a called shot always lands where the
player pointed. `weakpoints::hit_chance_of()` is the same probability
calculation the roll uses, which is what the menu displays — the numbers are
the real odds, not an approximation.

Only creatures whose limb map has more than one entry (that is, anything but
`mlm_single`) can be aimed at. Zones that several limbs share, such as
`arm_l`/`arm_r` answering to the single `arm` weakpoint, are presented as one
menu entry because a called shot can only name the zone.

The menu draws the creature next to the list. The figure comes from the limb
map's own `rows`, using each limb's `graph_sym`, so non-humanoid creatures get
their own silhouette instead of a borrowed human one:

```json
"rows": [
  "     e e    ",
  "    @@@@@   ",
  "   AATTTaa  ",
  "   AATTTaa  ",
  "   AATTTaa  ",
  "     TTT    ",
  "     TTT    ",
  "    L   r   ",
  "    L   r   ",
  "    L   r   "
]
```

`@` is the head, `e` an eye, `T` the torso, `A`/`a` the left and right arm,
`L`/`r` the left and right leg. Nothing in the renderer knows what a human
looks like, so `mlm_quadruped` simply supplies a different figure.

The limb under the cursor is highlighted; limbs that are already destroyed are
dimmed. `monster_limb_map::check_figure()` rejects a figure whose rows have
different lengths, that uses a character naming no limb, or that omits a limb
that has a `graph_sym`.

## Generating maps

`tools/gen_monster_limb_maps.py` turns weakpoint sets into draft limb maps:

```
python3 tools/gen_monster_limb_maps.py --report
python3 tools/gen_monster_limb_maps.py --emit data/json/monster_limb_maps
```

`--report` prints every weakpoint set with a suggested role (`limb`, `armour`,
`situational`) so the mapping can be reviewed cheaply; `--emit` writes the
curated maps plus the fluid definitions. Run `--check-mods dda` afterwards to
validate the result.
