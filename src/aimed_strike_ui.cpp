#include "aimed_strike_ui.h"

#include <algorithm>
#include <cstddef>
#include <string>
#include <vector>

#include "cata_imgui.h"
#include "imgui/imgui.h"
#include "monster.h"
#include "monster_limb_map.h"
#include "mtype.h"
#include "string_formatter.h"
#include "translations.h"
#include "uilist.h"
#include "weakpoint.h"

namespace
{

int limb_max_hp( const monster &target, const std::size_t index )
{
    return static_cast<int>( target.get_hp_max() * target.type->limb_map->limbs[index].hp_frac );
}

/** Percent chance that an unaimed attack lands on the "body" default zone. */
int default_zone_chance( const mtype &type, const weakpoint_attack &attack )
{
    int mapped = 0;
    for( const monster_limb_entry &limb : type.limb_map->limbs ) {
        if( limb.weakpoint.empty() ) {
            continue;
        }
        mapped += static_cast<int>( type.weakpoints.hit_chance_of( limb.weakpoint, attack ) );
    }
    return std::clamp( 100 - mapped, 0, 100 );
}

/** Human-readable, translated label for a weakpoint zone. */
std::string zone_label( const mtype &type, const std::string &zone )
{
    if( zone.empty() ) {
        return _( "body (no particular part)" );
    }
    for( const weakpoint &wp : type.weakpoints.weakpoint_list ) {
        if( wp.id == zone && !wp.name.empty() ) {
            return wp.name.translated();
        }
    }
    return zone;
}

std::string describe_zone( const monster &target, const std::vector<std::size_t> &limbs,
                           const int chance )
{
    int cur_hp = 0;
    int max_hp = 0;
    for( const std::size_t index : limbs ) {
        const int limb_max = limb_max_hp( target, index );
        max_hp += limb_max;
        cur_hp += index < target.get_limb_hp().size() ? target.get_limb_hp()[index] : limb_max;
    }

    std::string desc = string_format( _( "Chance to hit: %1$d%%\nStructure: %2$d/%3$d" ),
                                      chance, cur_hp, max_hp );
    if( max_hp > 0 && cur_hp <= 0 ) {
        desc += _( "\nThis part has been destroyed." );
    }
    return desc;
}

/**
 * Draws the creature's figure next to the list, highlighting whichever limb the
 * cursor is on. The figure comes from the limb map itself, so it stays correct
 * for creatures that do not look remotely human.
 */
class aimed_strike_callback : public uilist_callback
{
    private:
        const monster &target;
        const monster_limb_map &map;
        // Parallel to the menu entries. An empty zone means the default body hit.
        const std::vector<std::string> &zones;
        const std::vector<std::string> &descriptions;

    public:
        aimed_strike_callback( const monster &target, const std::vector<std::string> &zones,
                               const std::vector<std::string> &descriptions ) :
            target( target ), map( *target.type->limb_map ), zones( zones ),
            descriptions( descriptions ) {}

        float desired_extra_space_right( ) override {
            const std::size_t width = map.figure_width() == 0 ? 20 : map.figure_width() + 2;
            return static_cast<float>( width ) * ImGui::CalcTextSize( "X" ).x;
        }

        void refresh( uilist *menu ) override {
            ImGui::TableSetColumnIndex( 2 );
            const int entnum = menu->previewing;
            if( entnum < 0 || static_cast<std::size_t>( entnum ) >= zones.size() ) {
                return;
            }

            // Symbols of the previewed zone, split by whether the limb is still there.
            std::string intact;
            std::string destroyed;
            const std::vector<int> &limb_hp = target.get_limb_hp();
            for( const std::size_t index : map.limbs_for_weakpoint( zones[entnum] ) ) {
                const std::string &sym = map.limbs[index].graph_sym;
                if( sym.empty() ) {
                    continue;
                }
                const bool gone = index < limb_hp.size() && limb_hp[index] <= 0;
                ( gone ? destroyed : intact ) += sym;
            }

            for( const std::string &row : map.rows ) {
                for( std::size_t i = 0; i < row.size(); ) {
                    const char c = row[i];
                    std::size_t j = i;
                    while( j < row.size() && row[j] == c ) {
                        ++j;
                    }
                    const std::string run( row, i, j - i );
                    nc_color color = c_light_gray;
                    if( destroyed.find( c ) != std::string::npos ) {
                        color = c_dark_gray;
                    } else if( intact.find( c ) != std::string::npos ) {
                        color = c_yellow;
                    }
                    if( i != 0 ) {
                        ImGui::SameLine( 0, 0 );
                    }
                    ImGui::TextColored( color, "%s", run.c_str() );
                    i = j;
                }
            }

            ImGui::Text( "%s", descriptions[entnum].c_str() );
        }
};

} // namespace

std::optional<std::string> select_aimed_zone( const monster &target, const weakpoint_attack &attack )
{
    if( target.type->limb_map == nullptr || target.type->limb_map->limbs.size() < 2 ) {
        return std::nullopt;
    }

    const monster_limb_map &map = *target.type->limb_map;
    const int body_chance = default_zone_chance( *target.type, attack );

    // Present the most exposed parts first; the default body zone has no hit weight
    // of its own and is therefore always offered last.
    std::vector<std::string> zones;
    for( const monster_limb_entry &limb : map.limbs ) {
        if( limb.weakpoint.empty() ) {
            continue;
        }
        if( std::find( zones.begin(), zones.end(), limb.weakpoint ) != zones.end() ) {
            // arm_l and arm_r answer to the same "arm" zone, and a called shot can only
            // name the zone, so both limbs are presented as a single entry.
            continue;
        }
        zones.push_back( limb.weakpoint );
    }
    std::stable_sort( zones.begin(), zones.end(), [&map]( const std::string &l,
    const std::string &r ) {
        return map.limbs[map.limb_for_weakpoint( l )].hit_weight >
               map.limbs[map.limb_for_weakpoint( r )].hit_weight;
    } );
    zones.emplace_back();

    std::vector<std::string> descriptions;
    descriptions.reserve( zones.size() );

    uilist menu;
    menu.title = string_format( _( "Aim at which part of the %s?" ),
                                target.disp_name( false, true ) );
    menu.text = _( "The figure shows what you are pointing at." );

    for( std::size_t i = 0; i < zones.size(); ++i ) {
        const std::string &zone = zones[i];
        const std::vector<std::size_t> limbs = map.limbs_for_weakpoint( zone );
        const int chance = zone.empty() ? body_chance :
                           static_cast<int>( target.type->weakpoints.hit_chance_of( zone, attack ) );
        const std::string name = zone_label( *target.type, zone );
        menu.addentry_col( static_cast<int>( i ), true, MENU_AUTOASSIGN, name,
                           string_format( "%d%%", chance ), "" );
        descriptions.push_back( describe_zone( target, limbs, chance ) );
    }

    aimed_strike_callback cb( target, zones, descriptions );
    menu.callback = &cb;
    menu.query();

    if( menu.ret < 0 || menu.ret >= static_cast<int>( zones.size() ) ) {
        return std::nullopt;
    }
    return zones[static_cast<std::size_t>( menu.ret )];
}
