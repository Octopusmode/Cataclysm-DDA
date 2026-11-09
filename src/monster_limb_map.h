#pragma once
#ifndef CATA_SRC_MONSTER_LIMB_MAP_H
#define CATA_SRC_MONSTER_LIMB_MAP_H

#include <map>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "color.h"
#include "translation.h"
#include "type_id.h"

class JsonObject;

/**
 * Kinds of conditions which can kill a monster that has a limb model.
 *
 * Deliberately data-driven: not every creature dies when its head is destroyed
 * (blobs, fungal structures, turrets) and not every creature bleeds (robots,
 * skeletons), so `vital` limbs and fluid loss are opt-in per limb map.
 */
enum class monster_death_condition : int {
    /** Structure (the sum of all limb hp) reached zero. Always implicitly true. */
    structure_zero,
    /** A specific limb was destroyed; the limb id follows in `limb`. */
    limb_destroyed,
    /** The creature's fluid reservoir ran out (robots, hydraulics). */
    fluid_zero,
    /** Knocked out for too long. */
    incapacitated,
    last
};

struct monster_death_condition_data {
    monster_death_condition type = monster_death_condition::structure_zero;
    /** Limb id for monster_death_condition::limb_destroyed. */
    std::string limb;

    void load( const JsonObject &jo );
};

/** One limb (hit zone) of a monster limb map. */
struct monster_limb_entry {
    /** Limb id, e.g. "head", "arm_l", "abdomen". */
    std::string id;
    /** Fraction of the monster's `type->hp` this limb holds. All fractions sum to 1. */
    float hp_frac = 0.0f;
    /** Fraction of the creature's fluid volume held here. Sums to 1. */
    float blood_frac = 0.0f;
    /**
     * Relative chance to be hit when the attack does not aim at anything.
     * Derived from the coverage of the matching weakpoint zone.
     */
    float hit_weight = 0.0f;
    /** Weakpoint id used when an aimed attack targets this limb. Empty for defaults. */
    std::string weakpoint;
    /** Parent limb id, used by the limb graph UI. Empty for the root. */
    std::string parent;
    /** If destroyed, the limb map's `death_conditions` decide whether the creature dies. */
    bool vital = false;
    /** Used for headshot detection in the limb graph. */
    bool is_head = false;
    /** Can be severed from the body. */
    bool severable = false;
    /** Bleeding multiplier applied when this limb is severed. */
    int severable_bleed_mult = 1;
    /** Bleeding multiplier while the limb is merely wounded. */
    float bleed_mult = 1.0f;
    /** Capability contributions, using the same ids as data/json/limb_scores.json. */
    std::map<limb_score_id, float> scores;
    /**
     * Character used to draw this limb in the map's `rows` figure, e.g. "H".
     * Empty when the limb has no figure.
     */
    std::string graph_sym;

    void load( const JsonObject &jo );
};

/** The fluid a monster circulates instead of (or in addition to) blood. */
struct monster_fluid {
    monster_fluid_id id;
    std::vector<std::pair<monster_fluid_id, mod_id>> src;
    translation name;
    /** How many hit points one unit of fluid loss converts into. */
    int hp_per_unit = 0;
    nc_color color = c_red;
    /** Field dropped when the creature bleeds on the ground. Null = none. */
    field_type_str_id field_on_ground;
    /** Per-turn multiplier applied to an open wound. 1.0 = never clots. */
    float clot_factor = 0.99f;
    /** How much this fluid bleeds relative to blood. 0 = does not bleed at all. */
    float bleed_factor = 1.0f;
    /** Fluid index thresholds below which the creature slows down. */
    std::vector<float> debuff_curve;
    bool was_loaded = false;

    void load( const JsonObject &jo, std::string_view src );
    void check() const;

    static void load_fluid( const JsonObject &jo, const std::string &src );
    static void reset();
    static void finalize_all();
    static void check_consistency();
};

/**
 * Per-creature-kind map of limbs.
 *
 * Monsters previously had a single hp pool and no anatomy at all. A limb map
 * gives them named hit zones which reuse the existing weakpoint vocabulary, so
 * that damage can be localised without rewriting every monster definition.
 */
struct monster_limb_map {
    monster_limb_map_id id;
    std::vector<std::pair<monster_limb_map_id, mod_id>> src;
    /** Fluid circulating in this creature. Null = no fluid channel. */
    monster_fluid_id fluid;
    /** Bodytypes this map is applied to when a monster does not set `limb_map`. */
    std::vector<std::string> bodytypes;
    std::vector<monster_death_condition_data> death_conditions;
    std::vector<monster_limb_entry> limbs;
    /**
     * ASCII figure of the creature, one character per cell, using the limbs'
     * `graph_sym` characters. Every row must have the same length. Empty when the
     * creature has no meaningful figure (a single-limb map, for instance).
     */
    std::vector<std::string> rows;
    /** Fast lookup by limb id, filled in finalize(). */
    std::map<std::string, std::size_t> limb_index;
    /** Weakpoint zone id -> limb index, filled in finalize(). */
    std::map<std::string, std::size_t> weakpoint_index;
    bool was_loaded = false;

    void load( const JsonObject &jo, std::string_view src );
    void finalize();
    void check() const;
    void check_figure() const;

    const monster_limb_entry *limb( const std::string &limb_id ) const;
    /** Limb index fed by a weakpoint zone id, or -1 when unmapped. */
    int limb_for_weakpoint( const std::string &weakpoint ) const;
    /**
     * Limb indices that answer to the given weakpoint zone. Several limbs can share
     * one zone (arm_l and arm_r both answer to "arm").
     */
    std::vector<std::size_t> limbs_for_weakpoint( const std::string &weakpoint ) const;
    /** Width of the `rows` figure in cells, or 0 when there is no figure. */
    std::size_t figure_width() const;

    static void load_map( const JsonObject &jo, const std::string &src );
    static void reset();
    static void finalize_all();
    static void check_consistency();
    static const std::vector<monster_limb_map> &get_all();

    /** The map used by monsters that have no anatomy of their own. */
    static const monster_limb_map &fallback();
    /** Map lookup by bodytype, or a null id when no map claims that bodytype. */
    static monster_limb_map_id for_bodytype( const std::string &bodytype );
};

#endif // CATA_SRC_MONSTER_LIMB_MAP_H
