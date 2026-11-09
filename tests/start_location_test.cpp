#include <string>
#include <vector>

#include "cata_catch.h"
#include "city.h"
#include "coordinates.h"
#include "map_helpers.h"
#include "point.h"
#include "start_location.h"
#include "type_id.h"

static const start_location_id sloc_shelter_a( "sloc_shelter_a" );

// A city is only usable as a search origin if it has a size, so the tests build one
// explicitly instead of relying on world generation placing it first.
static city make_test_city( const point_om_omt &pos, const point_abs_om &om, const int size )
{
    city cit( pos, size );
    cit.pos_om = om;
    cit.name = "Testburg";
    return cit;
}

TEST_CASE( "city_start_search_falls_back_to_the_overmap", "[start_location]" )
{
    clear_map();

    // Nothing in this test needs a real city: the point is that a start location
    // which cannot be found inside the city must not abort character creation.
    // Evac shelters are generated as overmap specials outside city limits, so the
    // in-city search legitimately finds nothing for them.
    const start_location &shelter = sloc_shelter_a.obj();
    REQUIRE_FALSE( shelter.targets_count() == 0 );

    // A city far from the populated test world, so its 3x3 neighbourhood starts out
    // empty and the fallback path is genuinely exercised.
    const point_om_omt city_pos( 30, 30 );
    const point_abs_om om( 8, 8 );
    const city cit = make_test_city( city_pos, om, 4 );

    const auto result = shelter.find_player_initial_location( cit );
    INFO( "city start search returned " << result.first.to_string() );

    // The old behaviour popped up "Unable to generate a valid starting location" and
    // returned an invalid coordinate, which aborted character creation.
    CHECK_FALSE( result.first.is_invalid() );
}

TEST_CASE( "overmap_start_search_still_works_for_shelters", "[start_location]" )
{
    clear_map();

    const start_location &shelter = sloc_shelter_a.obj();
    const point_abs_om om( 8, 8 );

    const auto result = shelter.find_player_initial_location( om );
    INFO( "overmap start search returned " << result.first.to_string() );

    CHECK_FALSE( result.first.is_invalid() );
}
