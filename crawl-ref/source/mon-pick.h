/*
 *  File:       mon-pick.h
 *  Summary:    Functions used to help determine which monsters should appear.
 *  Written by: Linley Henzell
 *
 *  Modified for Crawl Reference by $Author$ on $Date$
 */


#ifndef MONPICK_H
#define MONPICK_H

#include "travel.h"

// last updated 12may2000 {dlb}
/* ***********************************************************************
 * called from: dungeon - fight
 * *********************************************************************** */
int mons_rarity(int mcls, const level_id &place = level_id::current());


// last updated 12may2000 {dlb}
/* ***********************************************************************
 * called from: dungeon
 * *********************************************************************** */
int mons_level(int mcls, const level_id &place = level_id::current());

int mons_global_level(int mcls);

// last updated 12may2000 {dlb}
/* ***********************************************************************
 * called from: dungeon - mon-pick
 * *********************************************************************** */
bool mons_abyss(int mcls);


// last updated 12may2000 {dlb}
/* ***********************************************************************
 * called from: dungeon - mon-pick
 * *********************************************************************** */
int mons_rare_abyss(int mcls);


// last updated 10jun2000 {dlb}
/* ***********************************************************************
 * called from: levels - mon-pick
 * *********************************************************************** */
bool mons_pan(int mcls);

int mons_cocytus_level(int mcls);
int mons_cocytus_rare(int mcls);
int mons_crypt_level(int mcls);
int mons_crypt_rare(int mcls);
int mons_dis_level(int mcls);
int mons_dis_rare(int mcls);
int mons_gehenna_level(int mcls);
int mons_gehenna_rare(int mcls);
int mons_hallblade_level(int mcls);
int mons_hallblade_rare(int mcls);
int mons_hallelf_level(int mcls);
int mons_hallelf_rare(int mcls);
int mons_hallzot_level(int mcls);
int mons_hallzot_rare(int mcls);
int mons_hive_level(int mcls);
int mons_hive_rare(int mcls);
int mons_lair_level(int mcls);
int mons_lair_rare(int mcls);
int mons_mineorc_level(int mcls);
int mons_mineorc_rare(int mcls);
int mons_pitslime_level(int mcls);
int mons_pitslime_rare(int mcls);
int mons_pitsnake_level(int mcls);
int mons_pitsnake_rare(int mcls);
int mons_standard_level(int mcls);
int mons_standard_rare(int mcls);
int mons_swamp_level(int mcls);
int mons_swamp_rare(int mcls);
int mons_shoals_level(int mcls);
int mons_shoals_rare(int mcls);
int mons_tartarus_level(int mcls);
int mons_tartarus_rare(int mcls);
int mons_tomb_level(int mcls);
int mons_tomb_rare(int mcls);
int mons_caverns_level(int mcls);
int mons_caverns_rare(int mcls);

#endif
