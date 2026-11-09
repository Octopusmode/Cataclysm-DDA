#!/usr/bin/env python3
"""Generate draft monster limb maps from the existing weakpoint sets.

The weakpoint system already encodes a per-archetype hit-zone vocabulary
(``arm``, ``leg``, ``head``, ``eye``, ``abdomen``, ...).  This tool reuses that
data to produce draft ``monster_limb_map`` JSON objects, so that the anatomy of
1156 monster definitions does not have to be authored by hand.

Usage::

    python3 tools/gen_monster_limb_maps.py --report
    python3 tools/gen_monster_limb_maps.py --emit <outdir>

``--report`` prints every weakpoint set with a *suggested* role for each entry
(limb / armour zone / situational variant), so the mapping can be reviewed
cheaply.  ``--emit`` writes the curated maps (see ``CURATED``) plus fluids.

Nothing here is loaded by the game yet: the output is a draft for
``data/mods/limb_model/`` and only becomes valid JSON once the C++ side
registers the ``monster_limb_map`` and ``monster_fluid`` types.
"""

from __future__ import annotations

import argparse
import json
import re
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent
WEAKPOINTS_DIR = REPO_ROOT / "data" / "json" / "monster_weakpoints"

# ---------------------------------------------------------------------------
# Classification
# ---------------------------------------------------------------------------

#: Zones that describe armour plating or structural gaps, not anatomy.
ARMOUR_ZONE = re.compile(
    r"(armou?r|helmet|gasmask|hard_hide|hard_point|heavy_armor|^gap$|joint$|"
    r"^(armou?r_)?shattered|weak_joint|weak_point|crystal|vulnerable|"
    r"insensitive|small_gap|bark|shell)",
    re.IGNORECASE,
)

#: Zones that only exist while some condition holds and belong to a parent limb.
SITUATIONAL = {
    "part_grabbing": "arm",
    "mouth_grabbing": "mouth",
    "tongue_grabbing": "mouth",
    "wing_stagger": "wing",
    "wing_down": "wing",
    "crushed": "torso",
}

#: Bilateral zones that must become two limbs to support crippling.
BILATERAL = {"arm", "leg", "wing", "foreleg", "hindleg", "eye", "hand", "foot", "claw"}

#: Extra limb ids that are not present in weakpoint sets but that a limb map
#: needs in order to keep hp fractions sane (torso is always the remainder).
IMPLICIT_TORSO = "torso"


def slug(text: str) -> str:
    text = text.strip().lower()
    text = re.sub(r"^the\s+", "", text)
    text = re.sub(r"[^a-z0-9_]+", "_", text)
    return text.strip("_")


def zone_id(wp: dict, index: int) -> str:
    """Mirror weakpoint::load(): the id defaults to the name verbatim, not a slug."""
    if "id" in wp:
        return wp["id"]
    if "name" in wp:
        return wp["name"]
    return f"zone_{index}"


def classify(zone: str) -> str:
    """Return one of: limb, armour, situational."""
    if zone in SITUATIONAL:
        return "situational"
    if ARMOUR_ZONE.search(zone):
        return "armour"
    return "limb"


# ---------------------------------------------------------------------------
# Loading
# ---------------------------------------------------------------------------


def load_sets() -> dict[str, list[dict]]:
    sets: dict[str, list[dict]] = {}
    for path in sorted(WEAKPOINTS_DIR.glob("*.json")):
        with path.open(encoding="utf-8") as handle:
            data = json.load(handle)
        for entry in data:
            if entry.get("type") == "weakpoint_set":
                sets[entry["id"]] = entry.get("weakpoints", [])
    return sets


def report(sets: dict[str, list[dict]]) -> None:
    for set_id, weakpoints in sets.items():
        print(f"\n=== {set_id} ({len(weakpoints)} zones) ===")
        for index, wp in enumerate(weakpoints):
            zone = zone_id(wp, index)
            role = classify(zone)
            coverage = wp.get("coverage", 100)
            bits = [f"{zone:<24}", f"{role:<12}", f"cov {coverage:<5}"]
            if wp.get("is_head"):
                bits.append("is_head")
            if "condition" in wp:
                bits.append("conditional")
            print("  " + " ".join(bits))


# ---------------------------------------------------------------------------
# Map construction
# ---------------------------------------------------------------------------


def build_limbs(weakpoints: list[dict], *, skip: set[str] = frozenset()) -> tuple[list[dict], list[str]]:
    """Turn weakpoints into limb entries with normalised coverage."""
    limbs: list[dict] = []
    dropped: list[str] = []

    for index, wp in enumerate(weakpoints):
        zone = zone_id(wp, index)
        role = classify(zone)
        if zone in skip or role == "armour":
            dropped.append(zone)
            continue
        if role == "situational":
            zone = SITUATIONAL[zone]
            # The parent limb already carries the coverage; do not double count.
            dropped.append(f"{zone_id(wp, index)}->{zone}")
            continue

        coverage = float(wp.get("coverage", 0))
        entry = {
            "id": zone,
            "weakpoint": wp["id"] if "id" in wp else zone,
            "hit_weight": coverage,
        }
        if wp.get("is_head"):
            entry["is_head"] = True
        limbs.append(entry)

        if zone in BILATERAL:
            entry["pair"] = f"{zone}_r"
            entry["id"] = f"{zone}_l"
            mirrored = dict(entry)
            mirrored["id"] = f"{zone}_r"
            mirrored["pair"] = f"{zone}_l"
            limbs.append(mirrored)

    total = sum(l["hit_weight"] for l in limbs) or 1.0
    for limb in limbs:
        limb["hit_frac"] = round(limb["hit_weight"] / total, 4)
    return limbs, dropped


#: Curated maps for the Romero-zombie scope.  ``zones`` maps a weakpoint zone to
#: the limbs it feeds (``None`` = deliberately dropped); ``hp_frac``/``scores``
#: are authored by hand.  Scores mirror data/json/body_parts.json so mod authors
#: recognise them.
CURATED: dict[str, dict] = {
    "mlm_humanoid": {
        "source": "wps_humanoid_body",
        "fluid": "mf_blood",
        "death_conditions": ["structure_zero", "head_destroyed"],
        "zones": {
            "arm": ["arm_l", "arm_r"],
            "leg": ["leg_l", "leg_r"],
            "head": ["head"],
            "eye": ["eye"],
            "part_grabbing": None,
        },
        "hp_frac": {
            "torso": 0.40,
            "head": 0.19,
            "eye": 0.01,
            "arm_l": 0.10,
            "arm_r": 0.10,
            "leg_l": 0.10,
            "leg_r": 0.10,
        },
        "blood_mult": {"torso": 1.5, "head": 0.5, "eye": 0.5, "arm_l": 0.8, "arm_r": 0.8,
                       "leg_l": 0.8, "leg_r": 0.8},
        "bodytypes": ["human"],
        "parent": {"head": "torso", "eye": "head", "arm_l": "torso", "arm_r": "torso",
                   "leg_l": "torso", "leg_r": "torso"},
        "scores": {
            "head": {"senses": 0.5, "bite": 1.0, "reaction": 0.15},
            "eye": {"senses": 0.5},
            "torso": {"balance": 0.7, "breathing": 1.0},
            "arm_l": {"manip": 0.5, "attack": 0.5, "block": 0.5},
            "arm_r": {"manip": 0.5, "attack": 0.5, "block": 0.5},
            "leg_l": {"move_speed": 0.5, "balance": 0.15},
            "leg_r": {"move_speed": 0.5, "balance": 0.15},
        },
        "bleed_mult": {"head": 3.0, "arm_l": 1.5, "arm_r": 1.5, "leg_l": 1.2, "leg_r": 1.2},
        "vital": ["head", "torso"],
        "severable": ["arm_l", "arm_r", "leg_l", "leg_r"],
        "sever_bleed": {"arm_l": 3, "arm_r": 3, "leg_l": 2, "leg_r": 2},
        "graph_syms": {"torso": "T", "head": "@", "eye": "e", "arm_l": "A", "arm_r": "a",
                       "leg_l": "L", "leg_r": "r"},
        "figure": [
            "     e e    ",
            "    @@@@@   ",
            "   AATTTaa  ",
            "   AATTTaa  ",
            "   AATTTaa  ",
            "     TTT    ",
            "     TTT    ",
            "    L   r   ",
            "    L   r   ",
            "    L   r   ",
        ],
    },
    "mlm_quadruped": {
        "source": "wps_animal_quadruped",
        "fluid": "mf_blood",
        "death_conditions": ["structure_zero", "head_destroyed"],
        "zones": {
            "the head": ["head"],
            "the eye": ["eye"],
            "the leg": ["foreleg_l", "foreleg_r", "hindleg_l", "hindleg_r"],
            # Hits on these land on the torso limb (the default zone) anyway.
            "the chest": None,
            "the vitals": None,
        },
        "hp_frac": {
            "torso": 0.46,
            "head": 0.17,
            "eye": 0.01,
            "foreleg_l": 0.09,
            "foreleg_r": 0.09,
            "hindleg_l": 0.09,
            "hindleg_r": 0.09,
        },
        "blood_mult": {"torso": 1.5, "head": 0.5, "eye": 0.5,
                       "foreleg_l": 0.8, "foreleg_r": 0.8, "hindleg_l": 0.8, "hindleg_r": 0.8},
        "bodytypes": ["dog", "horse", "pig", "cow", "bear", "kangaroo", "lizard", "turtle"],
        "parent": {"head": "torso", "eye": "head", "foreleg_l": "torso", "foreleg_r": "torso",
                   "hindleg_l": "torso", "hindleg_r": "torso"},
        "scores": {
            "head": {"senses": 0.5, "bite": 1.0, "reaction": 0.15},
            "eye": {"senses": 0.5},
            "torso": {"balance": 0.7},
            "foreleg_l": {"move_speed": 0.25, "balance": 0.2},
            "foreleg_r": {"move_speed": 0.25, "balance": 0.2},
            "hindleg_l": {"move_speed": 0.25, "balance": 0.2},
            "hindleg_r": {"move_speed": 0.25, "balance": 0.2},
        },
        "bleed_mult": {"head": 3.0},
        "vital": ["head", "torso"],
        "severable": ["foreleg_l", "foreleg_r", "hindleg_l", "hindleg_r"],
        "sever_bleed": {"foreleg_l": 2, "foreleg_r": 2, "hindleg_l": 2, "hindleg_r": 2},
        "graph_syms": {"torso": "T", "head": "@", "eye": "e", "foreleg_l": "F",
                       "foreleg_r": "f", "hindleg_l": "H", "hindleg_r": "h"},
        "figure": [
            "     @e@    ",
            "    TTTTT   ",
            "   TTTTTTT  ",
            "  F T   T f ",
            "  F T   T f ",
            "  H       h ",
            "  H       h ",
        ],
    },
    "mlm_arthropod": {
        "source": "wps_arthropod",
        "fluid": "mf_ichor",
        # No head condition: a roach keeps running after decapitation.
        "death_conditions": ["structure_zero"],
        "zones": {
            "leg": ["leg_l", "leg_r"],
            "head": ["head"],
            "eye": ["eye"],
            "mandible": ["mandible"],
            "antenna": ["antenna"],
            # Hits on these land on the torso limb (the default zone) anyway.
            "abdomen": None,
            "leg_joint": None,
            "body_joint": None,
            "armor_thick": None,
        },
        "hp_frac": {
            "torso": 0.57,
            "head": 0.12,
            "eye": 0.01,
            "mandible": 0.02,
            "antenna": 0.03,
            "leg_l": 0.125,
            "leg_r": 0.125,
        },
        "blood_mult": {"torso": 1.5, "head": 0.5, "eye": 0.5, "mandible": 0.3,
                       "antenna": 0.3, "leg_l": 0.8, "leg_r": 0.8},
        "bodytypes": ["insect", "flying insect", "spider", "crab"],
        "parent": {"head": "torso", "eye": "head", "mandible": "head", "antenna": "head",
                   "leg_l": "torso", "leg_r": "torso"},
        "scores": {
            "torso": {"balance": 0.7},
            "head": {"senses": 0.5, "bite": 1.0, "reaction": 0.15},
            "eye": {"senses": 0.5},
            "mandible": {"attack": 1.0},
            "leg_l": {"move_speed": 0.5, "balance": 0.15},
            "leg_r": {"move_speed": 0.5, "balance": 0.15},
        },
        "bleed_mult": {"head": 2.0},
        "severable": ["leg_l", "leg_r"],
        "sever_bleed": {"leg_l": 2, "leg_r": 2},
        "graph_syms": {"torso": "B", "head": "H", "eye": "e", "mandible": "m",
                       "antenna": "A", "leg_l": "L", "leg_r": "r"},
        "figure": [
            "   A   A    ",
            "    HHH     ",
            "   HHeHH    ",
            "    mmm     ",
            "  L BBBBB r ",
            "  L BBBBB r ",
            "  L BBBBB r ",
        ],
    },
    "mlm_serpent": {
        # Snakes reuse the arthropod weakpoint set, so only the anatomy differs.
        "source": "wps_arthropod",
        "fluid": "mf_blood",
        "death_conditions": ["structure_zero", "head_destroyed"],
        "zones": {
            "head": ["head"],
            "eye": ["eye"],
            "mandible": ["mandible"],
            "leg": None,
            "leg_joint": None,
            "body_joint": None,
            "armor_thick": None,
            "antenna": None,
            "abdomen": None,
        },
        "hp_frac": {"torso": 0.80, "head": 0.15, "eye": 0.01, "mandible": 0.04},
        "blood_mult": {"torso": 1.5, "head": 0.5, "eye": 0.5, "mandible": 0.3},
        "bodytypes": ["snake"],
        "parent": {"head": "torso", "eye": "head", "mandible": "head"},
        "scores": {
            # The whole body is the locomotor, so nothing slows down until it is hurt.
            "torso": {"move_speed": 1.0, "balance": 0.7},
            "head": {"senses": 0.5, "bite": 1.0, "reaction": 0.15},
            "eye": {"senses": 0.5},
            "mandible": {"attack": 1.0},
        },
        "bleed_mult": {"head": 2.0},
        "vital": ["torso"],
        "graph_syms": {"torso": "B", "head": "H", "eye": "e", "mandible": "m"},
        "figure": [
            "    HHH     ",
            "   HHe      ",
            "   mmm      ",
            "    BBB     ",
            "   BBB      ",
            "    BBBB    ",
            "      BBB   ",
        ],
    },
    "mlm_blob": {
        "source": "wps_slime",
        "fluid": "mf_ichor",
        # Fluid loss hurts but never kills a blob outright.
        "death_conditions": ["structure_zero"],
        "zones": {
            "organ": ["organ"],
            "pseudopod": ["pseudopod"],
            "body": None,
            "insensitive_spot": None,
            "fragment": None,
        },
        "hp_frac": {"torso": 0.86, "organ": 0.06, "pseudopod": 0.08},
        "blood_mult": {"torso": 1.5, "organ": 0.8, "pseudopod": 0.6},
        "bodytypes": ["blob"],
        "parent": {"organ": "torso", "pseudopod": "torso"},
        "scores": {
            "torso": {"move_speed": 1.0, "balance": 0.7},
            "pseudopod": {"attack": 1.0},
        },
        "graph_syms": {"torso": "B", "organ": "O", "pseudopod": "P"},
        "figure": [
            "    BBBB    ",
            "  BBBBBBBB  ",
            " BBBBOBBBBB ",
            " BBBBBBBBBB ",
            "  PBBBBBBP  ",
            "   P    P   ",
        ],
    },
    "mlm_amphibian": {
        "source": "wps_amphibian_body",
        "fluid": "mf_blood",
        "death_conditions": ["structure_zero", "head_destroyed"],
        "zones": {
            "head": ["head"],
            "eye": ["eye"],
            "mouth": ["mouth"],
            "foreleg": ["foreleg_l", "foreleg_r"],
            "hindleg": ["hindleg_l", "hindleg_r"],
            "belly": None,
            "mouth_grabbing": None,
        },
        "hp_frac": {
            "torso": 0.50,
            "head": 0.15,
            "eye": 0.01,
            "mouth": 0.04,
            "foreleg_l": 0.10,
            "foreleg_r": 0.10,
            "hindleg_l": 0.05,
            "hindleg_r": 0.05,
        },
        "blood_mult": {"torso": 1.5, "head": 0.5, "eye": 0.5, "mouth": 0.3,
                       "foreleg_l": 0.8, "foreleg_r": 0.8,
                       "hindleg_l": 0.8, "hindleg_r": 0.8},
        "bodytypes": ["frog"],
        "parent": {"head": "torso", "eye": "head", "mouth": "head",
                   "foreleg_l": "torso", "foreleg_r": "torso",
                   "hindleg_l": "torso", "hindleg_r": "torso"},
        "scores": {
            "torso": {"balance": 0.7},
            "head": {"senses": 0.5, "reaction": 0.15},
            "eye": {"senses": 0.5},
            "mouth": {"bite": 1.0, "attack": 1.0},
            "foreleg_l": {"move_speed": 0.15, "balance": 0.075},
            "foreleg_r": {"move_speed": 0.15, "balance": 0.075},
            "hindleg_l": {"move_speed": 0.35, "balance": 0.075},
            "hindleg_r": {"move_speed": 0.35, "balance": 0.075},
        },
        "bleed_mult": {"head": 2.0},
        "vital": ["torso"],
        "severable": ["foreleg_l", "foreleg_r", "hindleg_l", "hindleg_r"],
        "sever_bleed": {"foreleg_l": 2, "foreleg_r": 2, "hindleg_l": 2, "hindleg_r": 2},
        "graph_syms": {"torso": "B", "head": "H", "eye": "e", "mouth": "m",
                       "foreleg_l": "F", "foreleg_r": "f",
                       "hindleg_l": "G", "hindleg_r": "g"},
        "figure": [
            "    HHH     ",
            "   HHeHH    ",
            "    mmm     ",
            " FfBBBBB    ",
            " FfBBBBB    ",
            "   Gg Gg    ",
        ],
    },
    "mlm_bird": {
        # wps_bird_body names its weakpoints instead of giving them ids; the engine
        # uses the name as the id, and so does the generator.
        "source": "wps_bird_body",
        "fluid": "mf_blood",
        "death_conditions": ["structure_zero", "head_destroyed"],
        "zones": {
            "the head": ["head"],
            "the eye": ["eye"],
            "the wing": ["wing_l", "wing_r"],
            "the belly": None,
        },
        "hp_frac": {
            "torso": 0.62,
            "head": 0.16,
            "eye": 0.01,
            "wing_l": 0.105,
            "wing_r": 0.105,
        },
        "blood_mult": {"torso": 1.5, "head": 0.5, "eye": 0.5,
                       "wing_l": 0.6, "wing_r": 0.6},
        "bodytypes": ["bird"],
        "parent": {"head": "torso", "eye": "head", "wing_l": "torso", "wing_r": "torso"},
        "scores": {
            "torso": {"balance": 0.7, "move_speed": 0.4},
            "head": {"senses": 0.5, "reaction": 0.15},
            "eye": {"senses": 0.5},
            "wing_l": {"move_speed": 0.3},
            "wing_r": {"move_speed": 0.3},
        },
        "bleed_mult": {"head": 2.0},
        "vital": ["torso"],
        "severable": ["wing_l", "wing_r"],
        "sever_bleed": {"wing_l": 2, "wing_r": 2},
        "graph_syms": {"torso": "B", "head": "H", "eye": "e",
                       "wing_l": "W", "wing_r": "w"},
        "figure": [
            "    HHH     ",
            "   HHe      ",
            "  WWBBBww   ",
            " WWWBBBwww  ",
            "   BBBB     ",
            "   BBBB     ",
        ],
    },
    "mlm_migo": {
        "source": "wps_mi-go",
        "fluid": "mf_spore",
        "death_conditions": ["structure_zero", "head_destroyed"],
        "zones": {
            "arm": ["arm_l", "arm_r"],
            "leg": ["leg_l", "leg_r"],
            "wing": ["wing_l", "wing_r"],
            "head": ["head"],
        },
        "hp_frac": {
            "torso": 0.42,
            "head": 0.14,
            "arm_l": 0.10,
            "arm_r": 0.10,
            "leg_l": 0.10,
            "leg_r": 0.10,
            "wing_l": 0.02,
            "wing_r": 0.02,
        },
        "blood_mult": {"torso": 1.5, "head": 0.5, "arm_l": 0.8, "arm_r": 0.8,
                       "leg_l": 0.8, "leg_r": 0.8, "wing_l": 0.4, "wing_r": 0.4},
        "bodytypes": ["migo"],
        "parent": {"head": "torso", "arm_l": "torso", "arm_r": "torso",
                   "leg_l": "torso", "leg_r": "torso",
                   "wing_l": "torso", "wing_r": "torso"},
        "scores": {
            "torso": {"balance": 0.7},
            "head": {"senses": 1.0, "bite": 1.0, "reaction": 0.15},
            "arm_l": {"manip": 0.5, "attack": 0.5, "block": 0.5},
            "arm_r": {"manip": 0.5, "attack": 0.5, "block": 0.5},
            "leg_l": {"move_speed": 0.4, "balance": 0.15},
            "leg_r": {"move_speed": 0.4, "balance": 0.15},
            "wing_l": {"move_speed": 0.1},
            "wing_r": {"move_speed": 0.1},
        },
        "bleed_mult": {"head": 2.0},
        "vital": ["torso"],
        "severable": ["arm_l", "arm_r", "leg_l", "leg_r", "wing_l", "wing_r"],
        "sever_bleed": {"arm_l": 3, "arm_r": 3, "leg_l": 2, "leg_r": 2,
                        "wing_l": 2, "wing_r": 2},
        "graph_syms": {"torso": "B", "head": "H", "arm_l": "A", "arm_r": "a",
                       "leg_l": "L", "leg_r": "r", "wing_l": "W", "wing_r": "w"},
        "figure": [
            "   WW  ww   ",
            "    HHH     ",
            "  A BBBBB a ",
            "  A BBBBB a ",
            "   LBBBBB   ",
            "   L     r  ",
            "   L     r  ",
        ],
    },
}


def emit_map(map_id: str, spec: dict, sets: dict[str, list[dict]]) -> dict:
    source = spec["source"]
    if source not in sets:
        raise KeyError(f"weakpoint set '{source}' not found (needed by {map_id})")

    zone_coverages = {zone_id(wp, i): float(wp.get("coverage", 0)) for i, wp in enumerate(sets[source])}
    is_head = {zone_id(wp, i): bool(wp.get("is_head")) for i, wp in enumerate(sets[source])}

    limits: dict[str, dict] = {}
    dropped: list[str] = []

    for zone, targets in spec["zones"].items():
        if targets is None:
            dropped.append(zone)
            continue
        if zone not in zone_coverages:
            print(f"  ! {map_id}: zone '{zone}' missing from {source}", file=sys.stderr)
            continue
        share = zone_coverages[zone] / len(targets)
        for target in targets:
            limits[target] = {
                "hit_weight": round(share, 2),
                "weakpoint": zone,
                "is_head": is_head[zone],
            }

    # Every limb listed in hp_frac must exist; torso is synthetic (default zone).
    out_limbs: list[dict] = []
    blood_raw: dict[str, float] = {}
    for limb_id, hp_frac in spec["hp_frac"].items():
        info = limits.get(limb_id)
        entry: dict = {"id": limb_id, "hp_frac": hp_frac}
        if info is None:
            entry["hit_weight"] = 0
        else:
            entry["hit_weight"] = info["hit_weight"]
            entry["weakpoint"] = info["weakpoint"]
            if info["is_head"]:
                entry["is_head"] = True
        if limb_id in spec["parent"]:
            entry["parent"] = spec["parent"][limb_id]
        blood_raw[limb_id] = hp_frac * spec.get("blood_mult", {}).get(limb_id, 1.0)
        if limb_id in spec.get("vital", []):
            entry["vital"] = True
        if limb_id in spec.get("scores", {}):
            entry["scores"] = spec["scores"][limb_id]
        if limb_id in spec.get("bleed_mult", {}):
            entry["bleed_mult"] = spec["bleed_mult"][limb_id]
        if limb_id in spec.get("severable", []):
            entry["severable"] = True
            entry["severable_bleed_mult"] = spec.get("sever_bleed", {}).get(limb_id, 2)
        if limb_id in spec.get("graph_syms", {}):
            entry["graph_sym"] = spec["graph_syms"][limb_id]
        out_limbs.append(entry)

    # Blood fractions are weights normalised to 1.0 so they can never drift.
    blood_total = sum(blood_raw.values())
    for entry in out_limbs:
        entry["blood_frac"] = round(blood_raw[entry["id"]] / blood_total, 4)
    drift = round(1.0 - sum(e["blood_frac"] for e in out_limbs), 4)
    for entry in out_limbs:
        if entry["id"] == IMPLICIT_TORSO:
            entry["blood_frac"] = round(entry["blood_frac"] + drift, 4)

    # Sort: torso first, then the rest, so the file reads top-down.
    out_limbs.sort(key=lambda l: (l["id"] != IMPLICIT_TORSO, l["id"]))

    total_hp = sum(l["hp_frac"] for l in out_limbs)
    if abs(total_hp - 1.0) > 1e-6:
        raise ValueError(f"{map_id}: hp_frac sums to {total_hp}, expected 1.0")

    figure = spec.get("figure", [])
    if figure:
        width = len(figure[0])
        for i, row in enumerate(figure):
            if len(row) != width:
                raise ValueError(f"{map_id}: figure row {i} is {len(row)} wide, expected {width}")
        known = {c for c in spec.get("graph_syms", {}).values()}
        for row in figure:
            for c in row:
                if c != " " and c not in known:
                    raise ValueError(f"{map_id}: figure uses '{c}', which has no graph_sym")

    out = {
        "type": "monster_limb_map",
        "id": map_id,
        "//": f"generated from {source} by tools/gen_monster_limb_maps.py; "
              f"dropped zones: {', '.join(sorted(dropped)) or 'none'}",
        "fluid": spec["fluid"],
        "bodytypes": spec.get("bodytypes", []),
        "death_conditions": spec["death_conditions"],
        "limbs": out_limbs,
    }
    if figure:
        out["rows"] = figure
    return out


# ---------------------------------------------------------------------------
# Fluids
# ---------------------------------------------------------------------------

FLUIDS = [
    {
        "type": "monster_fluid",
        "id": "mf_blood",
        "name": "blood",
        "hp_per_unit": 25,
        "color": "red",
        "field_on_ground": "fd_blood",
        "clot_factor": 0.985,
        "bleed_factor": 1.0,
        "debuff_curve": [0.7, 0.5, 0.3],
    },
    {
        "type": "monster_fluid",
        "id": "mf_ichor",
        "name": "ichor",
        "hp_per_unit": 40,
        "color": "light_green",
        "field_on_ground": "fd_blood_insect",
        "clot_factor": 0.995,
        "bleed_factor": 0.6,
        "debuff_curve": [0.5, 0.3, 0.15],
    },
    {
        "type": "monster_fluid",
        "id": "mf_acid",
        "name": "acid",
        "hp_per_unit": 20,
        "color": "light_green",
        "field_on_ground": "fd_acid",
        "clot_factor": 0.99,
        "bleed_factor": 1.2,
        "debuff_curve": [0.7, 0.5, 0.3],
    },
    {
        "type": "monster_fluid",
        "id": "mf_sap",
        "name": "sap",
        "hp_per_unit": 60,
        "color": "brown",
        "field_on_ground": "fd_sap",
        "clot_factor": 0.999,
        "bleed_factor": 0.4,
        "debuff_curve": [0.8, 0.6, 0.4],
    },
    {
        "type": "monster_fluid",
        "id": "mf_hydraulic",
        "name": "hydraulic fluid",
        "hp_per_unit": 15,
        "color": "yellow",
        "clot_factor": 0.998,
        "bleed_factor": 0.8,
        "debuff_curve": [0.9, 0.6, 0.2],
    },
    {
        "type": "monster_fluid",
        "id": "mf_spore",
        "name": "spores",
        "hp_per_unit": 10,
        "color": "light_gray",
        # Spores disperse instead of pooling, so bleeding them leaves no field.
        "clot_factor": 0.99,
        "bleed_factor": 0.3,
        "debuff_curve": [0.6, 0.4, 0.2],
    },
]

SINGLE_MAP = {
    "type": "monster_limb_map",
    "id": "mlm_single",
    "//": "Fallback: one limb, behaves exactly like the pre-limb-model monster",
    "fluid": "mf_blood",
    "death_conditions": ["structure_zero"],
    "limbs": [
        {"id": "body", "hit_weight": 100, "hp_frac": 1.0, "blood_frac": 1.0, "vital": True},
    ],
}


# ---------------------------------------------------------------------------
# Entry point
# ---------------------------------------------------------------------------


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--report", action="store_true", help="print zone classification for all sets")
    parser.add_argument("--emit", metavar="OUTDIR", help="write draft JSON into OUTDIR")
    args = parser.parse_args()

    sets = load_sets()
    print(f"loaded {len(sets)} weakpoint sets from {WEAKPOINTS_DIR.relative_to(REPO_ROOT)}")

    if args.report or not args.emit:
        report(sets)

    if args.emit:
        outdir = Path(args.emit)
        outdir.mkdir(parents=True, exist_ok=True)

        maps = [emit_map(map_id, spec, sets) for map_id, spec in CURATED.items()]
        maps.append(SINGLE_MAP)
        (outdir / "monster_limb_maps.json").write_text(
            json.dumps(maps, indent=2, ensure_ascii=False) + "\n", encoding="utf-8"
        )
        (outdir / "monster_fluids.json").write_text(
            json.dumps(FLUIDS, indent=2, ensure_ascii=False) + "\n", encoding="utf-8"
        )
        print(f"wrote {len(maps)} limb maps and {len(FLUIDS)} fluids to {outdir}")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
