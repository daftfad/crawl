/*
 *  File:       acr.cc
 *  Summary:    Main entry point, event loop, and some initialization functions
 *  Written by: Linley Henzell
 *
 *  Modified for Crawl Reference by $Author$ on $Date$
 */

#include "AppHdr.h"
REVISION("$Rev$");

#include <string>
#include <algorithm>

// I don't seem to need values.h for VACPP..
#if !defined(__IBMCPP__)
#include <limits.h>
#endif

#if DEBUG
  // this contains the DBL_MAX constant
  #include <float.h>
#endif

#include <time.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <stdio.h>
#include <sstream>
#include <iostream>

#ifdef DOS
#include <dos.h>
#include <conio.h>
#include <file.h>
#endif

#ifdef USE_UNIX_SIGNALS
#include <signal.h>
#endif

#include "externs.h"

#include "abl-show.h"
#include "abyss.h"
#include "arena.h"
#include "branch.h"
#include "chardump.h"
#include "cio.h"
#include "cloud.h"
#include "clua.h"
#include "command.h"
#include "crash.h"
#include "database.h"
#include "debug.h"
#include "delay.h"
#include "describe.h"
#include "directn.h"
#include "dungeon.h"
#include "effects.h"
#include "fight.h"
#include "files.h"
#include "food.h"
#include "hiscores.h"
#include "initfile.h"
#include "invent.h"
#include "item_use.h"
#include "it_use2.h"
#include "it_use3.h"
#include "itemname.h"
#include "itemprop.h"
#include "items.h"
#include "lev-pand.h"
#include "luadgn.h"
#include "macro.h"
#include "makeitem.h"
#include "mapmark.h"
#include "maps.h"
#include "message.h"
#include "misc.h"
#include "monplace.h"
#include "monstuff.h"
#include "mon-util.h"
#include "mutation.h"
#include "newgame.h"
#include "notes.h"
#include "ouch.h"
#include "output.h"
#include "overmap.h"
#include "player.h"
#include "quiver.h"
#include "randart.h"
#include "religion.h"
#include "shopping.h"
#include "skills.h"
#include "skills2.h"
#include "spells1.h"
#include "spells2.h"
#include "spells3.h"
#include "spells4.h"
#include "spl-book.h"
#include "spl-cast.h"
#include "spl-util.h"
#include "state.h"
#include "stuff.h"
#include "tags.h"
#include "terrain.h"
#include "transfor.h"
#include "traps.h"
#include "travel.h"
#include "tutorial.h"
#include "view.h"
#include "stash.h"
#include "xom.h"

#ifdef USE_TILE
#include "tiles.h"
#include "tiledef-dngn.h"
#endif

// ----------------------------------------------------------------------
// Globals whose construction/destruction order needs to be managed
// ----------------------------------------------------------------------

CLua clua(true);
CLua dlua(false);		// Lua interpreter for the dungeon builder.
crawl_environment env;	// Requires dlua
player you;
system_environment SysEnv;
game_state crawl_state;



std::string init_file_error;    // externed in newgame.cc

char info[ INFO_SIZE ];         // messaging queue extern'd everywhere {dlb}

int stealth;                    // externed in view.cc

void world_reacts();

static key_recorder repeat_again_rec;

// Clockwise, around the compass from north (same order as enum RUN_DIR)
const struct coord_def Compass[8] =
{
    coord_def(0, -1), coord_def(1, -1), coord_def(1, 0), coord_def(1, 1),
    coord_def(0, 1), coord_def(-1, 1), coord_def(-1, 0), coord_def(-1, -1),
};

// Functions in main module
static void _do_berserk_no_combat_penalty(void);
static bool _initialise(void);
static void _input(void);
static void _move_player(int move_x, int move_y);
static void _move_player(coord_def move);
static int  _check_adjacent(dungeon_feature_type feat, coord_def& delta);
static void _open_door(coord_def move, bool check_confused = true);
static void _open_door(int x, int y, bool check_confused = true)
{
    _open_door(coord_def(x,y), check_confused);
}
static void _close_door(coord_def move);
static void _start_running( int dir, int mode );

static void _prep_input();
static command_type _get_next_cmd();
static keycode_type _get_next_keycode();
static command_type _keycode_to_command( keycode_type key );
static void _setup_cmd_repeat();
static void _do_prev_cmd_again();
static void _update_replay_state();

static void _show_commandline_options_help();
static void _wanderer_startup_message();
static void _god_greeting_message( bool game_start );
static void _take_starting_note();
static void _startup_tutorial();

#ifdef DGL_SIMPLE_MESSAGING
static void _read_messages();
#endif

static void _compile_time_asserts();

//
//  It all starts here. Some initialisations are run first, then straight
//  to new_game and then input.
//

#ifdef USE_TILE
#include <SDL_main.h>
#endif

int main( int argc, char *argv[] )
{
    _compile_time_asserts();  // Just to quiet "unused static function" warning.

    init_crash_handler();

    // Hardcoded initial keybindings.
    init_keybindings();

    // Load in the system environment variables
    get_system_environment();

    // Parse command line args -- look only for initfile & crawl_dir entries.
    if (!parse_args(argc, argv, true))
    {
        _show_commandline_options_help();
        return 1;
    }

    // Init monsters up front - needed to handle the mon_glyph option right.
    init_monsters();

    // Init name cache so that we can parse stash_filter by item name.
    init_properties();
    init_item_name_cache();

    // Read the init file.
    init_file_error = read_init_file();

    // Now parse the args again, looking for everything else.
    parse_args( argc, argv, false );

    if (Options.sc_entries != 0 || !SysEnv.scorefile.empty())
    {
        hiscores_print_all( Options.sc_entries, Options.sc_format );
        return 0;
    }
    else
    {
        // Don't allow scorefile override for actual gameplay, only for
        // score listings.
        SysEnv.scorefile.clear();
    }

#ifdef USE_TILE
    if (!tiles.initialise())
        return -1;
#endif

    const bool game_start = _initialise();

    // Override some options for tutorial.
    init_tutorial_options();

    msg::stream << "Welcome" << (game_start? "" : " back") << ", "
                << you.your_name << " the "
                << species_name( you.species,you.experience_level )
                << " " << you.class_name << "."
                << std::endl;

    // Activate markers only after the welcome message, so the
    // player can see any resulting messages.
    env.markers.activate_all();

    if (game_start && you.char_class == JOB_WANDERER)
        _wanderer_startup_message();

    _god_greeting_message( game_start );

    // Warn player about their weapon, if unsuitable.
    wield_warning(false);

    if (game_start)
    {
        if (Options.tutorial_left)
            _startup_tutorial();
        _take_starting_note();
    }
    else
        learned_something_new(TUT_LOAD_SAVED_GAME);

    // Catch up on any experience levels we did not assign last time. This
    // can happen if Crawl sees SIGHUP while it is waiting for the player
    // to dismiss a level-up prompt.
    _prep_input();
    level_change();

    while (true)
        _input();

    // Should never reach this stage, right?
#if defined(USE_TILE)
    tiles.shutdown();
#elif defined(UNIX)
    unixcurses_shutdown();
#endif

    return 0;
}                               // end main()

static void _show_commandline_options_help()
{
    puts("Command line options:");
    puts("  -name <string>   character name");
    puts("  -race <arg>      preselect race (by letter, abbreviation, or name)");
    puts("  -class <arg>     preselect class (by letter, abbreviation, or name)");
    puts("  -pizza <string>  crawl pizza");
    puts("  -plain           don't use IBM extended characters");
    puts("  -dir <path>      crawl directory");
    puts("  -rc <file>       init file name");
    puts("  -rcdir <dir>     directory that contains (included) rc files");
    puts("  -morgue <dir>    directory to save character dumps");
    puts("  -macro <dir>     directory to save/find macro.txt");
    puts("");
    puts("Command line options override init file options, which override");
    puts("environment options (CRAWL_NAME, CRAWL_PIZZA, CRAWL_DIR, CRAWL_RC).");
    puts("");
    puts("Highscore list options: (Can now be redirected to more, etc)");
    puts("  -scores [N]            highscore list");
    puts("  -tscores [N]           terse highscore list");
    puts("  -vscores [N]           verbose highscore list");
    puts("  -scorefile <filename>  scorefile to report on");
    puts("");
    puts("Arena options: (Stage a tournament between various monsters.)");
    puts("  -arena \"<monster list> v <monster list> arena:<arena map>\"");
}

static void _wanderer_startup_message()
{
    int skill_levels = 0;
    for (int i = 0; i < NUM_SKILLS; i++)
        skill_levels += you.skills[ i ];

    if (skill_levels <= 2)
    {
        // Demigods and Demonspawn wanderers stand to not be
        // able to see any of their skills at the start of
        // the game (one or two skills should be easily guessed
        // from starting equipment)... Anyways, we'll give the
        // player a message to warn them (and give a reason why). -- bwr
        mpr("You wake up in a daze, and can't recall much.");
    }
}

static void _god_greeting_message( bool game_start )
{
    switch (you.religion)
    {
    case GOD_ZIN:
        simple_god_message(" says: Spread the light, my child.");
        break;
    case GOD_SHINING_ONE:
        simple_god_message(" says: Lead the forces of light to victory!");
        break;
    case GOD_KIKUBAAQUDGHA:
        simple_god_message(" says: Welcome...");
        break;
    case GOD_YREDELEMNUL:
        simple_god_message(" says: Carry the black torch! Rouse the idle dead!");
        break;
    case GOD_NEMELEX_XOBEH:
        simple_god_message(" says: It's all in the cards!");
        break;
    case GOD_XOM:
        if (game_start)
            simple_god_message(" says: A new plaything!");
        break;
    case GOD_VEHUMET:
        simple_god_message(" says: Let it end in hellfire!");
        break;
    case GOD_OKAWARU:
        simple_god_message(" says: Welcome, disciple.");
        break;
    case GOD_MAKHLEB:
        god_speaks(you.religion, "Blood and souls for Makhleb!");
        break;
    case GOD_SIF_MUNA:
        simple_god_message(" whispers: I know many secrets...");
        break;
    case GOD_TROG:
        simple_god_message(" says: Kill them all!");
        break;
    case GOD_ELYVILON:
        simple_god_message(" says: Go forth and aid the weak!");
        break;
    case GOD_LUGONU:
        simple_god_message(" says: Spread carnage and corruption!");
        break;
    case GOD_BEOGH:
        simple_god_message(" says: Drown the unbelievers in a sea of blood!");
        break;
    case GOD_NO_GOD:
    case NUM_GODS:
    case GOD_RANDOM:
        break;
    }
}

static void _take_starting_note()
{
    std::ostringstream notestr;
    notestr << you.your_name << ", the "
            << species_name(you.species,you.experience_level) << " "
            << you.class_name
            << ", began the quest for the Orb.";
    take_note(Note(NOTE_MESSAGE, 0, 0, notestr.str().c_str()));

    notestr.str("");
    notestr.clear();

#ifdef WIZARD
    if (you.wizard)
    {
        notestr << "You started the game in wizard mode.";
        take_note(Note(NOTE_MESSAGE, 0, 0, notestr.str().c_str()));

        notestr.str("");
        notestr.clear();
    }
#endif

    notestr << "HP: " << you.hp << "/" << you.hp_max
            << " MP: " << you.magic_points << "/" << you.max_magic_points;

    take_note(Note(NOTE_XP_LEVEL_CHANGE, you.experience_level, 0,
                   notestr.str().c_str()));
}

static void _startup_tutorial()
{
    // Don't allow triggering at game start.
    Options.tut_just_triggered = true;

    // Print stats and everything.
    _prep_input();

    msg::streams(MSGCH_TUTORIAL)
        << "Press any key to start the tutorial intro, or Escape to skip it."
        << std::endl;

    const int ch = c_getch();
    if (ch != ESCAPE)
        tut_starting_screen();
}

#ifdef WIZARD
static void _do_wizard_command(int wiz_command, bool silent_fail)
{
    ASSERT(you.wizard);

    switch (wiz_command)
    {
    case '?':
    {
        const int key = list_wizard_commands(true);
        _do_wizard_command(key, true);
        return;
    }

    case CONTROL('D'): wizard_edit_durations(); break;
    case CONTROL('F'): debug_fight_statistics(false, true); break;
    case CONTROL('G'): save_ghost(true); break;
    case CONTROL('H'): wizard_set_hunger_state(); break;
    case CONTROL('I'): debug_item_statistics(); break;
    case CONTROL('X'): wizard_set_xl(); break;

    case 'O': debug_test_explore();                  break;
    case 'S': wizard_set_skill_level();              break;
    case 'A': wizard_set_all_skills();               break;
    case 'a': acquirement(OBJ_RANDOM, AQ_WIZMODE);   break;
    case 'v': wizard_value_randart();                break;
    case '+': wizard_make_object_randart();          break;
    case '|': wizard_create_all_artefacts();         break;
    case 'C': wizard_uncurse_item();                 break;
    case 'g': wizard_exercise_skill();               break;
    case 'G': wizard_dismiss_all_monsters();         break;
    case 'c': wizard_draw_card();                    break;
    case 'H': wizard_heal(true);                     break;
    case 'h': wizard_heal(false);                    break;
    case 'b': blink(1000, true, true);               break;
    case '~': wizard_interlevel_travel();            break;
    case '"': debug_list_monsters();                 break;
    case 't': wizard_tweak_object();                 break;
    case 'T': debug_make_trap();                     break;
    case '\\': debug_make_shop();                    break;
    case 'f': debug_fight_statistics(false);         break;
    case 'F': debug_fight_statistics(true);          break;
    case 'm': wizard_create_spec_monster();          break;
    case 'M': wizard_create_spec_monster_name();     break;
    case 'R': wizard_spawn_control();                break;
    case 'r': wizard_change_species();               break;
    case '>': wizard_place_stairs(true);             break;
    case '<': wizard_place_stairs(false);            break;
    case 'P': wizard_create_portal();                break;
    case 'L': debug_place_map();                     break;
    case 'i': wizard_identify_pack();                break;
    case 'I': wizard_unidentify_pack();              break;
    case 'z': wizard_cast_spec_spell();              break;
    case 'Z': wizard_cast_spec_spell_name();         break;
    case '(': wizard_create_feature_number();        break;
    case ')': wizard_create_feature_name();          break;
    case '[': demonspawn();                          break;
    case ':': wizard_list_branches();                break;
    case '{': wizard_map_level();                    break;
    case '@': wizard_set_stats();                    break;
    case '^': wizard_gain_piety();                   break;
    case '_': wizard_get_religion();                 break;
    case '\'': wizard_list_items();                  break;
    case 'd': case 'D': wizard_level_travel(true);   break;
    case 'u': case 'U': wizard_level_travel(false);  break;
    case '%': case 'o': wizard_create_spec_object(); break;

    case 'x':
        you.experience = 1 + exp_needed( 2 + you.experience_level );
        level_change();
        break;

    case 's':
        you.exp_available = 20000;
        you.redraw_experience = true;
        break;

    case '$':
        you.gold += 1000;
        if (!Options.show_gold_turns)
            mprf("You now have %d gold.", you.gold);
        break;

    case 'B':
        if (you.level_type != LEVEL_ABYSS)
            banished(DNGN_ENTER_ABYSS, "wizard command");
        else
            down_stairs(you.your_level, DNGN_EXIT_ABYSS);
        break;

    case CONTROL('A'):
        if (you.level_type == LEVEL_ABYSS)
            abyss_teleport(true);
        else
            mpr("You can only abyss_teleport() inside the Abyss.");
        break;

    case ']':
        if (!wizard_add_mutation())
            mpr( "Failure to give mutation." );
        break;

    case '=':
        mprf( "Cost level: %d  Skill points: %d  Next cost level: %d",
              you.skill_cost_level, you.total_skill_points,
              skill_cost_needed( you.skill_cost_level + 1 ) );
        break;

    case 'X':
        if (you.religion == GOD_XOM)
            xom_acts(abs(you.piety - 100));
        else
            xom_acts(coinflip(), random_range(0, MAX_PIETY - 100));
        break;

    case 'p':
        dungeon_terrain_changed(you.pos(), DNGN_ENTER_PANDEMONIUM, false);
        break;

    case 'l':
        dungeon_terrain_changed(you.pos(), DNGN_ENTER_LABYRINTH, false);
        break;

    case 'k':
        if (you.level_type == LEVEL_LABYRINTH)
            change_labyrinth(true);
        else
            mpr("This only makes sense in a labyrinth!");
        break;


    default:
        if (!silent_fail)
        {
            formatted_mpr(formatted_string::parse_string(
                              "Not a <magenta>Wizard</magenta> Command."));
        }
        break;
    }
    // Force the placement of any delayed monster gifts.
    you.turn_is_over = true;
    religion_turn_end();

    you.turn_is_over = false;
}

static void _handle_wizard_command( void )
{
    int wiz_command;

    // WIZ_NEVER gives protection for those who have wiz compiles,
    // and don't want to risk their characters.
    if (Options.wiz_mode == WIZ_NEVER)
        return;

    if (!you.wizard)
    {
        mpr( "WARNING: ABOUT TO ENTER WIZARD MODE!", MSGCH_WARN );

#ifndef SCORE_WIZARD_MODE
        mpr( "If you continue, your game will not be scored!", MSGCH_WARN );
#endif

        if (!yesno( "Do you really want to enter wizard mode?", false, 'n' ))
            return;

        take_note(Note(NOTE_MESSAGE, 0, 0, "Entered wizard mode."));

        you.wizard = true;
        redraw_screen();

        if (crawl_state.cmd_repeat_start)
        {
            crawl_state.cancel_cmd_repeat("Can't repeat entering wizard "
                                          "mode.");
            return;
        }
    }

    mpr( "Enter Wizard Command (? - help): ", MSGCH_PROMPT );
    wiz_command = getch();

    if (crawl_state.cmd_repeat_start)
    {
        // Easiest to list which wizard commands *can* be repeated.
        switch (wiz_command)
        {
        case 'x':
        case '$':
        case 'a':
        case 'c':
        case 'h':
        case 'H':
        case 'm':
        case 'M':
        case 'X':
        case '!':
        case '[':
        case ']':
        case '^':
        case '%':
        case 'o':
        case 'z':
        case 'Z':
            break;

        default:
            crawl_state.cant_cmd_repeat("You cannot repeat that "
                                        "wizard command.");
            return;
        }
    }

    _do_wizard_command(wiz_command, false);
}
#endif

// Set up the running variables for the current run.
static void _start_running( int dir, int mode )
{
    if (Options.tutorial_events[TUT_SHIFT_RUN] && mode == RMODE_START)
        Options.tutorial_events[TUT_SHIFT_RUN] = false;

    if (i_feel_safe(true))
        you.running.initialise(dir, mode);
}

static bool _recharge_rod( item_def &rod, bool wielded )
{
    if (!item_is_rod(rod) || rod.plus >= rod.plus2 || !enough_mp(1, true))
        return (false);

    const int charge = rod.plus / ROD_CHARGE_MULT;

    int rate = ((charge + 1) * ROD_CHARGE_MULT) / 10;

    rate *= (10 + skill_bump( SK_EVOCATIONS ));
    rate = div_rand_round( rate, 100 );

    if (rate < 5)
        rate = 5;
    else if (rate > ROD_CHARGE_MULT / 2)
        rate = ROD_CHARGE_MULT / 2;

    // If not wielded, the rod charges far more slowly.
    if (!wielded)
        rate /= 5;
    // Shields hamper recharging for wielded rods.
    else if (player_shield())
        rate /= 2;

    if (rod.plus / ROD_CHARGE_MULT != (rod.plus + rate) / ROD_CHARGE_MULT)
    {
        dec_mp(1);
        if (wielded)
            you.wield_change = true;
    }

    rod.plus += rate;
    if (rod.plus > rod.plus2)
        rod.plus = rod.plus2;

    if (wielded && rod.plus == rod.plus2)
    {
        mpr("Your rod has recharged.");
        if (is_resting())
            stop_running();
    }

    return (true);
}

static void _recharge_rods()
{
    const int wielded = you.equip[EQ_WEAPON];
    if (wielded != -1 && _recharge_rod( you.inv[wielded], true ))
        return;

    for (int i = 0; i < ENDOFPACK; ++i)
    {
        if (i != wielded && is_valid_item(you.inv[i])
            && one_chance_in(3)
            && _recharge_rod( you.inv[i], false ))
        {
            return;
        }
    }
}

static bool _cmd_is_repeatable(command_type cmd, bool is_again = false)
{
    switch(cmd)
    {
    // Informational commands
    case CMD_LOOK_AROUND:
    case CMD_INSPECT_FLOOR:
    case CMD_EXAMINE_OBJECT:
    case CMD_LIST_WEAPONS:
    case CMD_LIST_ARMOUR:
    case CMD_LIST_JEWELLERY:
    case CMD_LIST_EQUIPMENT:
    case CMD_LIST_GOLD:
    case CMD_CHARACTER_DUMP:
    case CMD_DISPLAY_COMMANDS:
    case CMD_DISPLAY_INVENTORY:
    case CMD_DISPLAY_KNOWN_OBJECTS:
    case CMD_DISPLAY_MUTATIONS:
    case CMD_DISPLAY_SKILLS:
    case CMD_DISPLAY_OVERMAP:
    case CMD_DISPLAY_RELIGION:
    case CMD_DISPLAY_CHARACTER_STATUS:
    case CMD_DISPLAY_SPELLS:
    case CMD_EXPERIENCE_CHECK:
    case CMD_RESISTS_SCREEN:
    case CMD_READ_MESSAGES:
    case CMD_SEARCH_STASHES:
        mpr("You can't repeat informational commands.");
        return (false);

    // Multi-turn commands
    case CMD_PICKUP:
    case CMD_DROP:
    case CMD_BUTCHER:
    case CMD_GO_UPSTAIRS:
    case CMD_GO_DOWNSTAIRS:
    case CMD_WIELD_WEAPON:
    case CMD_WEAPON_SWAP:
    case CMD_WEAR_JEWELLERY:
    case CMD_REMOVE_JEWELLERY:
    case CMD_MEMORISE_SPELL:
    case CMD_EXPLORE:
    case CMD_INTERLEVEL_TRAVEL:
        mpr("You can't repeat multi-turn commands.");
        return (false);

    // Miscellaneous non-repeatable commands.
    case CMD_TOGGLE_AUTOPICKUP:
    case CMD_TOGGLE_FRIENDLY_PICKUP:
    case CMD_ADJUST_INVENTORY:
    case CMD_QUIVER_ITEM:
    case CMD_REPLAY_MESSAGES:
    case CMD_REDRAW_SCREEN:
    case CMD_MACRO_ADD:
    case CMD_SAVE_GAME:
    case CMD_SAVE_GAME_NOW:
    case CMD_SUSPEND_GAME:
    case CMD_QUIT:
    case CMD_DESTROY_ITEM:
    case CMD_FORGET_STASH:
    case CMD_FIX_WAYPOINT:
    case CMD_CLEAR_MAP:
    case CMD_INSCRIBE_ITEM:
    case CMD_MAKE_NOTE:
    case CMD_CYCLE_QUIVER_FORWARD:
        mpr("You can't repeat that command.");
        return (false);

    case CMD_DISPLAY_MAP:
        mpr("You can't repeat map commands.");
        return (false);

    case CMD_MOUSE_MOVE:
    case CMD_MOUSE_CLICK:
        mpr("You can't repeat mouse clicks or movements.");
        return (false);

    case CMD_REPEAT_CMD:
        mpr("You can't repeat the repeat command!");
        return (false);

    case CMD_RUN_LEFT:
    case CMD_RUN_DOWN:
    case CMD_RUN_UP:
    case CMD_RUN_RIGHT:
    case CMD_RUN_UP_LEFT:
    case CMD_RUN_DOWN_LEFT:
    case CMD_RUN_UP_RIGHT:
    case CMD_RUN_DOWN_RIGHT:
        mpr("Why would you want to repeat a run command?");
        return (false);

    case CMD_PREV_CMD_AGAIN:
        ASSERT(!is_again);
        if (crawl_state.prev_cmd == CMD_NO_CMD)
        {
            mpr("No previous command to repeat.");
            return (false);
        }

        return _cmd_is_repeatable(crawl_state.prev_cmd, true);

    case CMD_MOVE_NOWHERE:
    case CMD_REST:
    case CMD_SEARCH:
        return (i_feel_safe(true));

    case CMD_MOVE_LEFT:
    case CMD_MOVE_DOWN:
    case CMD_MOVE_UP:
    case CMD_MOVE_RIGHT:
    case CMD_MOVE_UP_LEFT:
    case CMD_MOVE_DOWN_LEFT:
    case CMD_MOVE_UP_RIGHT:
    case CMD_MOVE_DOWN_RIGHT:
        if (!i_feel_safe())
        {
            return yesno("Really repeat movement command while monsters "
                         "are nearby?", false, 'n');
        }

        return (true);

    case CMD_NO_CMD:
        mpr("Unknown command, not repeating.");
        return (false);

    default:
        return (true);
    }

    return (false);
}

// Used to determine whether to apply the berserk penalty at end of round.
bool apply_berserk_penalty = false;

static void _center_cursor()
{
#ifndef USE_TILE
    const coord_def cwhere = grid2view(you.pos());
    cgotoxy(cwhere.x, cwhere.y);
#endif
}

//
//  This function handles the player's input. It's called from main(),
//  from inside an endless loop.
//
static void _input()
{
    crawl_state.clear_mon_acting();

    religion_turn_start();
    check_beholders();

    // Currently only set if Xom accidentally kills the player.
    you.reset_escaped_death();

    if (crawl_state.is_replaying_keys() && crawl_state.is_repeating_cmd()
        && kbhit())
    {
        // User pressed a key, so stop repeating commands and discard
        // the keypress.
        crawl_state.cancel_cmd_repeat("Key pressed, interrupting command "
                                      "repetition.");
        crawl_state.prev_cmd = CMD_NO_CMD;
        getchm();
        return;
    }

    update_monsters_in_view();

    you.turn_is_over = false;
    _prep_input();

    const bool player_feels_safe = i_feel_safe();

    if (Options.tutorial_left)
    {
        Options.tut_just_triggered = false;

        if (you.attribute[ATTR_HELD])
            learned_something_new(TUT_CAUGHT_IN_NET);
        else if (player_feels_safe && you.level_type != LEVEL_ABYSS)
        {
            // We don't want those "Whew, it's safe to rest now" messages
            // when you were just cast into the Abyss. Right?

            if (2 * you.hp < you.hp_max
                || 2 * you.magic_points < you.max_magic_points)
            {
                tutorial_healing_reminder();
            }
            else if (!you.running
                     && Options.tutorial_events[TUT_SHIFT_RUN]
                     && you.num_turns >= 200
                     && you.hp == you.hp_max
                     && you.magic_points == you.max_magic_points)
            {
                learned_something_new(TUT_SHIFT_RUN);
            }
            else if (!you.running
                     && Options.tutorial_events[TUT_MAP_VIEW]
                     && you.num_turns >= 500
                     && you.hp == you.hp_max
                     && you.magic_points == you.max_magic_points)
            {
                learned_something_new(TUT_MAP_VIEW);
            }
        }
        else
        {
            if (2*you.hp < you.hp_max)
                learned_something_new(TUT_RUN_AWAY);

            if (Options.tutorial_type == TUT_MAGIC_CHAR && you.magic_points < 1)
                learned_something_new(TUT_RETREAT_CASTER);
        }
    }

    if (you.cannot_act())
    {
        if (crawl_state.repeat_cmd != CMD_WIZARD)
            crawl_state.cancel_cmd_repeat("Cannot move, cancelling command "
                                          "repetition.");

        world_reacts();
        return;
    }

    // Stop autoclearing more now that we have control back.
    if (!you_are_delayed())
        reset_more_autoclear();

    if (need_to_autopickup())
        autopickup();

    if (need_to_autoinscribe())
        autoinscribe();

    handle_delay();

    _center_cursor();

    if (you_are_delayed() && current_delay_action() != DELAY_MACRO_PROCESS_KEY)
    {
        if (you.time_taken)
            world_reacts();
        return;
    }

    if (you.turn_is_over)
    {
        world_reacts();
        return;
    }

    crawl_state.check_term_size();
    if (crawl_state.terminal_resized)
        handle_terminal_resize();

    repeat_again_rec.paused = (crawl_state.is_replaying_keys()
                               && !crawl_state.cmd_repeat_start);

    crawl_state.input_line_curr = 0;

    {
        // Enable the cursor to read input. The cursor stays on while
        // the command is being processed, so subsidiary prompts
        // shouldn't need to turn it on explicitly.
#ifdef USE_TILE
        cursor_control con(false);
#else
        cursor_control con(true);
#endif

        clear_macro_process_key_delay();

        crawl_state.waiting_for_command = true;
        c_input_reset(true);

        const command_type cmd = _get_next_cmd();

        crawl_state.waiting_for_command = false;

        if (cmd != CMD_PREV_CMD_AGAIN && cmd != CMD_REPEAT_CMD
            && !crawl_state.is_replaying_keys())
        {
            crawl_state.prev_cmd = cmd;
            crawl_state.input_line_strs.clear();
        }

        if (cmd != CMD_MOUSE_MOVE)
            c_input_reset(false);

        // [dshaligram] If get_next_cmd encountered a Lua macro
        // binding, your turn may be ended by the first invoke of the
        // macro.
        if (!you.turn_is_over && cmd != CMD_NEXT_CMD)
            process_command( cmd );

        repeat_again_rec.paused = true;

        if (cmd != CMD_MOUSE_MOVE)
            c_input_reset(false, true);

        // If the command was CMD_REPEAT_CMD, then the key for the
        // command to repeat has been placed into the macro buffer,
        // so return now to let input() be called again while
        // the keys to repeat are recorded.
        if (cmd == CMD_REPEAT_CMD)
            return;

        // If the command was CMD_PREV_CMD_AGAIN then _input() has been
        // recursively called by _do_prev_cmd_again() via process_command()
        // to re-do the command, so there's nothing more to do.
        if (cmd == CMD_PREV_CMD_AGAIN)
            return;
    }

    if (need_to_autoinscribe())
        autoinscribe();

    if (you.turn_is_over)
    {
        if (apply_berserk_penalty)
            _do_berserk_no_combat_penalty();

        world_reacts();
    }
    else
        viewwindow(true, false);

    _update_replay_state();

    if (you.num_turns != -1)
    {
        PlaceInfo& curr_PlaceInfo = you.get_place_info();
        PlaceInfo  delta;

        delta.turns_total++;
        delta.elapsed_total += you.time_taken;

        switch(you.running)
        {
        case RMODE_INTERLEVEL:
            delta.turns_interlevel++;
            delta.elapsed_interlevel += you.time_taken;
            break;

        case RMODE_EXPLORE_GREEDY:
        case RMODE_EXPLORE:
            delta.turns_explore++;
            delta.elapsed_explore += you.time_taken;
            break;

        case RMODE_TRAVEL:
            delta.turns_travel++;
            delta.elapsed_travel += you.time_taken;
            break;

        default:
            // prev_was_rest is needed so that the turn in which
            // a player is interrupted from resting is counted
            // as a resting turn, rather than "other".
            static bool prev_was_rest = false;

            if (!you.delay_queue.empty() &&
                you.delay_queue.front().type == DELAY_REST)
                prev_was_rest = true;

            if (prev_was_rest)
            {
                delta.turns_resting++;
                delta.elapsed_resting += you.time_taken;

            }
            else
            {
                delta.turns_other++;
                delta.elapsed_other += you.time_taken;
            }

            if (you.delay_queue.empty() ||
                you.delay_queue.front().type != DELAY_REST)
                prev_was_rest = false;

            break;
        }

        you.global_info += delta;
        you.global_info.assert_validity();

        curr_PlaceInfo += delta;
        curr_PlaceInfo.assert_validity();
    }

    crawl_state.clear_god_acting();
}

static bool _toggle_flag( bool* flag, const char* flagname )
{
    *flag = !(*flag);
    mprf( "%s is now %s.", flagname, (*flag) ? "on" : "off" );
    return *flag;
}

static bool _stairs_check_mesmerised()
{
    if (you.duration[DUR_MESMERISED] && !you.confused())
    {
        mprf("You cannot move away from %s!",
             menv[you.mesmerised_by[0]].name(DESC_NOCAP_THE, true).c_str());
        return (true);
    }

    return (false);
}

static bool _marker_vetoes_stair()
{
    return marker_vetoes_operation("veto_stair");
}

static void _go_downstairs();
static void _go_upstairs()
{
    ASSERT(!crawl_state.arena && !crawl_state.arena_suspended);

    const dungeon_feature_type ygrd = grd(you.pos());

    if (_stairs_check_mesmerised())
        return;

    if (you.attribute[ATTR_HELD])
    {
        mpr("You're held in a net!");
        return;
    }

    if (ygrd == DNGN_ENTER_SHOP)
    {
        if (you.duration[DUR_BERSERKER])
            canned_msg(MSG_TOO_BERSERK);
        else
            shop();
        return;
    }
    else if (grid_stair_direction(ygrd) != CMD_GO_UPSTAIRS)
    {
        if (ygrd == DNGN_STONE_ARCH)
            mpr("There is nothing on the other side of the stone arch.");
        else if (ygrd == DNGN_ABANDONED_SHOP)
            mpr("This shop appears to be closed.");
        else
            mpr("You can't go up here!");
        return;
    }

    // Does the next level have a warning annotation?
    if (!check_annotation_exclusion_warning())
        return;

    if (_marker_vetoes_stair())
        return;

    tag_followers(); // Only those beside us right now can follow.
    start_delay(DELAY_ASCENDING_STAIRS,
                1 + (you.burden_state > BS_UNENCUMBERED));
}

static void _go_downstairs()
{
    ASSERT(!crawl_state.arena && !crawl_state.arena_suspended);

    bool shaft = (get_trap_type(you.pos()) == TRAP_SHAFT
                  && grd(you.pos()) != DNGN_UNDISCOVERED_TRAP);

    if (_stairs_check_mesmerised())
        return;

    if (shaft && you.flight_mode() == FL_LEVITATE)
    {
        mpr("You can't fall through a shaft while levitating.");
        return;
    }

    if (grid_stair_direction(grd(you.pos())) != CMD_GO_DOWNSTAIRS
        && !shaft)
    {
        if (grd(you.pos()) == DNGN_STONE_ARCH)
            mpr("There is nothing on the other side of the stone arch.");
        else
            mpr( "You can't go down here!" );
        return;
    }

    if (you.attribute[ATTR_HELD])
    {
        mpr("You're held in a net!");
        return;
    }

    // Does the next level have a warning annotation?
    // Also checks for entering a labyrinth with teleportitis.
    if (!check_annotation_exclusion_warning())
        return;

    if (shaft)
    {
        start_delay( DELAY_DESCENDING_STAIRS, 0, you.your_level );
    }
    else
    {
        if (_marker_vetoes_stair())
            return;

        tag_followers(); // Only those beside us right now can follow.
        start_delay(DELAY_DESCENDING_STAIRS,
                    1 + (you.burden_state > BS_UNENCUMBERED),
                    you.your_level);
    }
}

static void _experience_check()
{
    mprf("You are a level %d %s %s.",
         you.experience_level,
         species_name(you.species,you.experience_level).c_str(),
         you.class_name);

    if (you.experience_level < 27)
    {
        int xp_needed = (exp_needed(you.experience_level+2)-you.experience)+1;
        mprf( "Level %d requires %ld experience (%d point%s to go!)",
              you.experience_level + 1,
              exp_needed(you.experience_level + 2) + 1,
              xp_needed,
              (xp_needed > 1) ? "s" : "");
    }
    else
    {
        mpr( "I'm sorry, level 27 is as high as you can go." );
        mpr( "With the way you've been playing, I'm surprised you got this far." );
    }

    if (you.real_time != -1)
    {
        const time_t curr = you.real_time + (time(NULL) - you.start_time);
        msg::stream << "Play time: " << make_time_string(curr)
                    << " (" << you.num_turns << " turns)"
                    << std::endl;
    }
#ifdef DEBUG_DIAGNOSTICS
    if (wearing_amulet(AMU_THE_GOURMAND))
        mprf(MSGCH_DIAGNOSTICS, "Gourmand charge: %d",
             you.duration[DUR_GOURMAND]);

    mprf(MSGCH_DIAGNOSTICS, "Turns spent on this level: %d",
         env.turns_on_level);
#endif
}

static void _print_friendly_pickup_setting(bool was_changed)
{
    std::string now = (was_changed? "now " : "");

    if (you.friendly_pickup == FRIENDLY_PICKUP_NONE)
    {
        mprf("Your intelligent allies are %sforbidden to pick up anything at all.",
             now.c_str());
    }
    else if (you.friendly_pickup == FRIENDLY_PICKUP_FRIEND)
    {
        mprf("Your intelligent allies may %sonly pick up items dropped by allies.",
             now.c_str());
    }
    else if (you.friendly_pickup == FRIENDLY_PICKUP_ALL)
    {
        mprf("Your intelligent allies may %spick up anything they need.", now.c_str());
    }
    else
    {
        mprf(MSGCH_ERROR, "Your allies%s are collecting bugs!", now.c_str());
    }
}

// Note that in some actions, you don't want to clear afterwards.
// e.g. list_jewellery, etc.
void process_command( command_type cmd )
{
    apply_berserk_penalty = true;

    switch (cmd)
    {
    case CMD_OPEN_DOOR_UP_RIGHT:   _open_door( 1, -1); break;
    case CMD_OPEN_DOOR_UP:         _open_door( 0, -1); break;
    case CMD_OPEN_DOOR_UP_LEFT:    _open_door(-1, -1); break;
    case CMD_OPEN_DOOR_RIGHT:      _open_door( 1,  0); break;
    case CMD_OPEN_DOOR_DOWN_RIGHT: _open_door( 1,  1); break;
    case CMD_OPEN_DOOR_DOWN:       _open_door( 0,  1); break;
    case CMD_OPEN_DOOR_DOWN_LEFT:  _open_door(-1,  1); break;
    case CMD_OPEN_DOOR_LEFT:       _open_door(-1,  0); break;

    case CMD_MOVE_DOWN_LEFT:  _move_player(-1,  1); break;
    case CMD_MOVE_DOWN:       _move_player( 0,  1); break;
    case CMD_MOVE_UP_RIGHT:   _move_player( 1, -1); break;
    case CMD_MOVE_UP:         _move_player( 0, -1); break;
    case CMD_MOVE_UP_LEFT:    _move_player(-1, -1); break;
    case CMD_MOVE_LEFT:       _move_player(-1,  0); break;
    case CMD_MOVE_DOWN_RIGHT: _move_player( 1,  1); break;
    case CMD_MOVE_RIGHT:      _move_player( 1,  0); break;

    case CMD_REST:
        if (i_feel_safe())
        {
            if ((you.hp == you.hp_max || you.species == SP_VAMPIRE
                                         && you.hunger_state == HS_STARVING)
                && you.magic_points == you.max_magic_points )
            {
                mpr("You start searching.");
            }
            else
                mpr("You start resting.");
        }
        _start_running( RDIR_REST, RMODE_REST_DURATION );
        break;

    case CMD_RUN_DOWN_LEFT:
        _start_running( RDIR_DOWN_LEFT, RMODE_START );
        break;
    case CMD_RUN_DOWN:
        _start_running( RDIR_DOWN, RMODE_START );
        break;
    case CMD_RUN_UP_RIGHT:
        _start_running( RDIR_UP_RIGHT, RMODE_START );
        break;
    case CMD_RUN_UP:
        _start_running( RDIR_UP, RMODE_START );
        break;
    case CMD_RUN_UP_LEFT:
        _start_running( RDIR_UP_LEFT, RMODE_START );
        break;
    case CMD_RUN_LEFT:
        _start_running( RDIR_LEFT, RMODE_START );
        break;
    case CMD_RUN_DOWN_RIGHT:
        _start_running( RDIR_DOWN_RIGHT, RMODE_START );
        break;
    case CMD_RUN_RIGHT:
        _start_running( RDIR_RIGHT, RMODE_START );
        break;

    case CMD_DISABLE_MORE:
        Options.show_more_prompt = false;
        break;

    case CMD_ENABLE_MORE:
        Options.show_more_prompt = true;
        break;

    case CMD_REPEAT_KEYS:
        ASSERT(crawl_state.is_repeating_cmd());

        if (crawl_state.prev_repetition_turn == you.num_turns
            && crawl_state.repeat_cmd != CMD_WIZARD
            && (crawl_state.repeat_cmd != CMD_PREV_CMD_AGAIN
                || crawl_state.prev_cmd != CMD_WIZARD))
        {
            // This is a catch-all that shouldn't really happen.
            // If the command always takes zero turns, then it
            // should be prevented in cmd_is_repeatable().  If
            // a command sometimes takes zero turns (because it
            // can't be done, for instance), then
            // crawl_state.zero_turns_taken() should be called when
            // it does take zero turns, to cancel command repetition
            // before we reach here.
#ifdef WIZARD
            crawl_state.cant_cmd_repeat("Can't repeat a command which "
                                        "takes no turns (unless it's a "
                                        "wizard command), cancelling ");
#else
            crawl_state.cant_cmd_repeat("Can't repeat a command which "
                                        "takes no turns, cancelling "
                                        "repetitions.");
#endif
            crawl_state.cancel_cmd_repeat();
            return;
        }

        crawl_state.cmd_repeat_count++;
        if (crawl_state.cmd_repeat_count >= crawl_state.cmd_repeat_goal)
        {
            crawl_state.cancel_cmd_repeat();
            return;
        }

        ASSERT(crawl_state.repeat_cmd_keys.size() > 0);
        repeat_again_rec.paused = true;
        macro_buf_add(crawl_state.repeat_cmd_keys);
        macro_buf_add(KEY_REPEAT_KEYS);

        crawl_state.prev_repetition_turn = you.num_turns;

        break;

    case CMD_TOGGLE_AUTOPICKUP:
        _toggle_flag( &Options.autopickup_on, "Autopickup");
        break;

    case CMD_TOGGLE_FRIENDLY_PICKUP:
    {
        // Toggle pickup mode for friendlies.
        _print_friendly_pickup_setting(false);

        mpr("Change to (d)efault, (n)othing, (f)riend-dropped, or (a)ll? ",
            MSGCH_PROMPT);

        char type = (char) getchm(KC_DEFAULT);
        type = tolower(type);

        if (type == 'd')
            you.friendly_pickup = Options.default_friendly_pickup;
        else if (type == 'n')
            you.friendly_pickup = FRIENDLY_PICKUP_NONE;
        else if (type == 'f')
            you.friendly_pickup = FRIENDLY_PICKUP_FRIEND;
        else if (type == 'a')
            you.friendly_pickup = FRIENDLY_PICKUP_ALL;
        else
        {
            canned_msg( MSG_OK );
            break;
        }
        _print_friendly_pickup_setting(true);
        break;
    }
    case CMD_MAKE_NOTE:
        make_user_note();
        break;

    case CMD_READ_MESSAGES:
#ifdef DGL_SIMPLE_MESSAGING
        if (SysEnv.have_messages)
            _read_messages();
#endif
        break;

    case CMD_CLEAR_MAP:
        if (player_in_mappable_area())
        {
            mpr("Clearing level map.");
            clear_map();
            crawl_view.set_player_at(you.pos());
        }
        break;

    case CMD_DISPLAY_OVERMAP: display_overmap(); break;
    case CMD_GO_UPSTAIRS:     _go_upstairs();    break;
    case CMD_GO_DOWNSTAIRS:   _go_downstairs();  break;
    case CMD_OPEN_DOOR:       _open_door(0, 0);  break;
    case CMD_CLOSE_DOOR:      _close_door(coord_def(0, 0)); break;

    case CMD_DROP:
        drop();
        if (Options.stash_tracking >= STM_DROPPED)
            StashTrack.add_stash();
        break;

    case CMD_SEARCH_STASHES:
        if (Options.tut_stashes)
            Options.tut_stashes = 0;
        StashTrack.search_stashes();
        break;

    case CMD_FORGET_STASH:
        if (Options.stash_tracking >= STM_EXPLICIT)
            StashTrack.no_stash();
        break;

    case CMD_BUTCHER:
        butchery();
        break;

    case CMD_DISPLAY_INVENTORY:
        get_invent(OSEL_ANY);
        break;

    case CMD_EVOKE:
        if (!evoke_wielded())
            flush_input_buffer( FLUSH_ON_FAILURE );
        break;

    case CMD_PICKUP:
        pickup();
        break;

    case CMD_INSPECT_FLOOR:
        request_autopickup();
        break;

    case CMD_FULL_VIEW:
        full_describe_view();
        break;

    case CMD_WIELD_WEAPON:
        wield_weapon(false);
        break;

    case CMD_FIRE:
        fire_thing();
        break;

    case CMD_QUIVER_ITEM:
        choose_item_for_quiver();
        break;

    case CMD_THROW_ITEM_NO_QUIVER:
        throw_item_no_quiver();
        break;

    case CMD_WEAR_ARMOUR:
        wear_armour();
        break;

    case CMD_REMOVE_ARMOUR:
    {
        if (you.attribute[ATTR_TRANSFORMATION] == TRAN_BAT)
        {
            mpr("You can't wear or remove anything in your present form.");
            break;
        }
        int index = 0;

        if (armour_prompt("Take off which item?", &index, OPER_TAKEOFF))
            takeoff_armour(index);
    }
    break;

    case CMD_REMOVE_JEWELLERY:
        remove_ring();
        break;

    case CMD_WEAR_JEWELLERY:
        puton_ring(-1, false);
        break;

    case CMD_ADJUST_INVENTORY:
        adjust();
        break;

    case CMD_MEMORISE_SPELL:
        if (you.attribute[ATTR_TRANSFORMATION] == TRAN_BAT)
        {
           canned_msg(MSG_PRESENT_FORM);
           break;
        }
        if (!learn_spell())
            flush_input_buffer( FLUSH_ON_FAILURE );
        break;

    case CMD_ZAP_WAND:
        if (you.attribute[ATTR_TRANSFORMATION] == TRAN_BAT)
        {
           canned_msg(MSG_PRESENT_FORM);
           break;
        }
        zap_wand();
        break;

    case CMD_EAT:
        eat_food();
        break;

    case CMD_USE_ABILITY:
        if (!activate_ability())
            flush_input_buffer( FLUSH_ON_FAILURE );
        break;

    case CMD_DISPLAY_MUTATIONS:
        display_mutations();
        redraw_screen();
        break;

    case CMD_EXAMINE_OBJECT:
        examine_object();
        break;

    case CMD_PRAY:
        pray();
        break;

    case CMD_DISPLAY_RELIGION:
        describe_god( you.religion, true );
        redraw_screen();
        break;

    case CMD_MOVE_NOWHERE:
    case CMD_SEARCH:
        search_around();
        you.turn_is_over = true;
        break;

    case CMD_QUAFF:
        drink();
        break;

    case CMD_READ:
        if (you.attribute[ATTR_TRANSFORMATION] == TRAN_BAT)
        {
           canned_msg(MSG_PRESENT_FORM);
           break;
        }
        read_scroll();
        break;

    case CMD_LOOK_AROUND:
    {
        mpr("Move the cursor around to observe a square "
            "(v - describe square, ? - help)", MSGCH_PROMPT);

        struct dist lmove;   // Will be initialized by direction().
        direction(lmove, DIR_TARGET, TARG_ANY, -1, true);
        if (lmove.isValid && lmove.isTarget && !lmove.isCancel
            && !crawl_state.arena_suspended)
        {
            start_travel( lmove.target );
        }
        break;
    }

    case CMD_CAST_SPELL:
        if (you.attribute[ATTR_TRANSFORMATION] == TRAN_BAT)
        {
           canned_msg(MSG_PRESENT_FORM);
           break;
        }

        // Randart weapons.
        if (scan_randarts(RAP_PREVENT_SPELLCASTING))
        {
            mpr("Something interferes with your magic!");
            flush_input_buffer( FLUSH_ON_FAILURE );
            break;
        }

        if (Options.tutorial_left)
            Options.tut_spell_counter++;
        if (!cast_a_spell())
            flush_input_buffer( FLUSH_ON_FAILURE );
        break;

    case CMD_DISPLAY_SPELLS:
        inspect_spells();
        break;

    case CMD_WEAPON_SWAP:
        wield_weapon(true);
        break;

        // [ds] Waypoints can be added from the level-map, and we need
        // Ctrl+F for nobler things. Who uses waypoints, anyway?
        // Update: Appears people do use waypoints. Reinstating, on
        // CONTROL('W'). This means Ctrl+W is no longer a wizmode
        // trigger, but there's always '&'. :-)
    case CMD_FIX_WAYPOINT:
        travel_cache.add_waypoint();
        break;

    case CMD_INTERLEVEL_TRAVEL:
        if (Options.tut_travel)
            Options.tut_travel = 0;

        if (!can_travel_interlevel())
        {
            if (you.running.pos == you.pos())
            {
                mpr("You're already here.");
                break;
            }
            else if (!you.running.pos.x || !you.running.pos.y)
            {
                mpr("Sorry, you can't auto-travel out of here.");
                break;
            }

            // Don't ask for a destination if you can only travel
            // within level anyway.
            start_travel(you.running.pos);
        }
        else
            start_translevel_travel();

        if (you.running)
            mesclr();
        break;

    case CMD_ANNOTATE_LEVEL:
        annotate_level();
        break;

    case CMD_EXPLORE:
        // Start exploring
        start_explore(Options.explore_greedy);
        break;

    case CMD_DISPLAY_MAP:
        if (Options.tutorial_events[TUT_MAP_VIEW])
            Options.tutorial_events[TUT_MAP_VIEW] = false;

#if (!DEBUG_DIAGNOSTICS)
        if (!player_in_mappable_area())
        {
            mpr("It would help if you knew where you were, first.");
            break;
        }
#endif
        {
            coord_def pos;
#ifdef USE_TILE
            // Since there's no actual overview map, but the functionality
            // exists, give a message to explain what's going on.
            std::string str = "Move the cursor to view the level map, or "
                              "type <w>?</w> for a list of commands.";
            print_formatted_paragraph(str, get_number_of_cols());
#endif

            show_map(pos, true);
            redraw_screen();

#ifdef USE_TILE
            mpr("Returning to the game...");
#endif
            if (pos.x > 0)
                start_travel(pos);
        }
        break;

    case CMD_DISPLAY_KNOWN_OBJECTS:
        check_item_knowledge();
        break;

    case CMD_REPLAY_MESSAGES:
        replay_messages();
        redraw_screen();
        break;

    case CMD_REDRAW_SCREEN:
        redraw_screen();
        break;

    case CMD_SAVE_GAME_NOW:
        mpr("Saving game... please wait.");
        save_game(true);
        break;

#ifdef USE_UNIX_SIGNALS
    case CMD_SUSPEND_GAME:
        // CTRL-Z suspend behaviour is implemented here,
        // because we want to have CTRL-Y available...
        // and unfortunately they tend to be stuck together.
        clrscr();
#ifndef USE_TILE
        unixcurses_shutdown();
        kill(0, SIGTSTP);
        unixcurses_startup();
#endif
        redraw_screen();
        break;
#endif

    case CMD_DISPLAY_COMMANDS:
        list_commands(0, true);
        break;

    case CMD_EXPERIENCE_CHECK:
        _experience_check();
        break;

    case CMD_SHOUT:
        yell();
        break;

    case CMD_MOUSE_MOVE:
    {
        const coord_def dest = view2grid(crawl_view.mousep);
        if (in_bounds(dest))
            terse_describe_square(dest);
        break;
    }

    case CMD_MOUSE_CLICK:
    {
        // XXX: We should probably use specific commands such as
        // CMD_MOUSE_TRAVEL and get rid of CMD_MOUSE_CLICK and
        // CMD_MOUSE_MOVE.
        c_mouse_event cme = get_mouse_event();
        if (cme && crawl_view.in_view_viewport(cme.pos))
        {
            const coord_def dest = view2grid(cme.pos);
            if (cme.left_clicked())
            {
                if (in_bounds(dest))
                    start_travel(dest);
            }
            else if (cme.right_clicked())
            {
                if (see_grid(dest))
                    full_describe_square(dest);
                else
                    mpr("You can't see that place.");
            }
        }
        break;
    }

    case CMD_DISPLAY_CHARACTER_STATUS:
        display_char_status();
        break;

    case CMD_RESISTS_SCREEN:
        print_overview_screen();
        break;

    case CMD_DISPLAY_SKILLS:
        show_skills();
        redraw_screen();
        break;

    case CMD_CHARACTER_DUMP:
        char name_your[kNameLen+1];

        strncpy(name_your, you.your_name, kNameLen);
        name_your[kNameLen] = 0;
        if (dump_char( name_your, false ))
            mpr("Char dumped successfully.");
        else
            mpr("Char dump unsuccessful! Sorry about that.");
        break;

    case CMD_MACRO_ADD:
        macro_add_query();
        break;

    case CMD_CYCLE_QUIVER_FORWARD:
    {
        int cur;
        you.m_quiver->get_desired_item(NULL, &cur);
        const int next = get_next_fire_item(cur, +1);
#ifdef DEBUG_QUIVER
        mprf(MSGCH_DIAGNOSTICS, "next slot: %d, item: %s", next,
             next == -1 ? "none" : you.inv[next].name(DESC_PLAIN).c_str());
#endif
        if (next != -1)
        {
            // Kind of a hacky way to get quiver to change.
            you.m_quiver->on_item_fired(you.inv[next], true);

            if (next == cur)
                mpr("No other missiles available. Use F to throw any item.");
        }
        else if (cur == -1)
            mpr("No missiles available. Use F to throw any item.");
        break;
    }

    case CMD_LIST_WEAPONS:
        list_weapons();
        break;

    case CMD_LIST_ARMOUR:
        list_armour();
        break;

    case CMD_LIST_JEWELLERY:
        list_jewellery();
        break;

    case CMD_LIST_EQUIPMENT:
        get_invent(OSEL_EQUIP);
        break;

    case CMD_LIST_GOLD:
        mprf("You have %d gold piece%s.",
             you.gold, you.gold > 1 ? "s" : "");
        break;

    case CMD_INSCRIBE_ITEM:
        prompt_inscribe_item();
        break;

#ifdef WIZARD
    case CMD_WIZARD:
        _handle_wizard_command();
        break;
#endif

    case CMD_SAVE_GAME:
        if (yesno("Save game and exit?", true, 'n'))
            save_game(true);
        break;

    case CMD_QUIT:
        if (yes_or_no("Are you sure you want to quit"))
            ouch(INSTANT_DEATH, NON_MONSTER, KILLED_BY_QUITTING);
        else
            canned_msg(MSG_OK);
        break;

    case CMD_REPEAT_CMD:
        _setup_cmd_repeat();
        break;

    case CMD_PREV_CMD_AGAIN:
        _do_prev_cmd_again();
        break;

    case CMD_NO_CMD:
    default:
        if (Options.tutorial_left)
        {
           std::string msg = "Unknown command. (For a list of commands type "
                             "<w>?\?<lightgrey>.)";
           print_formatted_paragraph(msg, get_number_of_cols());
        }
        else // well, not examine, but...
           mpr("Unknown command.", MSGCH_EXAMINE_FILTER);
        break;

    }
}

static void _prep_input()
{
    you.time_taken = player_speed();
    you.shield_blocks = 0;              // no blocks this round

    textcolor(LIGHTGREY);

    set_redraw_status( REDRAW_LINE_2_MASK | REDRAW_LINE_3_MASK );
    print_stats();
}

// Decrement a single duration. Print the message if the duration runs out.
// At midpoint (defined by get_expiration_threshold() in player.cc)
// print midmsg and decrease duration by midloss (a randomized amount so as
// to make it impossible to know the exact remaining duration for sure).
// NOTE: The maximum possible midloss should be greater than midpoint,
//       otherwise the duration may end in the same turn the warning
//       message is printed which would be a bit late.
static bool _decrement_a_duration(duration_type dur, const char* endmsg = NULL,
                                  int midloss = 0, const char* midmsg = NULL,
                                  msg_channel_type chan = MSGCH_DURATION)
{
    if (you.duration[dur] < 1)
        return (false);

    bool rc = false;
    const int midpoint = get_expiration_threshold(dur);

    if (you.duration[dur] > 1)
    {
        you.duration[dur]--;
        if (you.duration[dur] == midpoint)
        {
            if (midmsg)
                mpr(midmsg, chan);

            you.duration[dur] -= midloss;
        }
    }

    // In case midloss caused the duration to end prematurely.
    // (This really shouldn't happen, else the whole point of the
    //  "begins to time out" message is lost!)
    if (you.duration[dur] <= 1)
    {
        if (endmsg)
            mpr(endmsg, chan);

        rc = true;
        you.duration[dur] = 0;
    }

    return rc;
}

//  Perhaps we should write functions like: update_repel_undead(),
//  update_liquid_flames(), and so on. Even better, we could have a
//  vector of callback functions (or objects) which get installed
//  at some point.
static void _decrement_durations()
{
    if (wearing_amulet(AMU_THE_GOURMAND))
    {
        if (you.duration[DUR_GOURMAND] < GOURMAND_MAX && coinflip())
            you.duration[DUR_GOURMAND]++;
    }
    else
        you.duration[DUR_GOURMAND] = 0;

    // Must come before might/haste/berserk.
    if (_decrement_a_duration(DUR_BUILDING_RAGE))
        go_berserk(false);

    if (_decrement_a_duration(DUR_SLEEP))
        you.awake();

    // Sticky flame paradox: It both lasts longer and does more damage
    // overall if you're moving more slowly.
    //
    // Rationalisation: I guess it gets rubbed off/falls off/etc. if you
    // move around more.
    dec_napalm_player();

    if (_decrement_a_duration(DUR_ICY_ARMOUR,
                              "Your icy armour evaporates.", coinflip(),
                              "Your icy armour starts to melt."))
    {
        you.redraw_armour_class = true;
    }

    if (_decrement_a_duration(DUR_SILENCE, "Your hearing returns."))
        you.attribute[ATTR_WAS_SILENCED] = 0;

    _decrement_a_duration(DUR_REPEL_MISSILES,
                          "You feel less protected from missiles.",
                          coinflip(),
                          "Your repel missiles spell is about to expire...");

    _decrement_a_duration(DUR_DEFLECT_MISSILES,
                          "You feel less protected from missiles.",
                          coinflip(),
                          "Your deflect missiles spell is about to expire...");

    _decrement_a_duration(DUR_REGENERATION,
                          "Your skin stops crawling.",
                          coinflip(),
                          "Your skin is crawling a little less now.");

    if (you.duration[DUR_PRAYER] > 1)
        you.duration[DUR_PRAYER]--;
    else if (you.duration[DUR_PRAYER] == 1)
        end_prayer();

    if (you.duration[DUR_DIVINE_SHIELD] > 0)
    {
        if (you.duration[DUR_DIVINE_SHIELD] > 1)
        {
            if (--you.duration[DUR_DIVINE_SHIELD] == 1)
                mpr("Your divine shield starts to fade.", MSGCH_DURATION);
        }

        if (you.duration[DUR_DIVINE_SHIELD] == 1 && !one_chance_in(3))
        {
            you.redraw_armour_class = true;
            if (--you.attribute[ATTR_DIVINE_SHIELD] == 0)
            {
                you.duration[DUR_DIVINE_SHIELD] = 0;
                mpr("Your divine shield fades completely.", MSGCH_DURATION);
            }
        }
    }

    //jmf: More flexible weapon branding code.
    if (you.duration[DUR_WEAPON_BRAND] > 1)
        you.duration[DUR_WEAPON_BRAND]--;
    else if (you.duration[DUR_WEAPON_BRAND] == 1)
    {
        item_def& weapon = *you.weapon();
        const int temp_effect = get_weapon_brand(weapon);

        you.duration[DUR_WEAPON_BRAND] = 0;
        set_item_ego_type( weapon, OBJ_WEAPONS, SPWPN_NORMAL );
        std::string msg = weapon.name(DESC_CAP_YOUR);

        switch (temp_effect)
        {
        case SPWPN_VORPAL:
            if (get_vorpal_type(weapon) == DVORP_SLICING)
                msg += " seems blunter.";
            else
                msg += " feels lighter.";
            break;
        case SPWPN_FLAMING:
            msg += " goes out.";
            break;
        case SPWPN_FREEZING:
            msg += " stops glowing.";
            break;
        case SPWPN_VENOM:
            msg += " stops dripping with poison.";
            break;
        case SPWPN_DRAINING:
            msg += " stops crackling.";
            break;
        case SPWPN_DISTORTION:
            msg += " seems straighter.";
            break;
        case SPWPN_PAIN:
            msg += " seems less painful.";
            break;
        default:
            msg += " seems inexplicably less special.";
            break;
        }

        mpr(msg.c_str(), MSGCH_DURATION);
        you.wield_change = true;
    }

    // Vampire bat transformations are permanent (until ended).
    if (you.species != SP_VAMPIRE
        || you.attribute[ATTR_TRANSFORMATION] != TRAN_BAT
        || you.duration[DUR_TRANSFORMATION] <= 5)
    {
        if ( _decrement_a_duration(DUR_TRANSFORMATION, NULL, random2(3),
                                   "Your transformation is almost over.") )
        {
            untransform();
            you.duration[DUR_BREATH_WEAPON] = 0;
        }
    }

    // Must come after transformation duration.
    _decrement_a_duration(DUR_BREATH_WEAPON, "You have got your breath back.",
                          0, NULL, MSGCH_RECOVERY);

    _decrement_a_duration(DUR_REPEL_UNDEAD,
                          "Your holy aura fades away.", random2(3),
                          "Your holy aura is starting to fade.");
    _decrement_a_duration(DUR_SWIFTNESS,
                          "You feel sluggish.", coinflip(),
                          "You start to feel a little slower.");
    _decrement_a_duration(DUR_INSULATION,
                          "You feel conductive.", coinflip(),
                          "You start to feel a little less insulated.");

    if (_decrement_a_duration(DUR_STONEMAIL,
                              "Your scaly stone armour disappears.",
                              coinflip(),
                              "Your scaly stone armour is starting "
                              "to flake away."))
    {
        you.redraw_armour_class = true;
        burden_change();
    }

    if (_decrement_a_duration(DUR_FORESCRY,
                              "You feel firmly rooted in the present.",
                              coinflip(),
                              "Your vision of the future begins to falter."))
    {
        you.redraw_evasion = true;
    }

    if (_decrement_a_duration(DUR_SEE_INVISIBLE) && !player_see_invis())
        mpr("Your eyesight blurs momentarily.", MSGCH_DURATION);

    _decrement_a_duration(DUR_TELEPATHY, "You feel less empathic.");

    if (_decrement_a_duration(DUR_CONDENSATION_SHIELD))
        remove_condensation_shield();

    if (you.duration[DUR_CONDENSATION_SHIELD] > 0 && player_res_cold() < 0)
    {
        mpr("You feel very cold.");
        ouch(2 + random2avg(13, 2), NON_MONSTER, KILLED_BY_FREEZING);
    }

    if (_decrement_a_duration(DUR_MAGIC_SHIELD,
                              "Your magical shield disappears."))
    {
        you.redraw_armour_class = true;
    }

    if (_decrement_a_duration(DUR_STONESKIN, "Your skin feels tender."))
        you.redraw_armour_class = true;

    if (_decrement_a_duration(DUR_TELEPORT))
    {
        // Only to a new area of the abyss sometimes (for abyss teleports).
        you_teleport_now(true, one_chance_in(5));
        untag_followers();
    }

    _decrement_a_duration(DUR_CONTROL_TELEPORT,
                          "You feel uncertain.", coinflip(),
                          "You start to feel a little uncertain.");

    if (_decrement_a_duration(DUR_DEATH_CHANNEL,
                              "Your unholy channel expires.", coinflip(),
                              "Your unholy channel is weakening."))
    {
        you.attribute[ATTR_DIVINE_DEATH_CHANNEL] = 0;
    }

    _decrement_a_duration(DUR_SAGE, "You feel less studious.");
    _decrement_a_duration(DUR_STEALTH, "You feel less stealthy.");
    _decrement_a_duration(DUR_RESIST_FIRE, "Your fire resistance expires.");
    _decrement_a_duration(DUR_RESIST_COLD, "Your cold resistance expires.");
    _decrement_a_duration(DUR_RESIST_POISON, "Your poison resistance expires.");
    _decrement_a_duration(DUR_SLAYING, "You feel less lethal.");

    _decrement_a_duration(DUR_INVIS, "You flicker back into view.",
                          coinflip(), "You flicker for a moment.");

    _decrement_a_duration(DUR_BARGAIN, "You feel less charismatic.");
    _decrement_a_duration(DUR_CONF, "You feel less confused.");
    _decrement_a_duration(DUR_LOWERED_MR, "You feel more resistant to magic.");

    if (you.duration[DUR_PARALYSIS] || you.duration[DUR_PETRIFIED])
    {
        _decrement_a_duration(DUR_PARALYSIS);
        _decrement_a_duration(DUR_PETRIFIED);

        if (!you.duration[DUR_PARALYSIS] && !you.duration[DUR_PETRIFIED])
        {
            mpr("You can move again.", MSGCH_DURATION);
            you.redraw_evasion = true;
        }
    }

    _decrement_a_duration(DUR_EXHAUSTED, "You feel less fatigued.");

    _decrement_a_duration(DUR_CONFUSING_TOUCH,
                          ((std::string("Your ") + your_hand(true)) +
                          " stop glowing.").c_str());

    _decrement_a_duration(DUR_SURE_BLADE,
                          "The bond with your blade fades away." );

    if ( _decrement_a_duration(DUR_MESMERISED, "You break out of your daze.",
                               0, NULL, MSGCH_RECOVERY ))
    {
        you.mesmerised_by.clear();
    }

    dec_slow_player();
    dec_haste_player();

    if (_decrement_a_duration(DUR_MIGHT, "You feel a little less mighty now."))
        modify_stat(STAT_STRENGTH, -5, true, "might running out");

    if (_decrement_a_duration(DUR_BERSERKER, "You are no longer berserk."))
    {
        //jmf: Guilty for berserking /after/ berserk.
        did_god_conduct(DID_STIMULANTS, 6 + random2(6));

        // Sometimes berserk leaves us physically drained.
        //
        // Chance of passing out:
        //     - mutation gives a large plus in order to try and
        //       avoid the mutation being a "death sentence" to
        //       certain characters.
        //     - knowing the spell gives an advantage just
        //       so that people who have invested 3 spell levels
        //       are better off than the casual potion drinker...
        //       this should make it a bit more interesting for
        //       Crusaders again.
        //     - similarly for the amulet

        if (you.berserk_penalty != NO_BERSERK_PENALTY)
        {
            const int chance =
                10 + player_mutation_level(MUT_BERSERK) * 25
                + (wearing_amulet(AMU_RAGE) ? 10 : 0)
                + (player_has_spell(SPELL_BERSERKER_RAGE) ? 5 : 0);

            // Note the beauty of Trog!  They get an extra save that's at
            // the very least 20% and goes up to 100%.
            if (you.religion == GOD_TROG && x_chance_in_y(you.piety, 150)
                && !player_under_penance())
            {
                mpr("Trog's vigour flows through your veins.");
            }
            else if (one_chance_in(chance))
            {
                mpr("You pass out from exhaustion.", MSGCH_WARN);
                you.duration[DUR_PARALYSIS] += roll_dice(1, 4);
            }
        }

        if (you.duration[DUR_PARALYSIS] == 0
            && you.duration[DUR_PETRIFIED] == 0)
        {
            mpr("You are exhausted.", MSGCH_WARN);
        }

        // This resets from an actual penalty or from NO_BERSERK_PENALTY.
        you.berserk_penalty = 0;

        int dur = 12 + roll_dice(2, 12);
        you.duration[DUR_EXHAUSTED] += dur;

        // Don't trigger too many tutorial messages.
        const bool tut_slow = Options.tutorial_events[TUT_YOU_ENCHANTED];
        Options.tutorial_events[TUT_YOU_ENCHANTED] = false;

        {
            // Don't give duplicate 'You feel yourself slow down' messages.
            no_messages nm;
            slow_player(dur);
        }

        make_hungry(700, true);
        you.hunger = std::max(50, you.hunger);

        // 1KB: no berserk healing
        you.hp = (you.hp+1)*2/3;
        calc_hp();

        learned_something_new(TUT_POSTBERSERK);
        Options.tutorial_events[TUT_YOU_ENCHANTED] = tut_slow;
    }

    if (you.duration[DUR_BACKLIGHT] > 0 && !--you.duration[DUR_BACKLIGHT]
        && !you.backlit())
    {
        mpr("You are no longer glowing.", MSGCH_DURATION);
    }

    // Leak piety from the piety pool into actual piety.
    // Note that changes of religious status without corresponding actions
    // (killing monsters, offering items, ...) might be confusing for characters
    // of other religions.
    // For now, though, keep information about what happened hidden.
    if (you.piety < MAX_PIETY && you.duration[DUR_PIETY_POOL] > 0
        && one_chance_in(5))
    {
        you.duration[DUR_PIETY_POOL]--;
        gain_piety(1);

#if DEBUG_DIAGNOSTICS || DEBUG_SACRIFICE || DEBUG_PIETY
        mpr("Piety increases by 1 due to piety pool.", MSGCH_DIAGNOSTICS);

        if (you.duration[DUR_PIETY_POOL] == 0)
            mpr("Piety pool is now empty.", MSGCH_DIAGNOSTICS);
#endif
    }

    if (!you.permanent_levitation() && !you.permanent_flight())
    {
        if (_decrement_a_duration(DUR_LEVITATION,
                                  "You float gracefully downwards.",
                                  random2(6),
                                  "You are starting to lose your buoyancy!"))
        {
            burden_change();
            // Landing kills controlled flight.
            you.duration[DUR_CONTROLLED_FLIGHT] = 0;
            // Re-enter the terrain.
            move_player_to_grid( you.pos(), false, true, true );
        }
    }

    if (!you.permanent_flight())
    {
        if (_decrement_a_duration(DUR_CONTROLLED_FLIGHT) && you.airborne())
            mpr("You lose control over your flight.", MSGCH_DURATION);
    }

    if (you.rotting > 0)
    {
        // XXX: Mummies have an ability (albeit an expensive one) that
        // can fix rotted HPs now... it's probably impossible for them
        // to even start rotting right now, but that could be changed. -- bwr
        if (you.species == SP_MUMMY)
            you.rotting = 0;
        else if (x_chance_in_y(you.rotting, 20))
        {
            mpr("You feel your flesh rotting away.", MSGCH_WARN);
            ouch(1, NON_MONSTER, KILLED_BY_ROTTING);
            rot_hp(1);
            you.rotting--;
        }
    }

    // ghoul rotting is special, but will deduct from you.rotting
    // if it happens to be positive - because this is placed after
    // the "normal" rotting check, rotting attacks can be somewhat
    // more painful on ghouls - reversing order would make rotting
    // attacks somewhat less painful, but that seems wrong-headed {dlb}:
    if (you.species == SP_GHOUL)
    {
        if (one_chance_in(400))
        {
            mpr("You feel your flesh rotting away.", MSGCH_WARN);
            ouch(1, NON_MONSTER, KILLED_BY_ROTTING);
            rot_hp(1);

            if (you.rotting > 0)
                you.rotting--;
        }
    }

    dec_disease_player();

    dec_poison_player();

    if (you.duration[DUR_DEATHS_DOOR])
    {
        if (you.hp > allowed_deaths_door_hp())
        {
            mpr("Your life is in your own hands once again.", MSGCH_DURATION);
            you.duration[DUR_PARALYSIS] += 5 + random2(5);
            confuse_player( 10 + random2(10) );
            you.hp_max--;
            deflate_hp(you.hp_max, false);
            you.duration[DUR_DEATHS_DOOR] = 0;
        }
        else
            _decrement_a_duration(DUR_DEATHS_DOOR,
                                  "Your life is in your own hands again!",
                                  random2(6),
                                  "Your time is quickly running out!");
    }

    if (_decrement_a_duration(DUR_DIVINE_VIGOUR))
        remove_divine_vigour();

    if (_decrement_a_duration(DUR_DIVINE_STAMINA))
        remove_divine_stamina();

    _decrement_a_duration(DUR_REPEL_STAIRS_MOVE);
    _decrement_a_duration(DUR_REPEL_STAIRS_CLIMB);
}

static void _check_banished()
{
    if (you.banished)
    {
        you.banished = false;
        if (you.level_type != LEVEL_ABYSS)
        {
            mpr("You are cast into the Abyss!");
            banished(DNGN_ENTER_ABYSS, you.banished_by);
        }
        you.banished_by.clear();
    }
}

static void _check_shafts()
{
    for (int i = 0; i < MAX_TRAPS; i++)
    {
        trap_def &trap = env.trap[i];

        if (trap.type != TRAP_SHAFT)
            continue;

        ASSERT(in_bounds(trap.pos));

        handle_items_on_shaft(trap.pos, true);
    }
}

static void _check_sanctuary()
{
    if (env.sanctuary_time <= 0)
        return;

    decrease_sanctuary_radius();
}

void world_reacts()
{
    crawl_state.clear_mon_acting();

    if (!crawl_state.arena)
    {
        you.turn_is_over = true;
        religion_turn_end();
        crawl_state.clear_god_acting();
    }

#ifdef USE_TILE
    tiles.clear_text_tags(TAG_TUTORIAL);
    tiles.place_cursor(CURSOR_TUTORIAL, Region::NO_CURSOR);
#endif

    if (you.num_turns != -1)
    {
        if (you.num_turns < LONG_MAX)
            you.num_turns++;
        if (env.turns_on_level < INT_MAX)
            env.turns_on_level++;
        update_turn_count();
    }

    _check_banished();

    _check_shafts();

    _check_sanctuary();

    run_environment_effects();

    if (!you.cannot_act() && !player_mutation_level(MUT_BLURRY_VISION)
        && x_chance_in_y(you.skills[SK_TRAPS_DOORS], 50))
    {
        search_around(false); // Check nonadjacent squares too.
    }

    if (!crawl_state.arena)
        stealth = check_stealth();

#ifdef DEBUG_STEALTH
    // Too annoying for regular diagnostics.
    mprf(MSGCH_DIAGNOSTICS, "stealth: %d", stealth );
#endif

    if (you.special_wield != SPWLD_NONE)
        special_wielded();

    if (!crawl_state.arena && one_chance_in(10))
    {
        // this is instantaneous
        if (player_teleport() > 0 && one_chance_in(100 / player_teleport()))
            you_teleport_now( true );
        else if (you.level_type == LEVEL_ABYSS && one_chance_in(30))
            you_teleport_now( false, true ); // to new area of the Abyss
    }

    if (!crawl_state.arena && env.cgrid(you.pos()) != EMPTY_CLOUD)
        in_a_cloud();

    if (you.level_type == LEVEL_DUNGEON && you.duration[DUR_TELEPATHY])
        detect_creatures( 1 + you.duration[DUR_TELEPATHY] / 2, true );

    _decrement_durations();

    const int food_use = player_hunger_rate();

    if (food_use > 0 && you.hunger >= 40)
        make_hungry( food_use, true );

    // XXX: using an int tmp to fix the fact that hit_points_regeneration
    // is only an unsigned char and is thus likely to overflow. -- bwr
    int tmp = static_cast< int >( you.hit_points_regeneration );

    if (you.hp < you.hp_max && !you.disease && !you.duration[DUR_DEATHS_DOOR])
        tmp += player_regen();

    while (tmp >= 100)
    {
        inc_hp(1, false);
        tmp -= 100;
    }

    ASSERT( tmp >= 0 && tmp < 100 );
    you.hit_points_regeneration = static_cast< unsigned char >( tmp );

    // XXX: Doing the same as the above, although overflow isn't an
    // issue with magic point regeneration, yet. -- bwr
    tmp = static_cast< int >( you.magic_points_regeneration );

    if (you.magic_points < you.max_magic_points)
        tmp += 7 + you.max_magic_points / 2;

    while (tmp >= 100)
    {
        inc_mp(1, false);
        tmp -= 100;
    }

    ASSERT( tmp >= 0 && tmp < 100 );
    you.magic_points_regeneration = static_cast< unsigned char >( tmp );

    // If you're wielding a rod, it'll gradually recharge.
    _recharge_rods();

    viewwindow(true, true);

    if (Options.stash_tracking && !crawl_state.arena)
    {
        StashTrack.update_visible_stashes(
            Options.stash_tracking == STM_ALL ? StashTracker::ST_AGGRESSIVE
                                              : StashTracker::ST_PASSIVE);
    }

    handle_monsters();

    _check_banished();

    ASSERT(you.time_taken >= 0);
    // Make sure we don't overflow.
    ASSERT(DBL_MAX - you.elapsed_time > you.time_taken);

    you.elapsed_time += you.time_taken;

    if (you.synch_time <= you.time_taken)
    {
        handle_time(200 + (you.time_taken - you.synch_time));
        you.synch_time = 200;
        _check_banished();
    }
    else
    {
        const long old_synch_time = you.synch_time;
        you.synch_time -= you.time_taken;

        // Call spawn_random_monsters() more often than the rest of
        // handle_time() so the spawning rates work out correctly.
        if (old_synch_time >= 150 && you.synch_time < 150
            || old_synch_time >= 100 && you.synch_time < 100
            || old_synch_time >= 50 && you.synch_time < 50)
        {
            spawn_random_monsters();
        }
    }

    manage_clouds();

    if (you.duration[DUR_FIRE_SHIELD] > 0)
        manage_fire_shield();

    // Food death check.
    if (you.is_undead != US_UNDEAD && you.hunger <= 500)
    {
        if (!you.cannot_act() && one_chance_in(40))
        {
            mpr("You lose consciousness!", MSGCH_FOOD);
            stop_running();

            you.duration[DUR_PARALYSIS] += 5 + random2(8);

            if (you.duration[DUR_PARALYSIS] > 13)
                you.duration[DUR_PARALYSIS] = 13;
        }

        if (you.hunger <= 100)
        {
            mpr("You have starved to death.", MSGCH_FOOD);
            ouch(INSTANT_DEATH, NON_MONSTER, KILLED_BY_STARVATION);
        }
    }

    viewwindow(true, false);

    if (you.cannot_act() && any_messages())
        more();

#if DEBUG_TENSION || DEBUG_RELIGION
    if (you.religion != GOD_NO_GOD)
        mprf(MSGCH_DIAGNOSTICS, "TENSION = %d", get_tension());
#endif
}

#ifdef DGL_SIMPLE_MESSAGING

static struct stat mfilestat;

static void _show_message_line(std::string line)
{
    const std::string::size_type sender_pos = line.find(":");
    if (sender_pos == std::string::npos)
        mpr(line.c_str());
    else
    {
        std::string sender = line.substr(0, sender_pos);
        line = line.substr(sender_pos + 1);
        trim_string(line);
        formatted_string fs;
        fs.textcolor(WHITE);
        fs.cprintf("%s: ", sender.c_str());
        fs.textcolor(LIGHTGREY);
        fs.cprintf("%s", line.c_str());
        formatted_mpr(fs, MSGCH_PLAIN, 0);
        take_note(Note( NOTE_MESSAGE, MSGCH_PLAIN, 0,
                        (sender + ": " + line).c_str() ));
    }
}

static void _read_each_message()
{
    bool say_got_msg = true;
    FILE *mf = fopen(SysEnv.messagefile.c_str(), "r+");
    if (!mf)
    {
        mprf(MSGCH_ERROR, "Couldn't read %s: %s", SysEnv.messagefile.c_str(),
             strerror(errno));
        goto kill_messaging;
    }

    // Read messages, code borrowed from the SIMPLEMAIL patch.
    char line[120];

    if (!lock_file_handle(mf, F_RDLCK))
    {
        mprf(MSGCH_ERROR, "Failed to lock %s: %s", SysEnv.messagefile.c_str(),
             strerror(errno));
        goto kill_messaging;
    }

    while (fgets(line, sizeof line, mf))
    {
        unlock_file_handle(mf);

        const int len = strlen(line);
        if (len)
        {
            if (line[len - 1] == '\n')
                line[len - 1] = 0;

            if (say_got_msg)
            {
                mprf(MSGCH_PROMPT, "Your messages:");
                say_got_msg = false;
            }

            _show_message_line(line);
        }

        if (!lock_file_handle(mf, F_RDLCK))
        {
            mprf(MSGCH_ERROR, "Failed to lock %s: %s",
                 SysEnv.messagefile.c_str(),
                 strerror(errno));
            goto kill_messaging;
        }
    }
    if (!lock_file_handle(mf, F_WRLCK))
    {
        mprf(MSGCH_ERROR, "Unable to write lock %s: %s",
             SysEnv.messagefile.c_str(),
             strerror(errno));
    }
    if (!ftruncate(fileno(mf), 0))
        mfilestat.st_mtime = 0;
    unlock_file_handle(mf);
    fclose(mf);

    SysEnv.have_messages = false;
    return;

kill_messaging:
    if (mf)
        fclose(mf);
    SysEnv.have_messages = false;
    Options.messaging = false;
}

static void _read_messages()
{
    _read_each_message();
    update_message_status();
}

static void _announce_messages()
{
    // XXX: We could do a NetHack-like mail daemon here at some point.
    mprf("Beep! Your pager goes off! Use _ to check your messages.");
}

static void _check_messages()
{
    if (!Options.messaging
        || SysEnv.have_messages
        || SysEnv.messagefile.empty()
        || kbhit()
        || (SysEnv.message_check_tick++ % DGL_MESSAGE_CHECK_INTERVAL))
    {
        return;
    }

    const bool had_messages = SysEnv.have_messages;
    struct stat st;
    if (stat(SysEnv.messagefile.c_str(), &st))
    {
        mfilestat.st_mtime = 0;
        return;
    }

    if (st.st_mtime > mfilestat.st_mtime)
    {
        if (st.st_size)
            SysEnv.have_messages = true;
        mfilestat.st_mtime = st.st_mtime;
    }

    if (SysEnv.have_messages && !had_messages)
    {
        _announce_messages();
        update_message_status();
        // Recenter the cursor on the player.
        _center_cursor();
    }
}
#endif

static command_type _get_next_cmd()
{
#ifdef DGL_SIMPLE_MESSAGING
    _check_messages();
#endif

#if DEBUG_DIAGNOSTICS
    // Save hunger at start of round for use with hunger "delta-meter"
    // in output.cc.
    you.old_hunger = you.hunger;
#endif

#if DEBUG_ITEM_SCAN
    debug_item_scan();
#endif
#if DEBUG_MONS_SCAN
    debug_mons_scan();
#endif

    const time_t before = time(NULL);
    keycode_type keyin = _get_next_keycode();

    const time_t after = time(NULL);

    // Clamp idle time so that play time is more meaningful.
    if (after - before > IDLE_TIME_CLAMP)
    {
        you.real_time  += int(before - you.start_time) + IDLE_TIME_CLAMP;
        you.start_time  = after;
    }

    if (is_userfunction(keyin))
    {
        run_macro(get_userfunction(keyin).c_str());
        return (CMD_NEXT_CMD);
    }

    return _keycode_to_command(keyin);
}

// We handle the synthetic keys, key_to_command() handles the
// real ones.
static command_type _keycode_to_command( keycode_type key )
{
    switch ( key )
    {
#ifdef USE_TILE
    case CK_MOUSE_CMD:    return CMD_NEXT_CMD;
#endif

    case KEY_MACRO_DISABLE_MORE: return CMD_DISABLE_MORE;
    case KEY_MACRO_ENABLE_MORE:  return CMD_ENABLE_MORE;
    case KEY_REPEAT_KEYS:        return CMD_REPEAT_KEYS;

    default:
        return key_to_command(key, KC_DEFAULT);
    }
}

static keycode_type _get_next_keycode()
{
    keycode_type keyin;

    flush_input_buffer( FLUSH_BEFORE_COMMAND );

    mouse_control mc(MOUSE_MODE_COMMAND);
    keyin = unmangle_direction_keys(getch_with_command_macros());

    if (!is_synthetic_key(keyin))
        mesclr();

    return (keyin);
}

// Check squares adjacent to player for given feature and return how
// many there are.  If there's only one, return the dx and dy.
static int _check_adjacent(dungeon_feature_type feat, coord_def& delta)
{
    int num = 0;

    for ( adjacent_iterator ai(you.pos(), false); ai; ++ai )
    {
        if ( grd(*ai) == feat )
        {
            num++;
            delta = *ai - you.pos();
        }
    }

    return num;
}

// Opens doors and handles some aspects of untrapping. If either move_x or
// move_y are non-zero, the pair carries a specific direction for the door
// to be opened (eg if you type ctrl + dir).
static void _open_door(coord_def move, bool check_confused)
{
    ASSERT(!crawl_state.arena && !crawl_state.arena_suspended);

    if (you.attribute[ATTR_TRANSFORMATION] == TRAN_BAT)
    {
        mpr("You can't open doors in your present form.");
        return;
    }

    if (you.attribute[ATTR_HELD])
    {
        free_self_from_net();
        you.turn_is_over = true;
        return;
    }

    // If there's only one door to open, don't ask.
    if ((!check_confused || !you.confused()) && move.origin())
    {
        if (_check_adjacent(DNGN_CLOSED_DOOR, move) == 0)
        {
            mpr("There's nothing to open.");
            return;
        }
    }

    if (check_confused && you.confused() && !one_chance_in(3))
    {
        move.x = random2(3) - 1;
        move.y = random2(3) - 1;
    }

    dist door_move;
    door_move.delta = move;
    coord_def doorpos;

    if (!move.origin())
    {
        doorpos = you.pos() + move;

        const int mon = mgrd(doorpos);

        if (mon != NON_MONSTER && player_can_hit_monster(&menv[mon]))
        {
            if (mons_is_caught(&menv[mon]))
            {
                std::string prompt  = "Do you want to try to take the net off ";
                            prompt += (&menv[mon])->name(DESC_NOCAP_THE);
                            prompt += '?';

                if (yesno(prompt.c_str(), true, 'n'))
                {
                    remove_net_from(&menv[mon]);
                    return;
                }

            }

            you.turn_is_over = true;
            you_attack(mgrd(doorpos), true);

            if (you.berserk_penalty != NO_BERSERK_PENALTY)
                you.berserk_penalty = 0;

            return;
        }

        if (find_trap(doorpos) && grd(doorpos) != DNGN_UNDISCOVERED_TRAP)
        {
            if (env.cgrid(doorpos) != EMPTY_CLOUD)
            {
                mpr("You can't get to that trap right now.");
                return;
            }

            disarm_trap(doorpos);
            return;
        }
    }
    else
    {
        mpr("Which direction?", MSGCH_PROMPT);
        direction( door_move, DIR_DIR );

        if (!door_move.isValid)
            return;

        doorpos = you.pos() + door_move.delta;

        const dungeon_feature_type feat =
            in_bounds(doorpos) ? grd(doorpos) : DNGN_UNSEEN;

        if (feat != DNGN_CLOSED_DOOR)
        {
            switch (feat)
            {
            case DNGN_OPEN_DOOR:
                mpr("It's already open!"); break;
            default:
                mpr("There isn't anything that you can open there!");
                break;
            }
            // Don't lose a turn.
            return;
        }
    }

    if (grd(doorpos) == DNGN_CLOSED_DOOR)
    {
        std::set<coord_def> all_door;
        find_connected_range(doorpos, DNGN_CLOSED_DOOR,
                             DNGN_SECRET_DOOR, all_door);
        const char *adj, *noun;
        get_door_description(all_door.size(), &adj, &noun);

        int skill = you.dex +
            (you.skills[SK_TRAPS_DOORS] + you.skills[SK_STEALTH]) / 2;

        if (you.duration[DUR_BERSERKER])
        {
            // XXX: Better flavour for larger doors?
            if (silenced(you.pos()))
                mprf("The %s%s flies open!", adj, noun);
            else
            {
                mprf(MSGCH_SOUND, "The %s%s flies open with a bang!",
                     adj, noun);
                noisy(15, you.pos());
            }
        }
        else if (one_chance_in(skill) && !silenced(you.pos()))
        {
            mprf(MSGCH_SOUND, "As you open the %s%s, it creaks loudly!",
                 adj, noun);
            noisy(10, you.pos());
        }
        else
        {
            const char* verb =
                player_is_airborne() ? "reach down and open" : "open";
            mprf( "You %s the %s%s.", verb, adj, noun );
        }

        bool seen_secret = false;
        for (std::set<coord_def>::iterator i = all_door.begin();
             i != all_door.end(); ++i)
        {
            const coord_def& dc = *i;
            // Even if some of the door is out of LOS, we want the entire
            // door to be updated.  Hitting this case requires a really big
            // door!
            if (is_terrain_seen(dc))
            {
                set_envmap_obj(dc, DNGN_OPEN_DOOR);
#ifdef USE_TILE
                env.tile_bk_bg[dc.x][dc.y] = TILE_DNGN_OPEN_DOOR;
#endif
                if (!seen_secret && grd(dc) == DNGN_SECRET_DOOR)
                {
                    seen_secret = true;
                    dungeon_feature_type secret
                        = grid_secret_door_appearance(dc);
                    mprf("That %s was a secret door!",
                         feature_description(secret, NUM_TRAPS, false,
                                             DESC_PLAIN, false).c_str());
                }
            }
            grd(dc) = DNGN_OPEN_DOOR;
        }
        you.turn_is_over = true;
    }
    else if (grd(doorpos) == DNGN_OPEN_DOOR)
        _close_door(move); // for convenience
    else
    {
        mpr("You swing at nothing.");
        make_hungry(3, true);
        you.turn_is_over = true;
    }
}

static void _close_door(coord_def move)
{
    if (you.attribute[ATTR_TRANSFORMATION] == TRAN_BAT)
    {
        mpr("You can't close doors in your present form.");
        return;
    }

    if (you.attribute[ATTR_HELD])
    {
        mpr("You can't close doors while held in a net.");
        return;
    }

    dist door_move;
    coord_def doorpos;

    // If there's only one door to close, don't ask.
    if (!you.confused() && move.origin())
    {
        int num = _check_adjacent(DNGN_OPEN_DOOR, move);
        if (num == 0)
        {
            mpr("There's nothing to close.");
            return;
        }
        else if (num == 1 && move.origin())
        {
            mpr("You can't close doors on yourself!");
            return;
        }
    }

    if (you.confused() && !one_chance_in(3))
    {
        move.x = random2(3) - 1;
        move.y = random2(3) - 1;
    }

    door_move.delta = move;

    if (move.origin())
    {
        mpr("Which direction?", MSGCH_PROMPT);
        direction( door_move, DIR_DIR );
        if (!door_move.isValid)
            return;
    }

    if (door_move.delta.origin())
    {
        mpr("You can't close doors on yourself!");
        return;
    }

    doorpos = you.pos() + door_move.delta;

    const dungeon_feature_type feat =
        in_bounds(doorpos) ? grd(doorpos) : DNGN_UNSEEN;

    if (feat == DNGN_OPEN_DOOR)
    {
        std::set<coord_def> all_door;
        find_connected_identical(doorpos, grd(doorpos), all_door);
        const char *adj, *noun;
        get_door_description(all_door.size(), &adj, &noun);

        for (std::set<coord_def>::const_iterator i = all_door.begin();
             i != all_door.end(); ++i)
        {
            const coord_def& dc = *i;
            if (mgrd(dc) != NON_MONSTER)
            {
                // Need to make sure that turn_is_over is set if creature is
                // invisible.
                if (!player_monster_visible(&menv[mgrd(dc)]))
                {
                    mprf("Something is blocking the %sway!", noun);
                    you.turn_is_over = true;
                }
                else
                    mprf("There's a creature in the %sway!", noun);
                return;
            }

            if (igrd(dc) != NON_ITEM)
            {
                mprf("There's something blocking the %sway.", noun);
                return;
            }

            if (you.pos() == dc)
            {
                mprf("There's a thick-headed creature in the %sway!", noun);
                return;
            }
        }

        int skill = you.dex +
            (you.skills[SK_TRAPS_DOORS] + you.skills[SK_STEALTH]) / 2;

        if (you.duration[DUR_BERSERKER])
        {
            if (silenced(you.pos()))
            {
                mprf("You slam the %s%s shut!", adj, noun);
            }
            else
            {
                mprf(MSGCH_SOUND,
                     "You slam the %s%s shut with an echoing bang!",
                     adj, noun);
                noisy(25, you.pos());
            }
        }
        else if (one_chance_in(skill) && !silenced(you.pos()))
        {
            mprf(MSGCH_SOUND, "As you close the %s%s, it creaks loudly!",
                 adj, noun);
            noisy(10, you.pos());
        }
        else
        {
            const char* verb = player_is_airborne() ? "reach down and close"
                                                    : "close";
            mprf( "You %s the %s%s.", verb, adj, noun );
        }

        for (std::set<coord_def>::const_iterator i = all_door.begin();
             i != all_door.end(); ++i)
        {
            const coord_def& dc = *i;
            grd(dc) = DNGN_CLOSED_DOOR;
            // Even if some of the door is out of LOS once it's closed (or even
            // if some of it is out of LOS when it's open), we want the entire
            // door to be updated.
            if (is_terrain_seen(dc))
            {
                set_envmap_obj(dc, DNGN_CLOSED_DOOR);
#ifdef USE_TILE
                env.tile_bk_bg[dc.x][dc.y] = TILE_DNGN_CLOSED_DOOR;
#endif
            }
        }
        you.turn_is_over = true;
    }
    else
    {
        switch (feat)
        {
        case DNGN_CLOSED_DOOR:
            mpr("It's already closed!"); break;
        default:
            mpr("There isn't anything that you can close there!"); break;
        }
    }
}

// Initialise whole lot of stuff...
// Returns true if a new character.
static bool _initialise(void)
{
    Options.fixup_options();

    // Read the options the player used last time they created a new
    // character.
    read_startup_prefs();

    you.symbol = '@';
    you.colour = LIGHTGREY;

    seed_rng();
    get_typeid_array().init(ID_UNKNOWN_TYPE);
    init_char_table(Options.char_set);
    init_feature_table();
    init_monster_symbols();
    init_spell_descs();        // This needs to be way up top. {dlb}
    init_mon_name_cache();

    // init_item_name_cache() needs to be redone after init_char_table()
    // and init_feature_table() have been called, so that the glyphs will
    // be set to use with item_names_by_glyph_cache.
    init_item_name_cache();

    msg::initialise_mpr_streams();

    // Init item array.
    for (int i = 0; i < MAX_ITEMS; i++)
        init_item(i);

    // Empty messaging string.
    info[0] = 0;

    for (int i = 0; i < MAX_MONSTERS; i++)
        menv[i].reset();

    igrd.init(NON_ITEM);
    mgrd.init(NON_MONSTER);
    env.map.init(map_cell());

    you.unique_creatures.init(false);
    you.unique_items.init(UNIQ_NOT_EXISTS);

    // Set up the Lua interpreter for the dungeon builder.
    init_dungeon_lua();

    // Read special levels and vaults.
    read_maps();

    // Initialise internal databases.
    databaseSystemInit();

    init_feat_desc_cache();
    init_spell_name_cache();
    init_spell_rarities();

    cio_init();

    // System initialisation stuff.
    textbackground(0);

    clrscr();

#ifdef DEBUG_DIAGNOSTICS
    sanity_check_mutation_defs();
    if (crawl_state.map_stat_gen)
    {
        generate_map_stats();
        end(0, false);
    }
#endif

    if (crawl_state.arena)
    {
        run_map_preludes();
        initialise_item_descriptions();
#ifdef USE_TILE
        tiles.initialise_items();
#endif

        run_arena();
        end(0, false);
    }

    // Sets up a new game.
    const bool newc = new_game();
    if (!newc)
        restore_game();

    // Fix the mutation definitions for the species we're playing.
    fixup_mutations();

    // Load macros
    macro_init();

    crawl_state.need_save = true;

    calc_hp();
    calc_mp();

    run_map_preludes();

    if (newc && you.char_direction == GDT_GAME_START)
    {
        // Chaos Knights of Lugonu start out in the Abyss.
        you.level_type  = LEVEL_ABYSS;
        you.entry_cause = EC_UNKNOWN;
    }

    load(you.entering_level ? you.transit_stair : DNGN_STONE_STAIRS_DOWN_I,
         you.entering_level ? LOAD_ENTER_LEVEL :
         newc               ? LOAD_START_GAME : LOAD_RESTART_GAME,
         NUM_LEVEL_AREA_TYPES, -1, you.where_are_you);

    if (newc && you.char_direction == GDT_GAME_START)
    {
        // Randomise colours properly for the Abyss.
        init_pandemonium();
    }

#if DEBUG_DIAGNOSTICS
    // Debug compiles display a lot of "hidden" information, so we auto-wiz.
    you.wizard = true;
#endif

    init_properties();
    burden_change();
    make_hungry(0, true);

    you.redraw_strength     = true;
    you.redraw_intelligence = true;
    you.redraw_dexterity    = true;
    you.redraw_armour_class = true;
    you.redraw_evasion      = true;
    you.redraw_experience   = true;
    you.redraw_quiver       = true;
    you.wield_change        = true;

    // Start timer on session.
    you.start_time = time( NULL );

#ifdef CLUA_BINDINGS
    clua.runhook("chk_startgame", "b", newc);
    std::string yname = you.your_name;
    read_init_file(true);
    Options.fixup_options();
    strncpy(you.your_name, yname.c_str(), kNameLen);
    you.your_name[kNameLen - 1] = 0;

    // In case Lua changed the character set.
    init_char_table(Options.char_set);
    init_feature_table();
    init_monster_symbols();
#endif

#ifdef USE_TILE
    tiles.resize();
#endif

    draw_border();
    new_level();
    update_turn_count();

    trackers_init_new_level(false);

    // Reset lava/water nearness check to unknown, so it'll be
    // recalculated for the next monster that tries to reach us.
    you.lava_in_sight = you.water_in_sight = -1;

    // Set vision radius to player's current vision.
    setLOSRadius(you.current_vision);
    init_exclusion_los();

    if (newc) // start a new game
    {
        you.friendly_pickup = Options.default_friendly_pickup;

        // Mark items in inventory as of unknown origin.
        origin_set_inventory(origin_set_unknown);

        // For a new game, wipe out monsters in LOS.
        zap_los_monsters();

        // For a newly started tutorial, turn secret doors into normal ones.
        if (Options.tutorial_left)
            tutorial_zap_secret_doors();
    }

#ifdef USE_TILE
    tiles.initialise_items();
    // Must re-run as the feature table wasn't initialized yet.
    TileNewLevel(newc);
#endif

    set_cursor_enabled(false);

    if (Options.stash_tracking && !crawl_state.arena)
    {
        StashTrack.update_visible_stashes(
            Options.stash_tracking == STM_ALL ? StashTracker::ST_AGGRESSIVE
                                              : StashTracker::ST_PASSIVE);
    }

    // This just puts the view up for the first turn.
    viewwindow(true, false);

    activate_notes(true);

    add_key_recorder(&repeat_again_rec);

    return (newc);
}

// An attempt to tone down berserk a little bit. -- bwross
//
// This function does the accounting for not attacking while berserk
// This gives a triangular number function for the additional penalty
// Turn:    1  2  3   4   5   6   7   8
// Penalty: 1  3  6  10  15  21  28  36
//
// Total penalty (including the standard one during upkeep is:
//          2  5  9  14  20  27  35  44
//
static void _do_berserk_no_combat_penalty(void)
{
    // Butchering/eating a corpse will maintain a blood rage.
    const int delay = current_delay_action();
    if (delay == DELAY_BUTCHER || delay == DELAY_EAT)
        return;

    if (you.berserk_penalty == NO_BERSERK_PENALTY)
        return;

    if (you.duration[DUR_BERSERKER])
    {
        you.berserk_penalty++;

        switch (you.berserk_penalty)
        {
        case 2:
            mpr("You feel a strong urge to attack something.", MSGCH_DURATION);
            break;
        case 4:
            mpr("You feel your anger subside.", MSGCH_DURATION);
            break;
        case 6:
            mpr("Your blood rage is quickly leaving you.", MSGCH_DURATION);
            break;
        }

        // I do these three separately, because the might and
        // haste counters can be different.
        you.duration[DUR_BERSERKER] -= you.berserk_penalty;
        if (you.duration[DUR_BERSERKER] < 1)
            you.duration[DUR_BERSERKER] = 1;

        you.duration[DUR_MIGHT] -= you.berserk_penalty;
        if (you.duration[DUR_MIGHT] < 1)
            you.duration[DUR_MIGHT] = 1;

        you.duration[DUR_HASTE] -= you.berserk_penalty;
        if (you.duration[DUR_HASTE] < 1)
            you.duration[DUR_HASTE] = 1;
    }
    return;
}                               // end do_berserk_no_combat_penalty()


// Called when the player moves by walking/running. Also calls attack
// function etc when necessary.
static void _move_player(int move_x, int move_y)
{
    _move_player( coord_def(move_x, move_y) );
}

static void _move_player(coord_def move)
{
    ASSERT(!crawl_state.arena && !crawl_state.arena_suspended);

    bool attacking = false;
    bool moving = true;         // used to prevent eventual movement (swap)
    bool swap = false;

    if (you.attribute[ATTR_HELD])
    {
        free_self_from_net();
        you.turn_is_over = true;
        return;
    }

    // When confused, sometimes make a random move
    if (you.confused())
    {
        if (!one_chance_in(3))
        {
            move.x = random2(3) - 1;
            move.y = random2(3) - 1;
            you.reset_prev_move();
        }

        const coord_def& new_targ = you.pos() + move;
        if (!in_bounds(new_targ) || !you.can_pass_through(new_targ))
        {
            you.turn_is_over = true;
            mpr("Ouch!");
            apply_berserk_penalty = true;
            crawl_state.cancel_cmd_repeat();

            return;
        }
    }

    const coord_def& targ = you.pos() + move;
    const dungeon_feature_type targ_grid  =  grd(targ);
    const unsigned short targ_monst = mgrd(targ);
    const bool           targ_pass  = you.can_pass_through(targ);

    // You can swap places with a friendly or good neutral monster if
    // you're not confused, or if both of you are inside a sanctuary.
    const bool can_swap_places = targ_monst != NON_MONSTER
                                 && !mons_is_stationary(&menv[targ_monst])
                                 && (mons_wont_attack(&menv[targ_monst])
                                       && !you.confused()
                                     || is_sanctuary(you.pos())
                                        && is_sanctuary(targ));

    // You cannot move away from a mermaid but you CAN fight monsters on
    // neighbouring squares.
    monsters *beholder = NULL;
    if (you.duration[DUR_MESMERISED] && !you.confused())
    {
        for (unsigned int i = 0; i < you.mesmerised_by.size(); i++)
        {
             monsters& mon = menv[you.mesmerised_by[i]];
             int olddist = grid_distance(you.pos(), mon.pos());
             int newdist = grid_distance(targ, mon.pos());

             if (olddist < newdist)
             {
                 beholder = &mon;
                 break;
             }
        }
    }

    if (you.running.check_stop_running())
    {
        // [ds] Do we need this? Shouldn't it be false to start with?
        you.turn_is_over = false;
        return;
    }

    coord_def mon_swap_dest;

    if (targ_monst != NON_MONSTER && !mons_is_submerged(&menv[targ_monst]))
    {
        monsters *mon = &menv[targ_monst];

        if (can_swap_places && !beholder)
        {
            if (swap_check(mon, mon_swap_dest))
                swap = true;
            else
                moving = false;
        }
        else if (!can_swap_places) // attack!
        {
            // XXX: Moving into a normal wall does nothing and uses no
            // turns or energy, but moving into a wall which contains
            // an invisible monster attacks the monster, thus allowing
            // the player to figure out which adjacent wall an invis
            // monster is in "for free".
            you.turn_is_over = true;
            you_attack( targ_monst, true );

            // We don't want to create a penalty if there isn't
            // supposed to be one.
            if (you.berserk_penalty != NO_BERSERK_PENALTY)
                you.berserk_penalty = 0;

            attacking = true;
        }
    }

    if (!attacking && targ_pass && moving && !beholder)
    {
        you.time_taken *= player_movement_speed();
        you.time_taken /= 10;
        if (!move_player_to_grid(targ, true, false, false, swap))
            return;

        if (swap)
            swap_places(&menv[targ_monst], mon_swap_dest);

        you.prev_move = move;
        move.reset();
        you.turn_is_over = true;
        request_autopickup();
    }

    // BCR - Easy doors single move
    if (targ_grid == DNGN_CLOSED_DOOR && Options.easy_open && !attacking)
    {
        _open_door(move.x, move.y, false);
        you.prev_move = move;
    }
    else if (!targ_pass && !attacking)
    {
        stop_running();
        move.reset();
        you.turn_is_over = false;
        crawl_state.cancel_cmd_repeat();
    }
    else if (beholder && !attacking)
    {
        mprf("You cannot move away from %s!",
            beholder->name(DESC_NOCAP_THE, true).c_str());
        return;
    }

    if (you.running == RMODE_START)
        you.running = RMODE_CONTINUE;

    if (you.level_type == LEVEL_ABYSS
        && (you.pos().x <= 15 || you.pos().x >= (GXM - 16)
            || you.pos().y <= 15 || you.pos().y >= (GYM - 16)))
    {
        area_shift();
        if (you.pet_target != MHITYOU)
            you.pet_target = MHITNOT;

#if DEBUG_DIAGNOSTICS
        mpr( "Shifting.", MSGCH_DIAGNOSTICS );
        int j = 0;
        for (int i = 0; i < MAX_ITEMS; i++)
            if (is_valid_item( mitm[i] ))
                ++j;

        mprf( MSGCH_DIAGNOSTICS, "Number of items present: %d", j );

        j = 0;
        for (int i = 0; i < MAX_MONSTERS; i++)
            if (menv[i].type != -1)
                ++j;

        mprf( MSGCH_DIAGNOSTICS, "Number of monsters present: %d", j);
        mprf( MSGCH_DIAGNOSTICS, "Number of clouds present: %d", env.cloud_no);
#endif
    }

    apply_berserk_penalty = !attacking;
}


static int _get_num_and_char_keyfun(int &ch)
{
    if (ch == CK_BKSP || isdigit(ch) || ch >= 128)
        return 1;

    return -1;
}

static int _get_num_and_char(const char* prompt, char* buf, int buf_len)
{
    if (prompt != NULL)
        mpr(prompt);

    line_reader reader(buf, buf_len);

    reader.set_keyproc(_get_num_and_char_keyfun);

    return reader.read_line(true);
}

static void _setup_cmd_repeat()
{
    if (is_processing_macro())
    {
        flush_input_buffer(FLUSH_ABORT_MACRO);
        crawl_state.cancel_cmd_again();
        crawl_state.cancel_cmd_repeat();
        return;
    }

    ASSERT(!crawl_state.is_repeating_cmd());

    char buf[80];

    // Function ensures that the buffer contains only digits.
    int ch = _get_num_and_char("Number of times to repeat, then command key: ",
                               buf, 80);

    if (ch == ESCAPE)
    {
        // This *might* be part of the trigger for a macro.
        keyseq trigger;
        trigger.push_back(ch);

        if (get_macro_buf_size() == 0)
        {
            // Was just a single ESCAPE key, so not a macro trigger.
            canned_msg( MSG_OK );
            crawl_state.cancel_cmd_again();
            crawl_state.cancel_cmd_repeat();
            flush_input_buffer(FLUSH_REPLAY_SETUP_FAILURE);
            return;
        }
        ch = getchm();
        trigger.push_back(ch);

        // Now that we have the entirety of the (possible) macro trigger,
        // clear out the keypress recorder so that we won't have recorded
        // the trigger twice.
        repeat_again_rec.clear();

        insert_macro_into_buff(trigger);

        ch = getchm();
        if (ch == ESCAPE)
        {
            if (get_macro_buf_size() > 0)
                // User pressed an Alt key which isn't bound to a macro.
                mpr("That key isn't bound to a macro.");
            else
                // Wasn't a macro trigger, just an ordinary escape.
                canned_msg( MSG_OK );

            crawl_state.cancel_cmd_again();
            crawl_state.cancel_cmd_repeat();
            flush_input_buffer(FLUSH_REPLAY_SETUP_FAILURE);
            return;
        }
        // *WAS* a macro trigger, keep going.
    }

    if (strlen(buf) == 0)
    {
        mpr("You must enter the number of times for the command to repeat.");

        crawl_state.cancel_cmd_again();
        crawl_state.cancel_cmd_repeat();
        flush_input_buffer(FLUSH_REPLAY_SETUP_FAILURE);

        return;
    }

    int count = atoi(buf);

    if (crawl_state.doing_prev_cmd_again)
        count = crawl_state.prev_cmd_repeat_goal;

    if (count <= 0)
    {
        canned_msg( MSG_OK );
        crawl_state.cancel_cmd_again();
        crawl_state.cancel_cmd_repeat();
        flush_input_buffer(FLUSH_REPLAY_SETUP_FAILURE);
        return;
    }

    if (crawl_state.doing_prev_cmd_again)
    {
        // If a "do previous command again" caused a command
        // repetition to be redone, the keys to be repeated are
        // already in the key recording buffer, so we just need to
        // discard all the keys saying how many times the command
        // should be repeated.
        do
        {
            repeat_again_rec.keys.pop_front();
        }
        while (repeat_again_rec.keys.size() > 0
               && repeat_again_rec.keys[0] != ch);

        repeat_again_rec.keys.pop_front();
    }

    // User can type space or enter and then the command key, in case
    // they want to repeat a command bound to a number key.
    c_input_reset(true);
    if (ch == ' ' || ch == CK_ENTER)
    {
        if (!crawl_state.doing_prev_cmd_again)
            repeat_again_rec.keys.pop_back();

        mpr("Enter command to be repeated: ");
        // Enable the cursor to read input. The cursor stays on while
        // the command is being processed, so subsidiary prompts
        // shouldn't need to turn it on explicitly.
        cursor_control con(true);

        crawl_state.waiting_for_command = true;

        ch = _get_next_keycode();

        crawl_state.waiting_for_command = false;
    }

    command_type cmd = _keycode_to_command( (keycode_type) ch);

    if (cmd != CMD_MOUSE_MOVE)
        c_input_reset(false);

    if (!is_processing_macro() && !_cmd_is_repeatable(cmd))
    {
        crawl_state.cancel_cmd_again();
        crawl_state.cancel_cmd_repeat();
        flush_input_buffer(FLUSH_REPLAY_SETUP_FAILURE);
        return;
    }

    if (!crawl_state.doing_prev_cmd_again && cmd != CMD_PREV_CMD_AGAIN)
        crawl_state.prev_cmd_keys = repeat_again_rec.keys;

    if (is_processing_macro())
    {
        // Put back in first key of the expanded macro, which
        // get_next_keycode() fetched
        repeat_again_rec.paused = true;
        macro_buf_add(ch, true);

        // If we're repeating a macro, get rid the keys saying how
        // many times to repeat, because the way that macros are
        // repeated means that the number keys will be repeated if
        // they aren't discarded.
        keyseq &keys = repeat_again_rec.keys;
        ch = keys[keys.size() - 1];
        while (isdigit(ch) || ch == ' ' || ch == CK_ENTER)
        {
            keys.pop_back();
            ASSERT(keys.size() > 0);
            ch = keys[keys.size() - 1];
        }
    }

    repeat_again_rec.paused = false;
    // Discard the setup for the command repetition, since what's
    // going to be repeated is yet to be typed, except for the fist
    // key typed which has to be put back in (unless we're repeating a
    // macro, in which case everything to be repeated is already in
    // the macro buffer).
    if (!is_processing_macro())
    {
        repeat_again_rec.clear();
        macro_buf_add(ch, crawl_state.doing_prev_cmd_again);
    }

    crawl_state.cmd_repeat_start     = true;
    crawl_state.cmd_repeat_count     = 0;
    crawl_state.repeat_cmd           = cmd;
    crawl_state.cmd_repeat_goal      = count;
    crawl_state.prev_cmd_repeat_goal = count;
    crawl_state.prev_repetition_turn = you.num_turns;

    crawl_state.cmd_repeat_started_unsafe = !i_feel_safe();

    crawl_state.input_line_strs.clear();
}

static void _do_prev_cmd_again()
{
    if (is_processing_macro())
    {
        mpr("Can't re-do previous command from within a macro.");
        flush_input_buffer(FLUSH_ABORT_MACRO);
        crawl_state.cancel_cmd_again();
        crawl_state.cancel_cmd_repeat();
        return;
    }

    if (crawl_state.prev_cmd == CMD_NO_CMD)
    {
        mpr("No previous command to re-do.");
        crawl_state.cancel_cmd_again();
        crawl_state.cancel_cmd_repeat();
        return;
    }

    ASSERT(!crawl_state.doing_prev_cmd_again
           || (crawl_state.is_repeating_cmd()
               && crawl_state.repeat_cmd == CMD_PREV_CMD_AGAIN));

    crawl_state.doing_prev_cmd_again = true;
    repeat_again_rec.paused          = false;

    if (crawl_state.prev_cmd == CMD_REPEAT_CMD)
    {
        crawl_state.cmd_repeat_start     = true;
        crawl_state.cmd_repeat_count     = 0;
        crawl_state.cmd_repeat_goal      = crawl_state.prev_cmd_repeat_goal;
        crawl_state.prev_repetition_turn = you.num_turns;
    }

    const keyseq &keys = crawl_state.prev_cmd_keys;
    ASSERT(keys.size() > 0);

    // Insert keys at front of input buffer, rather than at the end,
    // since if the player holds down the "`" key, then the buffer
    // might get two "`" in a row, and if the keys to be replayed go after
    // the second "`" then we get an assertion.
    macro_buf_add(keys, true);

    bool was_doing_repeats = crawl_state.is_repeating_cmd();

    _input();

    // crawl_state.doing_prev_cmd_again can be set to false
    // while input() does its stuff if something causes
    // crawl_state.cancel_cmd_again() to be called.
    while (!was_doing_repeats && crawl_state.is_repeating_cmd()
           && crawl_state.doing_prev_cmd_again)
    {
        _input();
    }

    if (!was_doing_repeats && crawl_state.is_repeating_cmd()
        && !crawl_state.doing_prev_cmd_again)
    {
        crawl_state.cancel_cmd_repeat();
    }

    crawl_state.doing_prev_cmd_again = false;
}

static void _update_replay_state()
{
    if (crawl_state.is_repeating_cmd())
    {
        // First repeat is to copy down the keys the user enters,
        // grab them so we can go on autopilot for the remaining
        // iterations.
        if (crawl_state.cmd_repeat_start)
        {
            ASSERT(repeat_again_rec.keys.size() > 0);

            crawl_state.cmd_repeat_start = false;
            crawl_state.repeat_cmd_keys  = repeat_again_rec.keys;

            // Setting up the "previous command key sequence"
            // for a repeated command is different from normal,
            // since in addition to all of the keystrokes for
            // the command, it needs the repeat command plus the
            // number of repeats at the very beginning of the
            // sequence.
            if (!crawl_state.doing_prev_cmd_again)
            {
                keyseq &prev = crawl_state.prev_cmd_keys;
                keyseq &curr = repeat_again_rec.keys;

                if (is_processing_macro())
                    prev = curr;
                else
                {
                    // Skip first key, because that's command key that's
                    // being repeated, which crawl_state.prev_cmd_keys
                    // aleardy contains.
                    keyseq::iterator begin = curr.begin();
                    begin++;

                    prev.insert(prev.end(), begin, curr.end());
                }
            }

            repeat_again_rec.paused = true;
            macro_buf_add(KEY_REPEAT_KEYS);
        }
    }

    if (!crawl_state.is_replaying_keys() && !crawl_state.cmd_repeat_start
        && crawl_state.prev_cmd != CMD_NO_CMD)
    {
        if (repeat_again_rec.keys.size() > 0)
            crawl_state.prev_cmd_keys = repeat_again_rec.keys;
    }

    if (!is_processing_macro())
        repeat_again_rec.clear();
}


static void _compile_time_asserts()
{
    // Check that the numbering comments in enum.h haven't been
    // disturbed accidentally.
    COMPILE_CHECK(SK_UNARMED_COMBAT == 19       , c1);
    COMPILE_CHECK(SK_EVOCATIONS == 39           , c2);
    COMPILE_CHECK(SP_VAMPIRE == 33              , c3);
    COMPILE_CHECK(SPELL_BOLT_OF_MAGMA == 19     , c4);
    COMPILE_CHECK(SPELL_POISON_ARROW == 94      , c5);
    COMPILE_CHECK(NUM_SPELLS == 229             , c6);

    //jmf: NEW ASSERTS: we ought to do a *lot* of these
    COMPILE_CHECK(NUM_JOBS < JOB_UNKNOWN        , c7);

    // Also some runtime stuff; I don't know if the order of branches[]
    // needs to match the enum, but it currently does.
    for (int i = 0; i < NUM_BRANCHES; i++)
        ASSERT(branches[i].id == i);
}
