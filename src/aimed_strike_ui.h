#pragma once
#ifndef CATA_SRC_AIMED_STRIKE_UI_H
#define CATA_SRC_AIMED_STRIKE_UI_H

#include <optional>
#include <string>

class monster;
struct weakpoint_attack;

/**
 * Lets the player choose which body part of a monster to aim at.
 *
 * Returns std::nullopt when the player cancelled, and a string on confirmation:
 * an empty string means "just the body" (no specific zone), otherwise the
 * weakpoint zone id the attack should be forced onto.
 */
std::optional<std::string> select_aimed_zone( const monster &target,
        const weakpoint_attack &attack );

#endif // CATA_SRC_AIMED_STRIKE_UI_H
