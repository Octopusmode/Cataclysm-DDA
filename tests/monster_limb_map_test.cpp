#include <cmath>
#include <map>
#include <numeric>
#include <string>
#include <vector>

#include "cata_catch.h"
#include "avatar.h"
#include "damage.h"
#include "map.h"
#include "map_helpers.h"
#include "monster.h"
#include "monster_limb_map.h"
#include "monstergenerator.h"
#include "mtype.h"
#include "player_helpers.h"
#include "type_id.h"
#include "weakpoint.h"

static const mtype_id mon_zombie( "mon_zombie" );
static const mtype_id mon_zombie_dog( "mon_zombie_dog" );
static const mtype_id mon_turret( "mon_turret" );
static const mtype_id mon_giant_cockroach( "mon_giant_cockroach" );
static const mtype_id mon_spider_cellar_giant( "mon_spider_cellar_giant" );
static const mtype_id mon_crow( "mon_crow" );
static const mtype_id mon_mi_go( "mon_mi_go" );
static const mtype_id mon_zombie_hulk( "mon_zombie_hulk" );

static const field_type_str_id fd_blood( "fd_blood" );

static const monster_limb_map_id mlm_humanoid( "mlm_humanoid" );
static const monster_limb_map_id mlm_quadruped( "mlm_quadruped" );
static const monster_limb_map_id mlm_single( "mlm_single" );
static const monster_limb_map_id mlm_arthropod( "mlm_arthropod" );
static const monster_limb_map_id mlm_bird( "mlm_bird" );
static const monster_limb_map_id mlm_migo( "mlm_migo" );

static const damage_type_id damage_bash( "bash" );

static int limb_hp_sum( const monster &mon )
{
    const std::vector<int> &limbs = mon.get_limb_hp();
    return std::accumulate( limbs.begin(), limbs.end(), 0 );
}

static int limb_bleed_sum( const monster &mon )
{
    const std::vector<int> &bleed = mon.get_limb_bleed();
    return std::accumulate( bleed.begin(), bleed.end(), 0 );
}

// ---------------------------------------------------------------------------
// Data
// ---------------------------------------------------------------------------

TEST_CASE( "monster_limb_maps_are_valid", "[monster][limb]" )
{
    REQUIRE( mlm_humanoid.is_valid() );
    REQUIRE( mlm_quadruped.is_valid() );
    REQUIRE( mlm_single.is_valid() );

    for( const monster_limb_map &map : monster_limb_map::get_all() ) {
        CAPTURE( map.id.str() );
        CHECK_FALSE( map.limbs.empty() );

        float hp_sum = 0.0f;
        float blood_sum = 0.0f;
        for( const monster_limb_entry &limb : map.limbs ) {
            hp_sum += limb.hp_frac;
            blood_sum += limb.blood_frac;
            if( !limb.parent.empty() ) {
                CHECK( map.limb( limb.parent ) != nullptr );
            }
        }
        CHECK( hp_sum == Approx( 1.0f ).margin( 0.01 ) );
        CHECK( blood_sum == Approx( 1.0f ).margin( 0.01 ) );
    }
}

TEST_CASE( "monster_limb_map_fractions_are_sane", "[monster][limb]" )
{
    const monster_limb_map &humanoid = mlm_humanoid.obj();

    const monster_limb_entry *torso = humanoid.limb( "torso" );
    REQUIRE( torso != nullptr );
    CHECK( torso->vital );

    const monster_limb_entry *head = humanoid.limb( "head" );
    REQUIRE( head != nullptr );
    CHECK( head->is_head );
    CHECK( head->weakpoint == "head" );

    // Symmetric limbs must exist as separate entries so they can be crippled.
    REQUIRE( humanoid.limb( "arm_l" ) != nullptr );
    REQUIRE( humanoid.limb( "arm_r" ) != nullptr );
    REQUIRE( humanoid.limb( "leg_l" ) != nullptr );
    REQUIRE( humanoid.limb( "leg_r" ) != nullptr );
    CHECK( humanoid.limb( "arm_l" )->hp_frac == Approx( humanoid.limb( "arm_r" )->hp_frac ) );

    // Legs drive movement, arms drive attacks; these scores are consumed by the
    // limb-score layer added in the next step.
    CHECK( humanoid.limb( "leg_l" )->scores.count( limb_score_id( "move_speed" ) ) == 1 );
    CHECK( humanoid.limb( "arm_l" )->scores.count( limb_score_id( "attack" ) ) == 1 );

    const monster_limb_map &quadruped = mlm_quadruped.obj();
    CHECK( quadruped.limb( "foreleg_l" ) != nullptr );
    CHECK( quadruped.limb( "hindleg_r" ) != nullptr );

    // Weakpoint zones must resolve to limbs so that hits can be localised.
    CHECK( humanoid.limb_for_weakpoint( "head" ) >= 0 );
    CHECK( humanoid.limb_for_weakpoint( "eye" ) >= 0 );
    CHECK( humanoid.limb_for_weakpoint( "no_such_zone" ) < 0 );
}

TEST_CASE( "every_monster_has_a_limb_map", "[monster][limb]" )
{
    for( const mtype &type : MonsterGenerator::generator().get_all_mtypes() ) {
        INFO( "monster type " << type.id.str() );
        REQUIRE( type.limb_map != nullptr );
        CHECK_FALSE( type.limb_map->limbs.empty() );
    }
}

TEST_CASE( "limb_map_resolution_order", "[monster][limb]" )
{
    // Bodytype "human" resolves to the humanoid map.
    CHECK( mon_zombie.obj().limb_map_id == mlm_humanoid );
    CHECK( mon_zombie.obj().limb_map->id == mlm_humanoid );

    // Quadrupeds use the quadruped map.
    CHECK( mon_zombie_dog.obj().limb_map->id == mlm_quadruped );

    // Monsters without an anatomy of their own fall back to a single limb.
    CHECK( mon_turret.obj().limb_map->limbs.size() == 1 );
    CHECK( mon_turret.obj().limb_map->limbs.front().id == "body" );

    // The non-humanoid families get their own maps instead of the fallback.
    CHECK( mon_giant_cockroach.obj().limb_map->id == mlm_arthropod );
    CHECK( mon_giant_cockroach.obj().limb_map->limbs.size() > 1 );
    CHECK( mon_spider_cellar_giant.obj().limb_map->id == mlm_arthropod );
    CHECK( mon_crow.obj().limb_map->id == mlm_bird );
    CHECK( mon_mi_go.obj().limb_map->id == mlm_migo );
    CHECK( mon_mi_go.obj().limb_map->limbs.size() == 8 );

    CHECK_FALSE( monster_limb_map::fallback().limbs.empty() );
    CHECK( monster_limb_map::for_bodytype( "no_such_bodytype" ).is_null() );
}

// ---------------------------------------------------------------------------
// Damage accounting
// ---------------------------------------------------------------------------

TEST_CASE( "limb_damage_keeps_hp_in_sync", "[monster][limb]" )
{
    clear_map();
    clear_creatures();

    monster &zombie = spawn_test_monster( "mon_zombie", tripoint_bub_ms( 30, 30, 0 ) );
    REQUIRE( zombie.has_limb_model() );

    const int max_hp = zombie.get_hp_max();
    REQUIRE( zombie.get_hp() == max_hp );

    damage_instance di;
    di.add_damage( damage_bash, 20 );
    zombie.deal_damage( nullptr, bodypart_id( "torso" ), di );

    CHECK( zombie.get_hp() == max_hp - 20 );
    // The cached hp must always equal the sum of the limbs.
    CHECK( limb_hp_sum( zombie ) == zombie.get_hp() );
    CHECK( zombie.get_limb_hp().size() == zombie.type->limb_map->limbs.size() );
}

TEST_CASE( "damage_overflows_from_a_destroyed_limb", "[monster][limb]" )
{
    clear_map();
    clear_creatures();

    monster &zombie = spawn_test_monster( "mon_zombie", tripoint_bub_ms( 30, 30, 0 ) );
    REQUIRE( zombie.has_limb_model() );
    const int max_hp = zombie.get_hp_max();

    // Way more damage than any single limb can absorb: the surplus must still
    // reduce total hp instead of being discarded.
    zombie.apply_limb_damage( 0, max_hp / 2 );

    CHECK( zombie.get_hp() == max_hp - max_hp / 2 );
    CHECK( limb_hp_sum( zombie ) == zombie.get_hp() );
}

TEST_CASE( "bleeding_converts_fluid_loss_into_damage", "[monster][limb]" )
{
    clear_map();
    clear_creatures();

    monster &zombie = spawn_test_monster( "mon_zombie", tripoint_bub_ms( 32, 30, 0 ) );
    REQUIRE( zombie.has_limb_model() );
    REQUIRE( zombie.get_fluid_max() > 0 );

    // Guard against a silently missing fluid definition: a default-constructed
    // fluid has bleed_factor 1.0 but hp_per_unit 0, which would open wounds
    // that never deal any damage.
    REQUIRE( zombie.type->limb_map->fluid.is_valid() );
    CHECK( zombie.type->limb_map->fluid.obj().hp_per_unit == 25 );

    damage_instance di;
    di.add_damage( damage_bash, 40 );
    zombie.deal_damage( nullptr, bodypart_id( "torso" ), di );

    const int hp_after_hit = zombie.get_hp();
    REQUIRE( hp_after_hit == zombie.get_hp_max() - 40 );
    REQUIRE( limb_bleed_sum( zombie ) > 0 );

    for( int turn = 0; turn < 20; ++turn ) {
        zombie.process_bleeding();
    }

    CHECK( zombie.get_hp() < hp_after_hit );
    CHECK( zombie.get_fluid_cur() < zombie.get_fluid_max() );
    CHECK( limb_hp_sum( zombie ) == zombie.get_hp() );
}

TEST_CASE( "damaged_limbs_reduce_capability", "[monster][limb]" )
{
    clear_map();
    clear_creatures();

    monster &zombie = spawn_test_monster( "mon_zombie", tripoint_bub_ms( 36, 30, 0 ) );
    REQUIRE( zombie.has_limb_model() );

    const int base_speed = zombie.get_speed();
    REQUIRE( limb_score_id( "move_speed" ).is_valid() );
    CHECK( zombie.limb_score( limb_score_id( "move_speed" ) ) == Approx( 1.0f ) );
    CHECK( zombie.limb_score( limb_score_id( "attack" ) ) == Approx( 1.0f ) );

    // Cripple both legs: movement must drop to a crawl.
    const monster_limb_map &map = *zombie.type->limb_map;
    zombie.apply_limb_damage( static_cast<int>( map.limb_index.at( "leg_l" ) ),
                              zombie.get_hp_max() );
    zombie.apply_limb_damage( static_cast<int>( map.limb_index.at( "leg_r" ) ),
                              zombie.get_hp_max() );

    CHECK( zombie.limb_score( limb_score_id( "move_speed" ) ) == Approx( 0.0f ).margin( 0.01f ) );
    CHECK( zombie.get_speed() < base_speed );

    // Arms no longer contribute to attack strength.
    zombie.apply_limb_damage( static_cast<int>( map.limb_index.at( "arm_l" ) ),
                              zombie.get_hp_max() );
    zombie.apply_limb_damage( static_cast<int>( map.limb_index.at( "arm_r" ) ),
                              zombie.get_hp_max() );

    CHECK( zombie.limb_score( limb_score_id( "attack" ) ) == Approx( 0.0f ).margin( 0.01f ) );
}

TEST_CASE( "destroyed_vital_limb_kills", "[monster][limb]" )
{
    clear_map();
    clear_creatures();

    monster &zombie = spawn_test_monster( "mon_zombie", tripoint_bub_ms( 38, 30, 0 ) );
    REQUIRE( zombie.has_limb_model() );
    REQUIRE( zombie.get_hp() > 0 );

    // The humanoid map declares "head_destroyed" as a death condition.
    const monster_limb_map &map = *zombie.type->limb_map;
    const int head = static_cast<int>( map.limb_index.at( "head" ) );
    zombie.apply_limb_damage( head, zombie.get_hp_max() );

    CHECK( zombie.get_limb_hp()[head] == 0 );
    CHECK( zombie.get_hp() == 0 );
    CHECK( zombie.is_dead_state() );
}

TEST_CASE( "crippled_jaw_blocks_biting", "[monster][limb]" )
{
    clear_map();
    clear_creatures();

    monster &zombie = spawn_test_monster( "mon_zombie", tripoint_bub_ms( 40, 30, 0 ) );
    REQUIRE( zombie.has_limb_model() );
    CHECK( zombie.special_attack_allowed( "bite_humanoid" ) );
    CHECK( zombie.special_attack_allowed( "grab" ) );

    const monster_limb_map &map = *zombie.type->limb_map;
    // Biting needs the head; grabbing needs a working arm.
    zombie.apply_limb_damage( static_cast<int>( map.limb_index.at( "head" ) ),
                              zombie.get_hp_max() );

    CHECK_FALSE( zombie.special_attack_allowed( "bite_humanoid" ) );
    // The head is a death condition, so put it back to keep the test focused.
    zombie.set_hp( zombie.get_hp_max() );

    zombie.apply_limb_damage( static_cast<int>( map.limb_index.at( "arm_l" ) ),
                              zombie.get_hp_max() );
    zombie.apply_limb_damage( static_cast<int>( map.limb_index.at( "arm_r" ) ),
                              zombie.get_hp_max() );

    CHECK_FALSE( zombie.special_attack_allowed( "grab" ) );
}

TEST_CASE( "bloodless_monsters_do_not_bleed", "[monster][limb]" )
{
    clear_map();
    clear_creatures();

    // Turrets have no anatomy of their own, so nothing changes for them.
    monster &turret = spawn_test_monster( "mon_turret", tripoint_bub_ms( 34, 30, 0 ) );
    REQUIRE_FALSE( turret.has_limb_model() );

    const int max_hp = turret.get_hp_max();
    // apply_damage() bypasses armour, so this checks the accounting path only.
    turret.apply_damage( nullptr, bodypart_id( "torso" ), 10 );

    CHECK( turret.get_hp() == max_hp - 10 );
    CHECK( turret.get_limb_hp().empty() );
    CHECK( turret.get_fluid_max() == 0 );
}

TEST_CASE( "aimed_zone_hit_chances_are_reported", "[monster][weakpoint][limb]" )
{
    clear_map();
    clear_creatures();

    monster &zombie = spawn_test_monster( "mon_zombie", tripoint_bub_ms( 30, 30, 0 ) );
    REQUIRE( zombie.has_limb_model() );

    weakpoint_attack attack;
    attack.source = &get_avatar();
    attack.target = &zombie;
    attack.type = weakpoint_attack::attack_type::MELEE_BASH;
    attack.accuracy = 1.0;
    attack.compute_wp_skill();

    const weakpoints &wps = zombie.type->weakpoints;

    CHECK( wps.hit_chance_of( "", attack ) == 0.0f );
    CHECK( wps.hit_chance_of( "not_a_zone", attack ) == 0.0f );

    // TEST_DATA replaces wps_humanoid_body, so in tests the zombie answers to
    // test_wp1 (coverage 10) and test_wp2 (coverage 20) instead of the shipped
    // anatomy zones. Those two are enough to exercise the probability plumbing.
    const float small = wps.hit_chance_of( "test_wp1", attack );
    const float large = wps.hit_chance_of( "test_wp2", attack );
    CHECK( small > 0.0f );
    CHECK( large > 0.0f );

    // Zones never add up to more than 100%: the remainder is the default body hit.
    CHECK( small + large < 100.0f );
}

TEST_CASE( "limb_map_figures_match_their_anatomy", "[monster][limb]" )
{
    for( const monster_limb_map &map : monster_limb_map::get_all() ) {
        CAPTURE( map.id.str() );
        if( map.rows.empty() ) {
            // mlm_single is the "no anatomy" fallback and is never drawn.
            continue;
        }
        const std::size_t width = map.figure_width();
        REQUIRE( width > 0 );

        std::map<char, std::string> symbols;
        for( const monster_limb_entry &limb : map.limbs ) {
            REQUIRE( limb.graph_sym.size() == 1 );
            symbols[limb.graph_sym[0]] = limb.id;
        }

        std::map<char, bool> seen;
        for( const std::string &row : map.rows ) {
            CHECK( row.size() == width );
            for( const char c : row ) {
                if( c == ' ' ) {
                    continue;
                }
                CHECK( symbols.count( c ) == 1 );
                seen[c] = true;
            }
        }
        for( const std::pair<const char, std::string> &sym : symbols ) {
            CHECK( seen.count( sym.first ) == 1 );
        }
    }
}

TEST_CASE( "every_bodytype_resolves_to_one_limb_map", "[monster][limb]" )
{
    // A bodytype claimed by two maps is ambiguous and would silently pick one.
    std::map<std::string, monster_limb_map_id> claimed;
    for( const monster_limb_map &map : monster_limb_map::get_all() ) {
        for( const std::string &bodytype : map.bodytypes ) {
            CAPTURE( bodytype );
            CHECK( claimed.count( bodytype ) == 0 );
            claimed[bodytype] = map.id;
        }
    }

    // The maps that cover the common non-humanoid bodytypes must actually be
    // reachable through bodytype resolution.
    for( const std::string &bodytype : {
             "human", "dog", "insect", "flying insect", "spider", "crab",
             "snake", "blob", "frog", "bird"
         } ) {
        CAPTURE( bodytype );
        CHECK_FALSE( monster_limb_map::for_bodytype( bodytype ).is_null() );
    }
}

TEST_CASE( "limbs_for_weakpoint_groups_shared_zones", "[monster][limb]" )
{
    const monster_limb_map &map = mlm_humanoid.obj();

    const std::vector<std::size_t> arms = map.limbs_for_weakpoint( "arm" );
    REQUIRE( arms.size() == 2 );
    CHECK( map.limbs[arms[0]].id == "arm_l" );
    CHECK( map.limbs[arms[1]].id == "arm_r" );

    // The first limb wins for damage localisation, which is what keeps the pair
    // usable with a single weakpoint zone.
    CHECK( map.limb_for_weakpoint( "arm" ) == static_cast<int>( arms[0] ) );

    // The empty zone is the default body hit, which lands on the first limb.
    const std::vector<std::size_t> body = map.limbs_for_weakpoint( "" );
    REQUIRE( body.size() == 1 );
    CHECK( map.limbs[body[0]].id == "torso" );
    CHECK( map.limbs_for_weakpoint( "not_a_zone" ).empty() );
}

TEST_CASE( "bleeding_monsters_leave_a_trail", "[monster][limb]" )
{
    clear_map();
    clear_creatures();

    // The hulk is used because it survives a wound big enough to keep bleeding.
    monster &hulk = spawn_test_monster( "mon_zombie_hulk", tripoint_bub_ms( 38, 30, 0 ) );
    REQUIRE( hulk.has_limb_model() );
    REQUIRE( hulk.get_fluid_max() > 0 );

    hulk.apply_damage( nullptr, bodypart_id( "torso" ), 100 );
    REQUIRE_FALSE( hulk.is_dead_state() );

    const std::vector<int> &wounds = hulk.get_limb_bleed();
    REQUIRE( wounds.size() == hulk.get_limb_hp().size() );
    CHECK( std::accumulate( wounds.begin(), wounds.end(), 0 ) > 0 );

    bool blood_on_floor = false;
    for( int turn = 0; turn < 30 && !blood_on_floor; ++turn ) {
        hulk.process_bleeding();
        blood_on_floor = get_map().has_field_at( hulk.pos_bub(), fd_blood );
    }
    CHECK( blood_on_floor );
}

TEST_CASE( "aimed_zone_is_consumed_by_a_melee_attack", "[monster][weakpoint][limb]" )
{
    clear_map();
    clear_creatures();
    clear_avatar();

    map &here = get_map();
    avatar &you = get_avatar();
    you.setpos( here, tripoint_bub_ms( 36, 30, 0 ) );
    you.set_moves( 1000 );
    monster &zombie = spawn_test_monster( "mon_zombie", tripoint_bub_ms( 37, 30, 0 ) );
    REQUIRE( zombie.has_limb_model() );

    you.aimed_zone = "test_wp2";
    REQUIRE_FALSE( you.aimed_zone.empty() );

    you.melee_attack( zombie, false );

    // The choice belongs to the attack that just happened; it must not leak into
    // the next one.
    CHECK( you.aimed_zone.empty() );
}

TEST_CASE( "forced_zone_overrides_weakpoint_roll", "[monster][weakpoint][limb]" )
{
    clear_map();
    clear_creatures();

    monster &zombie = spawn_test_monster( "mon_zombie", tripoint_bub_ms( 32, 30, 0 ) );
    REQUIRE( zombie.has_limb_model() );

    weakpoint_attack attack;
    attack.source = &get_avatar();
    attack.target = &zombie;
    attack.type = weakpoint_attack::attack_type::MELEE_BASH;
    attack.accuracy = 1.0;
    attack.compute_wp_skill();
    attack.forced_zone = "test_wp1";

    // A deliberately aimed attack must land on the chosen zone every time instead of
    // being rolled.
    for( int i = 0; i < 20; ++i ) {
        const weakpoint *const hit = zombie.type->weakpoints.select_weakpoint( attack );
        REQUIRE( hit != nullptr );
        CHECK( hit->id == "test_wp1" );
    }

    // An unknown zone is ignored and the normal roll happens.
    attack.forced_zone = "not_a_zone";
    CHECK( zombie.type->weakpoints.select_weakpoint( attack ) != nullptr );
}
