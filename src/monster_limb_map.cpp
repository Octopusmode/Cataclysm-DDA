#include "monster_limb_map.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include "debug.h"
#include "flexbuffer_json.h"
#include "generic_factory.h"
#include "json.h"

static const monster_limb_map_id fallback_limb_map( "mlm_single" );

namespace
{

generic_factory<monster_limb_map> limb_map_factory( "monster_limb_map" );
generic_factory<monster_fluid> fluid_factory( "monster_fluid" );

/** bodytype -> limb map, rebuilt in monster_limb_map::finalize_all(). */
std::map<std::string, monster_limb_map_id> bodytype_index;

constexpr float FRACTION_TOLERANCE = 0.01f;

} // namespace

// ---------------------------------------------------------------------------
// string_id specializations
// ---------------------------------------------------------------------------

template<>
bool monster_limb_map_id::is_valid() const
{
    return limb_map_factory.is_valid( *this );
}

template<>
const monster_limb_map &monster_limb_map_id::obj() const
{
    return limb_map_factory.obj( *this );
}

template<>
bool monster_fluid_id::is_valid() const
{
    return fluid_factory.is_valid( *this );
}

template<>
const monster_fluid &monster_fluid_id::obj() const
{
    return fluid_factory.obj( *this );
}

// ---------------------------------------------------------------------------
// death conditions
// ---------------------------------------------------------------------------

namespace
{

monster_death_condition_data death_condition_from_string( const std::string &value )
{
    monster_death_condition_data ret;
    static const std::string destroyed_suffix = "_destroyed";
    if( value == "structure_zero" ) {
        ret.type = monster_death_condition::structure_zero;
    } else if( value == "fluid_zero" ) {
        ret.type = monster_death_condition::fluid_zero;
    } else if( value == "incapacitated" ) {
        ret.type = monster_death_condition::incapacitated;
    } else if( value.size() > destroyed_suffix.size() &&
               value.compare( value.size() - destroyed_suffix.size(), destroyed_suffix.size(),
                              destroyed_suffix ) == 0 ) {
        ret.type = monster_death_condition::limb_destroyed;
        ret.limb = value.substr( 0, value.size() - destroyed_suffix.size() );
    } else {
        debugmsg( "unknown monster death condition '%s'", value );
    }
    return ret;
}

} // namespace

void monster_death_condition_data::load( const JsonObject &jo )
{
    std::string kind;
    mandatory( jo, false, "type", kind );
    *this = death_condition_from_string( kind );
    optional( jo, false, "limb", limb );
}

// ---------------------------------------------------------------------------
// limbs
// ---------------------------------------------------------------------------

void monster_limb_entry::load( const JsonObject &jo )
{
    mandatory( jo, false, "id", id );
    optional( jo, false, "hp_frac", hp_frac, 0.0f );
    optional( jo, false, "blood_frac", blood_frac, 0.0f );
    optional( jo, false, "hit_weight", hit_weight, 0.0f );
    optional( jo, false, "weakpoint", weakpoint );
    optional( jo, false, "parent", parent );
    optional( jo, false, "vital", vital, false );
    optional( jo, false, "is_head", is_head, false );
    optional( jo, false, "severable", severable, false );
    optional( jo, false, "severable_bleed_mult", severable_bleed_mult, 1 );
    optional( jo, false, "bleed_mult", bleed_mult, 1.0f );
    optional( jo, false, "graph_sym", graph_sym );

    if( jo.has_object( "scores" ) ) {
        for( const JsonMember member : jo.get_object( "scores" ) ) {
            scores[limb_score_id( member.name() )] = member.get_float();
        }
    }
}

// ---------------------------------------------------------------------------
// fluid
// ---------------------------------------------------------------------------

void monster_fluid::load( const JsonObject &jo, std::string_view )
{
    mandatory( jo, was_loaded, "id", id );
    mandatory( jo, was_loaded, "name", name );
    optional( jo, was_loaded, "hp_per_unit", hp_per_unit, 0 );
    optional( jo, was_loaded, "clot_factor", clot_factor, 0.99f );
    optional( jo, was_loaded, "bleed_factor", bleed_factor, 1.0f );
    optional( jo, was_loaded, "debuff_curve", debuff_curve );
    optional( jo, was_loaded, "field_on_ground", field_on_ground );
    if( jo.has_string( "color" ) ) {
        color = color_from_string( jo.get_string( "color" ) );
    }
}

void monster_fluid::check() const
{
    if( hp_per_unit <= 0 ) {
        debugmsg( "monster_fluid '%s' must have a positive hp_per_unit", id.str() );
    }
    if( bleed_factor < 0.0f ) {
        debugmsg( "monster_fluid '%s' has a negative bleed_factor", id.str() );
    }
    if( !field_on_ground.str().empty() && !field_on_ground.is_valid() ) {
        debugmsg( "monster_fluid '%s' references unknown field '%s'", id.str(),
                  field_on_ground.str() );
    }
    for( const float threshold : debuff_curve ) {
        if( threshold <= 0.0f || threshold >= 1.0f ) {
            debugmsg( "monster_fluid '%s' has a debuff threshold outside (0, 1): %f", id.str(),
                      threshold );
        }
    }
}

void monster_fluid::load_fluid( const JsonObject &jo, const std::string &src )
{
    fluid_factory.load( jo, src );
}

void monster_fluid::reset()
{
    fluid_factory.reset();
}

void monster_fluid::finalize_all()
{
    fluid_factory.finalize();
}

void monster_fluid::check_consistency()
{
    fluid_factory.check();
}

// ---------------------------------------------------------------------------
// limb map
// ---------------------------------------------------------------------------

void monster_limb_map::load( const JsonObject &jo, std::string_view )
{
    mandatory( jo, was_loaded, "id", id );
    optional( jo, was_loaded, "fluid", fluid );

    if( jo.has_array( "bodytypes" ) ) {
        bodytypes.clear();
        for( const JsonValue value : jo.get_array( "bodytypes" ) ) {
            bodytypes.emplace_back( value.get_string() );
        }
    }

    if( jo.has_array( "death_conditions" ) ) {
        death_conditions.clear();
        for( const JsonValue value : jo.get_array( "death_conditions" ) ) {
            if( value.test_string() ) {
                death_conditions.emplace_back( death_condition_from_string( value.get_string() ) );
            } else if( value.test_object() ) {
                monster_death_condition_data data;
                data.load( value.get_object() );
                death_conditions.emplace_back( data );
            } else {
                value.throw_error( "death condition must be a string or an object" );
            }
        }
    }

    if( jo.has_array( "rows" ) ) {
        rows.clear();
        for( const JsonValue value : jo.get_array( "rows" ) ) {
            rows.emplace_back( value.get_string() );
        }
    }

    if( jo.has_array( "limbs" ) ) {
        limbs.clear();
        for( const JsonObject limb_jo : jo.get_array( "limbs" ) ) {
            monster_limb_entry entry;
            entry.load( limb_jo );
            limbs.emplace_back( std::move( entry ) );
        }
    }
}

void monster_limb_map::finalize()
{
    limb_index.clear();
    weakpoint_index.clear();
    for( std::size_t i = 0; i < limbs.size(); ++i ) {
        if( limb_index.count( limbs[i].id ) > 0 ) {
            debugmsg( "monster_limb_map '%s' defines limb '%s' twice", id.str(), limbs[i].id );
        }
        limb_index[limbs[i].id] = i;
        if( !limbs[i].weakpoint.empty() ) {
            // First limb wins: arm_l/arm_r both map to the "arm" zone, and a hit
            // on that zone picks the first one; the limb score layer then treats
            // the pair as a unit.
            weakpoint_index.emplace( limbs[i].weakpoint, i );
        }
    }

    float hp_total = 0.0f;
    float blood_total = 0.0f;
    for( const monster_limb_entry &entry : limbs ) {
        hp_total += entry.hp_frac;
        blood_total += entry.blood_frac;
        if( !entry.parent.empty() && limb_index.count( entry.parent ) == 0 ) {
            debugmsg( "monster_limb_map '%s': limb '%s' has unknown parent '%s'", id.str(),
                      entry.id, entry.parent );
        }
        if( !entry.weakpoint.empty() && limbs.size() > 1 ) {
            // Weakpoints are optional, but when present they should be real zone ids.
            // Actual validation against the weakpoint set happens in check().
        }
    }

    if( limbs.empty() ) {
        debugmsg( "monster_limb_map '%s' defines no limbs", id.str() );
    } else if( std::fabs( hp_total - 1.0f ) > FRACTION_TOLERANCE ) {
        debugmsg( "monster_limb_map '%s': hp_frac sums to %.3f, expected 1.0", id.str(), hp_total );
    }

    if( std::fabs( blood_total - 1.0f ) > FRACTION_TOLERANCE ) {
        debugmsg( "monster_limb_map '%s': blood_frac sums to %.3f, expected 1.0", id.str(),
                  blood_total );
    }
}

void monster_limb_map::check() const
{
    if( !fluid.is_null() && !fluid.is_valid() ) {
        debugmsg( "monster_limb_map '%s' references unknown fluid '%s'", id.str(), fluid.str() );
    }

    for( const monster_death_condition_data &condition : death_conditions ) {
        if( condition.type == monster_death_condition::limb_destroyed &&
            limb_index.count( condition.limb ) == 0 ) {
            debugmsg( "monster_limb_map '%s': death condition references unknown limb '%s'",
                      id.str(), condition.limb );
        }
        if( condition.type == monster_death_condition::fluid_zero && fluid.is_null() ) {
            debugmsg( "monster_limb_map '%s': fluid_zero death condition without a fluid",
                      id.str() );
        }
    }

    for( const monster_limb_entry &entry : limbs ) {
        for( const std::pair<const limb_score_id, float> &score : entry.scores ) {
            if( !score.first.is_valid() ) {
                debugmsg( "monster_limb_map '%s': limb '%s' uses unknown limb score '%s'",
                          id.str(), entry.id, score.first.str() );
            }
            if( score.second < 0.0f ) {
                debugmsg( "monster_limb_map '%s': limb '%s' has a negative '%s' score",
                          id.str(), entry.id, score.first.str() );
            }
        }
    }

    check_figure();
}

void monster_limb_map::check_figure() const
{
    if( rows.empty() ) {
        return;
    }

    const std::size_t width = rows.front().size();
    for( const std::string &row : rows ) {
        if( row.size() != width ) {
            debugmsg( "monster_limb_map '%s': figure rows have different lengths (%d vs %d)",
                      id.str(), static_cast<int>( row.size() ), static_cast<int>( width ) );
            return;
        }
    }

    // Each cell must name a limb, so that the UI can tell what it is pointing at.
    std::map<char, std::size_t> symbols;
    for( std::size_t i = 0; i < limbs.size(); ++i ) {
        const std::string &sym = limbs[i].graph_sym;
        if( sym.empty() ) {
            debugmsg( "monster_limb_map '%s': limb '%s' has no graph_sym but the map has a figure",
                      id.str(), limbs[i].id );
            continue;
        }
        if( sym.size() != 1 ) {
            debugmsg( "monster_limb_map '%s': limb '%s' has a multi-character graph_sym '%s'",
                      id.str(), limbs[i].id, sym );
            continue;
        }
        if( !symbols.emplace( sym[0], i ).second ) {
            debugmsg( "monster_limb_map '%s': graph_sym '%s' is used by more than one limb",
                      id.str(), sym );
        }
    }

    std::map<char, bool> seen;
    for( const std::string &row : rows ) {
        for( const char c : row ) {
            if( c == ' ' ) {
                continue;
            }
            if( symbols.count( c ) == 0 ) {
                debugmsg( "monster_limb_map '%s': figure uses '%c', which names no limb",
                          id.str(), c );
                return;
            }
            seen[c] = true;
        }
    }
    for( const std::pair<const char, std::size_t> &sym : symbols ) {
        if( seen.count( sym.first ) == 0 ) {
            debugmsg( "monster_limb_map '%s': limb '%s' (%c) is missing from the figure",
                      id.str(), limbs[sym.second].id, sym.first );
        }
    }
}

const monster_limb_entry *monster_limb_map::limb( const std::string &limb_id ) const
{
    const auto iter = limb_index.find( limb_id );
    return iter == limb_index.end() ? nullptr : &limbs[iter->second];
}

int monster_limb_map::limb_for_weakpoint( const std::string &weakpoint ) const
{
    const auto iter = weakpoint_index.find( weakpoint );
    return iter == weakpoint_index.end() ? -1 : static_cast<int>( iter->second );
}

std::vector<std::size_t> monster_limb_map::limbs_for_weakpoint( const std::string &weakpoint ) const
{
    std::vector<std::size_t> result;
    for( std::size_t i = 0; i < limbs.size(); ++i ) {
        if( limbs[i].weakpoint == weakpoint ) {
            result.push_back( i );
        }
    }
    return result;
}

std::size_t monster_limb_map::figure_width() const
{
    return rows.empty() ? 0 : rows.front().size();
}

void monster_limb_map::load_map( const JsonObject &jo, const std::string &src )
{
    limb_map_factory.load( jo, src );
}

void monster_limb_map::reset()
{
    limb_map_factory.reset();
    bodytype_index.clear();
}

void monster_limb_map::finalize_all()
{
    limb_map_factory.finalize();

    // generic_factory::finalize() only loads deferred data; per-object
    // finalization has to be requested explicitly.
    for( const monster_limb_map &entry : limb_map_factory.get_all() ) {
        const_cast<monster_limb_map &>( entry ).finalize();
    }

    bodytype_index.clear();
    for( const monster_limb_map &map : limb_map_factory.get_all() ) {
        for( const std::string &bodytype : map.bodytypes ) {
            const auto iter = bodytype_index.find( bodytype );
            if( iter != bodytype_index.end() && iter->second != map.id ) {
                debugmsg( "bodytype '%s' is claimed by both '%s' and '%s'", bodytype,
                          iter->second.str(), map.id.str() );
                continue;
            }
            bodytype_index[bodytype] = map.id;
        }
    }
}

void monster_limb_map::check_consistency()
{
    limb_map_factory.check();
}

const std::vector<monster_limb_map> &monster_limb_map::get_all()
{
    return limb_map_factory.get_all();
}

const monster_limb_map &monster_limb_map::fallback()
{
    if( fallback_limb_map.is_valid() ) {
        return fallback_limb_map.obj();
    }
    static const monster_limb_map default_map = []() {
        monster_limb_map ret;
        ret.id = fallback_limb_map;
        monster_limb_entry body;
        body.id = "body";
        body.hp_frac = 1.0f;
        body.blood_frac = 1.0f;
        body.hit_weight = 100.0f;
        body.vital = true;
        ret.limbs.emplace_back( std::move( body ) );
        ret.limb_index["body"] = 0;
        return ret;
    }();
    return default_map;
}

monster_limb_map_id monster_limb_map::for_bodytype( const std::string &bodytype )
{
    const auto iter = bodytype_index.find( bodytype );
    return iter == bodytype_index.end() ? monster_limb_map_id::NULL_ID() : iter->second;
}
