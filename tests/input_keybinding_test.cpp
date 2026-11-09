#include <string>
#include <vector>

#include "cata_catch.h"
#include "input.h"
#include "input_enums.h"

// Verifies that the keybindings backing the aimed strike actually resolve.
// A missing entry here means the action is dead: the input context never emits
// it and no amount of handling code makes the hotkey work.
TEST_CASE( "aimed_attack_hotkey_is_bound", "[input][limb]" )
{
    // Control: an action that has existed since forever, so a failure here means
    // the test harness itself is broken rather than our binding.
    const std::vector<input_event> &fire =
        inp_mngr.get_input_for_action( "fire", "DEFAULTMODE" );
    REQUIRE_FALSE( fire.empty() );

    const std::vector<input_event> &aimed =
        inp_mngr.get_input_for_action( "aimed_attack", "DEFAULTMODE" );
    REQUIRE_FALSE( aimed.empty() );

    bool has_ctrl_f_code = false;
    bool has_ctrl_f_char = false;
    for( const input_event &ev : aimed ) {
        if( ev.sequence.size() != 1 ) {
            continue;
        }
        if( ev.type == input_event_t::keyboard_code && ev.sequence.front() == 'f' &&
            ev.modifiers.count( keymod_t::ctrl ) == 1 ) {
            has_ctrl_f_code = true;
        }
        // In character input mode Ctrl+F arrives as the control character 0x06,
        // which the keybinding file names "CTRL+F".
        if( ev.type == input_event_t::keyboard_char && ev.sequence.front() == 6 ) {
            has_ctrl_f_char = true;
        }
    }
    CHECK( has_ctrl_f_code );
    CHECK( has_ctrl_f_char );
}

TEST_CASE( "every_registered_default_action_has_a_binding", "[input]" )
{
    // Mirrors what game::get_default_mode_input_context() promises: an action
    // registered there must be reachable by the player.
    for( const std::string &action : {
             "fire", "aimed_attack", "throw"
         } ) {
        CAPTURE( action );
        CHECK_FALSE( inp_mngr.get_input_for_action( action, "DEFAULTMODE" ).empty() );
    }
}
