/*
 *  File:       output.cc
 *  Summary:    Functions used to print player related info.
 *  Written by: Linley Henzell
 *
 *  Modified for Crawl Reference by $Author$ on $Date$
 */

#include "AppHdr.h"
REVISION("$Rev$");

#include "output.h"

#include <stdlib.h>
#include <sstream>

#ifdef DOS
#include <conio.h>
#endif

#include "externs.h"

#include "abl-show.h"
#include "branch.h"
#include "cio.h"
#include "describe.h"
#include "directn.h"
#include "format.h"
#include "fight.h"
#include "initfile.h"
#include "itemname.h"
#include "itemprop.h"
#include "items.h"
#include "item_use.h"
#include "menu.h"
#include "message.h"
#include "misc.h"
#include "monstuff.h"
#include "mon-util.h"
#include "newgame.h"
#include "ouch.h"
#include "player.h"
#include "place.h"
#include "quiver.h"
#include "religion.h"
#include "skills2.h"
#include "stuff.h"
#include "transfor.h"
#include "tiles.h"
#include "travel.h"
#include "view.h"

// Color for captions like 'Health:', 'Str:', etc.
#define HUD_CAPTION_COLOR Options.status_caption_colour

// Colour for values, which come after captions.
static const short HUD_VALUE_COLOUR = LIGHTGREY;

// ----------------------------------------------------------------------
// colour_bar
// ----------------------------------------------------------------------

class colour_bar
{
    typedef unsigned short color_t;
 public:
    colour_bar(color_t default_colour,
               color_t change_pos,
               color_t change_neg,
               color_t empty)
        : m_default(default_colour), m_change_pos(change_pos),
          m_change_neg(change_neg), m_empty(empty),
          m_old_disp(-1),
          m_request_redraw_after(0)
    {
        // m_old_disp < 0 means it's invalid and needs to be initialized.
    }

    bool wants_redraw() const
    {
        return (m_request_redraw_after
                && you.num_turns >= m_request_redraw_after);
    }

    void draw(int ox, int oy, int val, int max_val)
    {
        ASSERT(val <= max_val);
        if (max_val <= 0)
        {
            m_old_disp = -1;
            return;
        }

        const int width = crawl_view.hudsz.x - (ox-1);
        const int disp  = width * val / max_val;
        const int old_disp = (m_old_disp < 0) ? disp : m_old_disp;
        m_old_disp = disp;

        cgotoxy(ox, oy, GOTO_STAT);

        textcolor(BLACK);
        for (int cx = 0; cx < width; cx++)
        {
#ifdef USE_TILE
            // Maybe this should use textbackground too?
            textcolor(BLACK + m_empty * 16);

            if (cx < disp)
                textcolor(BLACK + m_default * 16);
            else if (old_disp > disp && cx < old_disp)
                textcolor(BLACK + m_change_neg * 16);
            putch(' ');
#else
            if (cx < disp && cx < old_disp)
            {
                textcolor(m_default);
                putch('=');
            }
            else if (cx < disp)
            {
                textcolor(m_change_pos);
                putch('=');
            }
            else if (cx < old_disp)
            {
                textcolor(m_change_neg);
                putch('-');
            }
            else
            {
                textcolor(m_empty);
                putch('-');
            }
#endif

            // If some change colour was rendered, redraw in a few
            // turns to clear it out.
            if (old_disp != disp)
                m_request_redraw_after = you.num_turns + 4;
            else
                m_request_redraw_after = 0;
        }

        textcolor(LIGHTGREY);
        textbackground(BLACK);
    }

 private:
    const color_t m_default;
    const color_t m_change_pos;
    const color_t m_change_neg;
    const color_t m_empty;
    int m_old_disp;
    int m_request_redraw_after; // force a redraw at this turn count
};

colour_bar HP_Bar(LIGHTGREEN, GREEN, RED, DARKGREY);

#ifdef USE_TILE
colour_bar MP_Bar(BLUE, BLUE, LIGHTBLUE, DARKGREY);
#else
colour_bar MP_Bar(LIGHTBLUE, BLUE, MAGENTA, DARKGREY);
#endif

// ----------------------------------------------------------------------
// Status display
// ----------------------------------------------------------------------

static int _bad_ench_colour( int lvl, int orange, int red )
{
    if (lvl > red)
        return (RED);
    else if (lvl > orange)
        return (LIGHTRED);

    return (YELLOW);
}

static int _dur_colour( int running_out_color, bool running_out )
{
    if (running_out)
    {
        return running_out_color;
    }
    else
    {
        switch (running_out_color)
        {
        case GREEN:     return ( LIGHTGREEN );
        case BLUE:      return ( LIGHTBLUE );
        case MAGENTA:   return ( LIGHTMAGENTA );
        case LIGHTGREY: return ( WHITE );
        default: return running_out_color;
        }
    }
}

#ifdef DGL_SIMPLE_MESSAGING
void update_message_status()
{
    static const char *msg = "(Hit _)";
    static const int len = strlen(msg);
    static const std::string spc(len, ' ');

    textcolor(LIGHTBLUE);

    cgotoxy(crawl_view.hudsz.x - len + 1, 1, GOTO_STAT);
    if (SysEnv.have_messages)
        cprintf(msg);
    else
        cprintf(spc.c_str());
    textcolor(LIGHTGREY);
}
#endif

void update_turn_count()
{
    // Don't update turn counter when running/resting/traveling to
    // prevent pointless screen updates.
    if (!Options.show_gold_turns
        || you.running > 0
        || you.running < 0 && Options.travel_delay == -1)
    {
        return;
    }

    cgotoxy(19+6, 8, GOTO_STAT);

    // Show the turn count starting from 1. You can still quit on turn 0.
    textcolor(HUD_VALUE_COLOUR);
    cprintf("%ld", you.num_turns);
    textcolor(LIGHTGREY);
}

static int _count_digits(int val)
{
    if (val > 999)
        return 4;
    else if (val > 99)
        return 3;
    else if (val > 9)
        return 2;
    return 1;
}

static const char* _describe_hunger(int& color)
{
    const bool vamp = (you.species == SP_VAMPIRE);

    switch (you.hunger_state)
    {
        case HS_ENGORGED:
            color = LIGHTGREEN;
            return (vamp ? "Alive" : "Engorged");
        case HS_VERY_FULL:
            color = GREEN;
            return ("Very Full");
        case HS_FULL:
            color = GREEN;
            return ("Full");
        case HS_SATIATED: // normal
            color = GREEN;
            return NULL;
        case HS_HUNGRY:
            color = YELLOW;
            return (vamp ? "Thirsty" : "Hungry");
        case HS_VERY_HUNGRY:
            color = YELLOW;
            return (vamp ? "Very Thirsty" : "Very Hungry");
        case HS_NEAR_STARVING:
            color = YELLOW;
            return (vamp ? "Near Bloodless" : "Near Starving");
        case HS_STARVING:
        default:
            color = RED;
            return (vamp ? "Bloodless" : "Starving");
    }
}

static void _print_stats_mp(int x, int y)
{
    // Calculate colour
    short mp_colour = HUD_VALUE_COLOUR;
    {
        int mp_percent = (you.max_magic_points == 0
                          ? 100
                          : (you.magic_points * 100) / you.max_magic_points);

        for (unsigned int i = 0; i < Options.mp_colour.size(); ++i)
            if (mp_percent <= Options.mp_colour[i].first)
                mp_colour = Options.mp_colour[i].second;
    }

    cgotoxy(x+8, y, GOTO_STAT);
    textcolor(mp_colour);
    cprintf("%d", you.magic_points);
    textcolor(HUD_VALUE_COLOUR);
    cprintf("/%d", you.max_magic_points );

    int col = _count_digits(you.magic_points)
              + _count_digits(you.max_magic_points) + 1;
    for (int i = 11-col; i > 0; i--)
        cprintf(" ");

    if (! Options.classic_hud)
        MP_Bar.draw(19, y, you.magic_points, you.max_magic_points);
}

static void _print_stats_hp(int x, int y)
{
    const int max_max_hp = get_real_hp(true, true);

    // Calculate colour
    short hp_colour = HUD_VALUE_COLOUR;
    {
        const int hp_percent =
            (you.hp * 100) / (max_max_hp ? max_max_hp : you.hp);

        for (unsigned int i = 0; i < Options.hp_colour.size(); ++i)
            if (hp_percent <= Options.hp_colour[i].first)
                hp_colour = Options.hp_colour[i].second;
    }

    // 01234567890123456789
    // Health: xxx/yyy (zzz)
    cgotoxy(x, y, GOTO_STAT);
    textcolor(HUD_CAPTION_COLOR);
    cprintf(max_max_hp != you.hp_max ? "HP: " : "Health: ");
    textcolor(hp_colour);
    cprintf( "%d", you.hp );
    textcolor(HUD_VALUE_COLOUR);
    cprintf( "/%d", you.hp_max );
    if (max_max_hp != you.hp_max)
        cprintf( " (%d)", max_max_hp );

    int col = wherex() - crawl_view.hudp.x;
    for (int i = 18-col; i > 0; i--)
        cprintf(" ");

    if (!Options.classic_hud)
        HP_Bar.draw(19, y, you.hp, you.hp_max);
}

short _get_stat_colour(stat_type stat)
{
    int val = 0, max_val = 0;
    switch (stat)
    {
    case STAT_STRENGTH:
        val     = you.strength;
        max_val = you.max_strength;
        break;
    case STAT_INTELLIGENCE:
        val     = you.intel;
        max_val = you.max_intel;
        break;
    case STAT_DEXTERITY:
        val     = you.dex;
        max_val = you.max_dex;
        break;
    default:
        ASSERT(false);
    }

    // Check the stat_colour option for warning thresholds.
    for (unsigned int i = 0; i < Options.stat_colour.size(); ++i)
        if (val <= Options.stat_colour[i].first)
            return (Options.stat_colour[i].second);

    // Stat is magically increased.
    if (you.duration[DUR_DIVINE_STAMINA]
        || stat == STAT_STRENGTH && you.duration[DUR_MIGHT])
    {
        return (LIGHTBLUE);  // no end of effect warning
    }

    // Stat is degenerated.
    if (val < max_val)
        return (YELLOW);

    return (HUD_VALUE_COLOUR);
}

// XXX: alters state!  Does more than just print!
static void _print_stats_str(int x, int y)
{
    if (you.strength < 0)
        you.strength = 0;
    else if (you.strength > 72)
        you.strength = 72;

    if (you.max_strength > 72)
        you.max_strength = 72;

    cgotoxy(x+5, y, GOTO_STAT);

    textcolor(_get_stat_colour(STAT_STRENGTH));
    cprintf( "%d", you.strength );

    if (you.strength != you.max_strength)
        cprintf( " (%d)  ", you.max_strength );
    else
        cprintf( "       " );

    burden_change();
}

static void _print_stats_int(int x, int y)
{
    if (you.intel < 0)
        you.intel = 0;
    else if (you.intel > 72)
        you.intel = 72;

    if (you.max_intel > 72)
        you.max_intel = 72;

    cgotoxy(x+5, y, GOTO_STAT);

    textcolor(_get_stat_colour(STAT_INTELLIGENCE));
    cprintf( "%d", you.intel );

    if (you.intel != you.max_intel)
        cprintf( " (%d)  ", you.max_intel );
    else
        cprintf( "       " );
}

static void _print_stats_dex(int x, int y)
{
    if (you.dex < 0)
        you.dex = 0;
    else if (you.dex > 72)
        you.dex = 72;

    if (you.max_dex > 72)
        you.max_dex = 72;

    cgotoxy(x+5, y, GOTO_STAT);

    textcolor(_get_stat_colour(STAT_DEXTERITY));
    cprintf( "%d", you.dex );

    if (you.dex != you.max_dex)
        cprintf( " (%d)  ", you.max_dex );
    else
        cprintf( "       " );
}

static void _print_stats_ac(int x, int y)
{
    // AC:
    cgotoxy(x+4, y, GOTO_STAT);
    if (you.duration[DUR_STONEMAIL])
        textcolor(_dur_colour( BLUE, dur_expiring(DUR_STONEMAIL) ));
    else if (you.duration[DUR_ICY_ARMOUR] || you.duration[DUR_STONESKIN])
        textcolor( LIGHTBLUE );
    else
        textcolor( HUD_VALUE_COLOUR );
    cprintf( "%2d ", player_AC() );

    // SH: (two lines lower)
    cgotoxy(x+4, y+2, GOTO_STAT);
    if (you.duration[DUR_CONDENSATION_SHIELD] || you.duration[DUR_DIVINE_SHIELD])
        textcolor( LIGHTBLUE );
    else
        textcolor( HUD_VALUE_COLOUR );
    cprintf( "%2d ", player_shield_class() );
}

static void _print_stats_ev(int x, int y)
{
    cgotoxy(x+4, y, GOTO_STAT);
    textcolor(you.duration[DUR_FORESCRY] ? LIGHTBLUE : HUD_VALUE_COLOUR);
    cprintf( "%2d ", player_evasion() );
}

static void _print_stats_wp(int y)
{
    cgotoxy(1, y, GOTO_STAT);
    textcolor(Options.status_caption_colour);
    cprintf("Wp: ");
    if (you.weapon())
    {
        const item_def& wpn = *you.weapon();
        textcolor(wpn.colour);

        const std::string prefix = menu_colour_item_prefix(wpn);
        const int prefcol = menu_colour(wpn.name(DESC_INVENTORY), prefix);
        if (prefcol != -1)
            textcolor(prefcol);

        cprintf("%s",
                wpn.name(DESC_INVENTORY, true, false, true)
                .substr(0, crawl_view.hudsz.x - 4).c_str());
        textcolor(LIGHTGREY);
    }
    else
    {
        if (you.attribute[ATTR_TRANSFORMATION] == TRAN_BLADE_HANDS)
        {
            textcolor(RED);
            cprintf("Blade Hands");
            textcolor(LIGHTGREY);
        }
        else
        {
            textcolor(LIGHTGREY);
            cprintf("Nothing wielded");
        }
    }
    clear_to_end_of_line();
}

static void _print_stats_qv(int y)
{
    cgotoxy(1, y, GOTO_STAT);
    textcolor(Options.status_caption_colour);
    cprintf("Qv: ");

    int q = you.m_quiver->get_fire_item();

    ASSERT(q >= -1 && q < ENDOFPACK);
    if (q != -1)
    {
        const item_def& quiver = you.inv[q];
        textcolor(quiver.colour);

        const std::string prefix = menu_colour_item_prefix(quiver);
        const int prefcol =
            menu_colour(quiver.name(DESC_INVENTORY), prefix);

        if (prefcol != -1)
            textcolor(prefcol);

        cprintf("%s",
                quiver.name(DESC_INVENTORY, true)
                .substr(0, crawl_view.hudsz.x - 4)
                .c_str());
    }
    else
    {
        textcolor(LIGHTGREY);
        cprintf("Nothing quivered");
    }

    textcolor(LIGHTGREY);
    clear_to_end_of_line();
}

struct status_light
{
    status_light(int c, const char* t) : color(c), text(t) {}
    int color;
    const char* text;
};

// The colour scheme for these flags is currently:
//
// - yellow, "orange", red      for bad conditions
// - light grey, white          for god based conditions
// - green, light green         for good conditions
// - blue, light blue           for good enchantments
// - magenta, light magenta     for "better" enchantments (deflect, fly)
//
// Prints burden, hunger,
// pray, holy, teleport, regen, insulation, fly/lev, invis, silence,
//   conf. touch, bargain, sage
// confused, mesmerised, fire, poison, disease, rot, held, glow, swift,
//   fast, slow, breath
//
// Note the usage of bad_ench_colour() correspond to levels that
// can be found in player.cc, ie those that the player can tell by
// using the '@' command.  Things like confusion and sticky flame
// hide their amounts and are thus always the same colour (so
// we're not really exposing any new information). --bwr
static void _get_status_lights(std::vector<status_light>& out)
{
#if DEBUG_DIAGNOSTICS
    {
        static char static_pos_buf[80];
        snprintf(static_pos_buf, sizeof(static_pos_buf),
                 "%2d,%2d", you.pos().x, you.pos().y );
        out.push_back(status_light(LIGHTGREY, static_pos_buf));

        static char static_hunger_buf[80];
        snprintf(static_hunger_buf, sizeof(static_hunger_buf),
                 "(%d:%d)", you.hunger - you.old_hunger, you.hunger );
        out.push_back(status_light(LIGHTGREY, static_hunger_buf));
    }
#endif

    switch (you.burden_state)
    {
    case BS_OVERLOADED:
        out.push_back(status_light(RED, "Overloaded"));
        break;

    case BS_ENCUMBERED:
        out.push_back(status_light(LIGHTRED, "Encumbered"));
        break;

    case BS_UNENCUMBERED:
        break;
    }

    {
        int hunger_color;
        const char* hunger_text = _describe_hunger(hunger_color);
        if (hunger_text)
            out.push_back(status_light(hunger_color, hunger_text));
    }

    if (you.duration[DUR_PRAYER])
        out.push_back(status_light(WHITE, "Pray"));  // no end of effect warning

    if (you.duration[DUR_REPEL_UNDEAD])
    {
        int colour = _dur_colour( LIGHTGREY, dur_expiring(DUR_REPEL_UNDEAD) );
        out.push_back(status_light(colour, "Holy"));
    }

    if (you.duration[DUR_TELEPORT])
        out.push_back(status_light(LIGHTBLUE, "Tele"));

    if (you.duration[DUR_DEFLECT_MISSILES])
    {
        int color = _dur_colour( MAGENTA, dur_expiring(DUR_DEFLECT_MISSILES) );
        out.push_back(status_light(color, "DMsl"));
    }
    else if (you.duration[DUR_REPEL_MISSILES])
    {
        int color = _dur_colour( BLUE, dur_expiring(DUR_REPEL_MISSILES) );
        out.push_back(status_light(color, "RMsl"));
    }

    if (you.duration[DUR_REGENERATION])
    {
        int color = _dur_colour( BLUE, dur_expiring(DUR_REGENERATION) );
        out.push_back(status_light(color, "Regen"));
    }

    if (you.duration[DUR_INSULATION])
    {
        int color = _dur_colour( BLUE, dur_expiring(DUR_INSULATION) );
        out.push_back(status_light(color, "Ins"));
    }

    if (player_is_airborne())
    {
        const bool perm     = you.permanent_flight();
        const bool expiring = (!perm && dur_expiring(DUR_LEVITATION));
        if (wearing_amulet( AMU_CONTROLLED_FLIGHT ))
        {
            int color = _dur_colour( you.light_flight()? BLUE : MAGENTA,
                                      expiring);
            out.push_back(status_light(color, "Fly"));
        }
        else
        {
            int color = _dur_colour(BLUE, expiring);
            out.push_back(status_light(color, "Lev"));
        }
    }

    if (you.duration[DUR_INVIS])
    {
        int color = _dur_colour( BLUE, dur_expiring(DUR_INVIS) );
        out.push_back(status_light(color, "Invis"));
    }

    if (you.duration[DUR_SILENCE])
    {
        int color = _dur_colour( BLUE, dur_expiring(DUR_SILENCE) );
        out.push_back(status_light(color, "Sil"));
    }

    if (you.duration[DUR_CONFUSING_TOUCH])
    {
        int color = _dur_colour( BLUE, dur_expiring(DUR_CONFUSING_TOUCH) );
        out.push_back(status_light(color, "Touch"));
    }

    if (you.duration[DUR_BARGAIN])
    {
        int color = _dur_colour( BLUE, dur_expiring(DUR_BARGAIN) );
        out.push_back(status_light(color, "Brgn"));
    }

    if (you.duration[DUR_SAGE])
    {
        int color = _dur_colour( BLUE, dur_expiring(DUR_SAGE) );
        out.push_back(status_light(color, "Sage"));
    }

    if (you.duration[DUR_FIRE_SHIELD])
    {
        int color = _dur_colour( BLUE, dur_expiring(DUR_FIRE_SHIELD) );
        out.push_back(status_light(color, "RoF"));
    }

    if (you.duration[DUR_SURE_BLADE])
        out.push_back(status_light(BLUE, "Blade"));

    if (you.confused())
        out.push_back(status_light(RED, "Conf"));

    if (you.duration[DUR_LOWERED_MR])
        out.push_back(status_light(RED, "-MR"));

    // TODO: Differentiate between mermaids and sirens!
    if (you.duration[DUR_MESMERISED])
        out.push_back(status_light(RED, "Mesm"));

    if (you.duration[DUR_LIQUID_FLAMES])
        out.push_back(status_light(RED, "Fire"));

    if (you.duration[DUR_POISONING])
    {
        int color = _bad_ench_colour( you.duration[DUR_POISONING], 5, 10 );
        out.push_back(status_light(color, "Pois"));
    }

    if (you.disease)
    {
        int color = _bad_ench_colour( you.disease, 40, 120 );
        out.push_back(status_light(color, "Sick"));
    }

    if (you.rotting)
    {
        int color = _bad_ench_colour( you.rotting, 4, 8 );
        out.push_back(status_light(color, "Rot"));
    }

    if (you.attribute[ATTR_HELD])
        out.push_back(status_light(RED, "Held"));

    if (you.backlit(false))
    {
        int color = you.magic_contamination > 5
            ? _bad_ench_colour( you.magic_contamination, 15, 25 )
            : LIGHTBLUE;
        out.push_back(status_light(color, "Glow"));
    }

    if (you.duration[DUR_SWIFTNESS])
    {
        int color = _dur_colour( BLUE, dur_expiring(DUR_SWIFTNESS) );
        out.push_back(status_light(color, "Swift"));
    }

    if (you.duration[DUR_SLOW] && !you.duration[DUR_HASTE])
        out.push_back(status_light(RED, "Slow"));
    else if (you.duration[DUR_HASTE] && !you.duration[DUR_SLOW])
    {
        int color = _dur_colour( BLUE, dur_expiring(DUR_HASTE) );
        out.push_back(status_light(color, "Fast"));
    }

    if (you.duration[DUR_BREATH_WEAPON])
        out.push_back(status_light(YELLOW, "BWpn"));
}

static void _print_status_lights(int y)
{
    std::vector<status_light> lights;
    static int last_number_of_lights = 0;
    _get_status_lights(lights);
    if (lights.size() == 0 && last_number_of_lights == 0)
        return;
    last_number_of_lights = lights.size();

    size_t line_cur = y;
    const size_t line_end = crawl_view.hudsz.y+1;

    cgotoxy(1, line_cur, GOTO_STAT);
    ASSERT(wherex()-crawl_view.hudp.x == 0);

    size_t i_light = 0;
    while (true)
    {
        const int end_x = (wherex() - crawl_view.hudp.x)
                + (i_light < lights.size() ? strlen(lights[i_light].text)
                                           : 10000);

        if (end_x <= crawl_view.hudsz.x)
        {
            textcolor(lights[i_light].color);
            cprintf("%s", lights[i_light].text);
            if (end_x < crawl_view.hudsz.x)
                cprintf(" ");
            ++i_light;
        }
        else
        {
            clear_to_end_of_line();
            ++line_cur;
            // Careful not to trip the )#(*$ cgotoxy ASSERT
            if (line_cur == line_end)
                break;
            cgotoxy(1, line_cur, GOTO_STAT);
        }
    }
}

#ifdef USE_TILE
static bool _need_stats_printed()
{
    return you.redraw_hit_points
           || you.redraw_magic_points
           || you.redraw_armour_class
           || you.redraw_evasion
           || you.redraw_strength
           || you.redraw_intelligence
           || you.redraw_dexterity
           || you.redraw_experience
           || you.wield_change
           || you.redraw_quiver;
}
#endif

void print_stats(void)
{
    textcolor(LIGHTGREY);

    // Displayed evasion is now tied to dex.
    if (you.redraw_dexterity)
        you.redraw_evasion = true;

    if (HP_Bar.wants_redraw())
        you.redraw_hit_points = true;
    if (MP_Bar.wants_redraw())
        you.redraw_magic_points = true;

#ifdef USE_TILE
    bool has_changed = _need_stats_printed();
#endif

    if (you.redraw_hit_points)   { you.redraw_hit_points = false;   _print_stats_hp ( 1, 3); }
    if (you.redraw_magic_points) { you.redraw_magic_points = false; _print_stats_mp ( 1, 4); }
    if (you.redraw_armour_class) { you.redraw_armour_class = false; _print_stats_ac ( 1, 5); }
    if (you.redraw_evasion)      { you.redraw_evasion = false;      _print_stats_ev ( 1, 6); }

    if (you.redraw_strength)     { you.redraw_strength = false;     _print_stats_str(19, 5); }
    if (you.redraw_intelligence) { you.redraw_intelligence = false; _print_stats_int(19, 6); }
    if (you.redraw_dexterity)    { you.redraw_dexterity = false;    _print_stats_dex(19, 7); }

    int yhack = 0;

    // If Options.show_gold_turns, line 8 is Gold and Turns
    if (Options.show_gold_turns)
    {
        // Increase y-value for all following lines.
        yhack = 1;
        cgotoxy(1+6, 8, GOTO_STAT);
        textcolor(HUD_VALUE_COLOUR);
        cprintf("%-6d", you.gold);
    }

    if (you.redraw_experience)
    {
        cgotoxy(1,8 + yhack, GOTO_STAT);
        textcolor(Options.status_caption_colour);
#if DEBUG_DIAGNOSTICS
        cprintf("XP: ");
        textcolor(HUD_VALUE_COLOUR);
        cprintf("%d/%d (%d) ",
                you.skill_cost_level, you.exp_available, you.experience);
#else
        cprintf("Exp Pool: ");
        textcolor(HUD_VALUE_COLOUR);
        cprintf("%-6d", you.exp_available);
#endif
        you.redraw_experience = false;
    }

    if (you.wield_change)
    {
        // weapon_change is set in a billion places; probably not all
        // of them actually mean the user changed their weapon.  Calling
        // on_weapon_changed redundantly is normally OK; but if the user
        // is wielding a bow and throwing javelins, the on_weapon_changed
        // will switch them back to arrows, which is annoying.
        // Perhaps there should be another bool besides wield_change
        // that's set in fewer places?
        // Also, it's a little bogus to change simulation state in
        // render code.  We should find a better place for this.
        you.m_quiver->on_weapon_changed();
        _print_stats_wp(9 + yhack);
    }

    if (you.redraw_quiver || you.wield_change)
    {
        _print_stats_qv(10 + yhack);
        you.redraw_quiver = false;
    }
    you.wield_change  = false;

    if (you.redraw_status_flags)
    {
        you.redraw_status_flags = 0;
        _print_status_lights(11 + yhack);
    }
    textcolor(LIGHTGREY);

#ifdef USE_TILE
    if (has_changed)
        update_screen();
#else
    update_screen();
#endif
}

static std::string _level_description_string_hud()
{
    const PlaceInfo& place = you.get_place_info();
    std::string short_name = place.short_name();

    if (place.level_type == LEVEL_DUNGEON
        && branches[place.branch].depth > 1)
    {
        short_name += make_stringf(":%d", player_branch_depth());
    }
    // Indefinite articles
    else if (place.level_type == LEVEL_PORTAL_VAULT
             || place.level_type == LEVEL_LABYRINTH)
    {
        if (!you.level_type_name.empty())
        {
            // If the level name is faking a dungeon depth
            // (i.e., "Ziggurat:3") then don't add an article
            if (you.level_type_name.find(":") != std::string::npos)
                short_name = you.level_type_name;
            else
                short_name = article_a(upcase_first(you.level_type_name),
                                       false);
        }
        else
            short_name.insert(0, "A ");
    }
    // Definite articles
    else if (place.level_type == LEVEL_ABYSS)
    {
        short_name.insert(0, "The ");
    }
    return short_name;
}

// For some odd reason, only redrawn on level change.
void print_stats_level()
{
    int ypos = 8;
    if (Options.show_gold_turns)
        ypos++;
    cgotoxy(19, ypos, GOTO_STAT);
    textcolor(HUD_CAPTION_COLOR);
    cprintf("Place: ");

    textcolor(HUD_VALUE_COLOUR);
#if DEBUG_DIAGNOSTICS
    cprintf( "(%d) ", you.your_level + 1 );
#endif
    cprintf("%s", _level_description_string_hud().c_str());
    clear_to_end_of_line();
}

void redraw_skill(const std::string &your_name, const std::string &class_name)
{
    std::string title = your_name + " the " + class_name;

    unsigned int in_len = title.length();
    const unsigned int WIDTH = crawl_view.hudsz.x;
    if (in_len > WIDTH)
    {
        in_len -= 3;  // What we're getting back from removing "the".

        const unsigned int name_len = your_name.length();
        std::string trimmed_name = your_name;

        // Squeeze name if required, the "- 8" is to not squeeze too much.
        if (in_len > WIDTH && (name_len - 8) > (in_len - WIDTH))
        {
            trimmed_name =
                trimmed_name.substr(0, name_len - (in_len - WIDTH) - 1);
        }

        title = trimmed_name + ", " + class_name;
    }

    // Line 1: Foo the Bar    *WIZARD*
    cgotoxy(1, 1, GOTO_STAT);
    textcolor( YELLOW );
    if (title.size() > WIDTH)
        title.resize(WIDTH, ' ');
    cprintf( "%-*s", WIDTH, title.c_str() );
    if (you.wizard)
    {
        textcolor( LIGHTBLUE );
        cgotoxy(1 + crawl_view.hudsz.x-9, 1, GOTO_STAT);
        cprintf(" *WIZARD*");
    }
#ifdef DGL_SIMPLE_MESSAGING
    update_message_status();
#endif

    // Line 2:
    // Level N Minotaur [of God]
    textcolor( YELLOW );
    cgotoxy(1, 2, GOTO_STAT);
    nowrap_eol_cprintf("Level %d %s", you.experience_level,
                       species_name(you.species,you.experience_level).c_str());
    if (you.religion != GOD_NO_GOD)
        nowrap_eol_cprintf(" of %s", god_name(you.religion).c_str());
    clear_to_end_of_line();

    textcolor( LIGHTGREY );
}

void draw_border(void)
{
    textcolor( HUD_CAPTION_COLOR );
    clrscr();
    redraw_skill( you.your_name, player_title() );

    textcolor(Options.status_caption_colour);

    //cgotoxy( 1, 3, GOTO_STAT); cprintf("Hp:");
    cgotoxy( 1, 4, GOTO_STAT); cprintf("Magic:");
    cgotoxy( 1, 5, GOTO_STAT); cprintf("AC:");
    cgotoxy( 1, 6, GOTO_STAT); cprintf("EV:");
    cgotoxy( 1, 7, GOTO_STAT); cprintf("SH:");

    cgotoxy(19, 5, GOTO_STAT); cprintf("Str:");
    cgotoxy(19, 6, GOTO_STAT); cprintf("Int:");
    cgotoxy(19, 7, GOTO_STAT); cprintf("Dex:");

    if (Options.show_gold_turns)
    {
        cgotoxy( 1, 8, GOTO_STAT); cprintf("Gold:");
        cgotoxy(19, 8, GOTO_STAT); cprintf("Turn:");
    }
    // Line 9 (or 8) is exp pool, Level
}

// ----------------------------------------------------------------------
// Monster pane
// ----------------------------------------------------------------------

static bool _mons_hostile(const monsters *mon)
{
    return (!mons_friendly(mon) && !mons_neutral(mon));
}

static std::string _get_monster_name(const monster_pane_info& m,
                                     int count)
{
    std::string desc = "";
    const monsters *mon = m.m_mon;

    bool adj = false;
    if (mons_friendly(mon))
    {
        desc += "friendly ";
        adj = true;
    }
    else if (mons_neutral(mon))
    {
        desc += "neutral ";
        adj = true;
    }

    std::string monpane_desc;
    int col;
    m.to_string(count, monpane_desc, col);

    if (count == 1)
    {
        if (!mon->is_named())
        {
            desc = ((!adj && is_vowel(monpane_desc[0])) ? "an "
                                                        : "a ")
                   + desc;
        }
        else if (adj)
            desc = "the " + desc;
    }

    desc += monpane_desc;
    return (desc);
}

// Returns true if the first monster is more aggressive (in terms of
// hostile/neutral/friendly) than the second, or, if both monsters share the
// same attitude, if the first monster has a lower type.
// If monster type and attitude are the same, return false.
bool compare_monsters_attitude( const monsters *m1, const monsters *m2 )
{
    if (_mons_hostile(m1) && !_mons_hostile(m2))
        return (true);

    if (mons_neutral(m1))
    {
        if (mons_friendly(m2))
            return (true);
        if (_mons_hostile(m2))
            return (false);
    }

    if (mons_friendly(m1) && !mons_friendly(m2))
        return (false);

    // If we get here then monsters have the same attitude.
    // FIXME: replace with difficulty comparison
    return (m1->type < m2->type);
}

// If past is true, the messages should be printed in the past tense
// because they're needed for the morgue dump.
std::string mpr_monster_list(bool past)
{
    // Get monsters via the monster_pane_info, sorted by difficulty.
    std::vector<monster_pane_info> mons;
    get_monster_pane_info(mons);

    std::string msg = "";
    if (mons.empty())
    {
        msg  = "There ";
        msg += (past ? "were" : "are");
        msg += " no monsters in sight!";

        return (msg);
    }

    std::sort(mons.begin(), mons.end(), monster_pane_info::less_than_wrapper);
    std::vector<std::string> describe;

    int count = 0;
    for (unsigned int i = 0; i < mons.size(); ++i)
    {
        if (i > 0 && monster_pane_info::less_than(mons[i-1], mons[i]))
        {
            describe.push_back(_get_monster_name(mons[i-1], count).c_str());
            count = 0;
        }
        count++;
    }

    describe.push_back(_get_monster_name(mons[mons.size()-1], count).c_str());

    msg = "You ";
    msg += (past ? "could" : "can");
    msg += " see ";

    if (describe.size() == 1)
        msg += describe[0];
    else
    {
        msg += comma_separated_line(describe.begin(), describe.end(),
                                    ", and ", ", ");
    }
    msg += ".";

    return (msg);
}

monster_pane_info::monster_pane_info(const monsters *m)
    : m_mon(m), m_attitude(ATT_HOSTILE), m_difficulty(0),
      m_brands(0), m_fullname(true)
{
    // XXX: this doesn't take into account ENCH_NEUTRAL, but that's probably
    // a bug for mons_attitude, not this.
    // XXX: also, mons_attitude_type should be sorted hostile/neutral/friendly;
    // will break saves a little bit though.
    m_attitude = mons_attitude(m);

    int mtype = m->type;
    if (mtype == MONS_RAKSHASA_FAKE)
        mtype = MONS_RAKSHASA;

    // Currently, difficulty is defined as "average hp".
    m_difficulty = mons_difficulty(mtype);

    // [ds] XXX: Kill the magic numbers.
    if (mons_looks_stabbable(m))   m_brands |= 1;
    if (mons_looks_distracted(m))  m_brands |= 2;
    if (m->has_ench(ENCH_BERSERK)) m_brands |= 4;
}

// Needed because gcc 4.3 sort does not like comparison functions that take
// more than 2 arguments.
bool monster_pane_info::less_than_wrapper(const monster_pane_info& m1,
                                          const monster_pane_info& m2)
{
    return monster_pane_info::less_than(m1, m2, true);
}

// Sort monsters by (in that order):    attitude, difficulty, type, brand
bool monster_pane_info::less_than(const monster_pane_info& m1,
                                  const monster_pane_info& m2, bool zombified)
{
    if (m1.m_attitude < m2.m_attitude)
        return (true);
    else if (m1.m_attitude > m2.m_attitude)
        return (false);

    int m1type = m1.m_mon->type;
    int m2type = m2.m_mon->type;

    // Don't differentiate real rakshasas from fake ones.
    if (m1type == MONS_RAKSHASA_FAKE)
        m1type = MONS_RAKSHASA;
    if (m2type == MONS_RAKSHASA_FAKE)
        m2type = MONS_RAKSHASA;

    // Force plain but different coloured draconians to be treated like the
    // same sub-type.
    if (!zombified && m1type >= MONS_DRACONIAN
        && m1type <= MONS_PALE_DRACONIAN
        && m2type >= MONS_DRACONIAN
        && m2type <= MONS_PALE_DRACONIAN)
    {
        return (false);
    }

    // By descending difficulty
    if (m1.m_difficulty > m2.m_difficulty)
        return (true);
    else if (m1.m_difficulty < m2.m_difficulty)
        return (false);

    // Force mimics of different types to be treated like the same one.
    if (mons_is_mimic(m1type) && mons_is_mimic(m2type))
        return (false);

    if (m1type < m2type)
        return (true);
    else if (m1type > m2type)
        return (false);

    // Never distinguish between dancing weapons.
    // The above checks guarantee that *both* monsters are of this type.
    if (m1type == MONS_DANCING_WEAPON)
        return (false);

    if (zombified)
    {
        // Because of the type checks above, if one of the two is zombified, so
        // is the other, and of the same type.
        if (mons_is_zombified(m1.m_mon)
            && m1.m_mon->base_monster < m2.m_mon->base_monster)
        {
            return (true);
        }

        // Both monsters are hydras or hydra zombies, sort by number of heads.
        if (m1.m_mon->has_hydra_multi_attack()
            && m1.m_mon->number > m2.m_mon->number)
        {
            return (true);
        }
    }

    if (m1.m_fullname && m2.m_fullname || m1type == MONS_PLAYER_GHOST)
        return (m1.m_mon->name(DESC_PLAIN) < m1.m_mon->name(DESC_PLAIN));

#if 0 // for now, sort brands together.
    // By descending brands, so no brands sorts to the end
    if (m1.m_brands > m2.m_brands)
        return (true);
    else if (m1.m_brands < m2.m_brands)
        return (false);
#endif

    return (false);
}

static std::string _verbose_info(const monsters* m)
{
    if (mons_is_caught(m))
        return (" (caught)");

    if (mons_behaviour_perceptible(m))
    {
        if (mons_is_petrified(m))
            return(" (petrified)");
        if (mons_is_paralysed(m))
            return(" (paralysed)");
        if (mons_is_petrifying(m))
            return(" (petrifying)");
        if (mons_is_confused(m))
            return(" (confused)");
        if (mons_is_fleeing(m))
            return(" (fleeing)");
        if (mons_is_sleeping(m))
            return(" (sleeping)");
        if (mons_is_wandering(m) && !mons_is_batty(m))
            return(" (wandering)");
        if (m->foe == MHITNOT && !mons_is_batty(m) && !mons_neutral(m)
            && !mons_friendly(m))
        {
            return (" (unaware)");
        }
    }

    if (m->has_ench(ENCH_STICKY_FLAME))
        return (" (burning)");

    if (m->has_ench(ENCH_ROT))
        return (" (rotting)");

    if (m->has_ench(ENCH_INVIS))
        return (" (invisible)");

    return ("");
}

void monster_pane_info::to_string( int count, std::string& desc,
                                   int& desc_color) const
{
    std::ostringstream out;

    if (count == 1)
    {
        if (mons_is_mimic(m_mon->type))
            out << mons_type_name(m_mon->type, DESC_PLAIN);
        else
            out << m_mon->full_name(DESC_PLAIN);
    }
    else
    {
        // Don't differentiate between dancing weapons, mimics, or draconians
        // of different types.
        if (m_fullname
            && m_mon->type != MONS_DANCING_WEAPON
            && mons_genus(m_mon->type) != MONS_DRACONIAN
            && !mons_is_mimic(m_mon->type)
            && m_mon->mname.empty())
        {
            out << count << " "
                << pluralise(m_mon->name(DESC_PLAIN));
        }
        else if (m_mon->type >= MONS_DRACONIAN
                 && m_mon->type <= MONS_PALE_DRACONIAN)
        {
            out << count << " "
                << pluralise(mons_type_name(MONS_DRACONIAN, DESC_PLAIN));
        }
        else
        {
            out << count << " "
                << pluralise(mons_type_name(m_mon->type, DESC_PLAIN));
        }
    }

#if DEBUG_DIAGNOSTICS
    out << " av" << m_difficulty << " "
        << m_mon->hit_points << "/" << m_mon->max_hit_points;
#endif

    if (count == 1)
    {
        if (m_mon->has_ench(ENCH_BERSERK))
            out << " (berserk)";
        else if (Options.verbose_monster_pane)
            out << _verbose_info(m_mon);
        else if (mons_looks_stabbable(m_mon))
            out << " (resting)";
        else if (mons_looks_distracted(m_mon))
            out << " (distracted)";
        else if (m_mon->has_ench(ENCH_INVIS))
            out << " (invisible)";
    }

    // Friendliness
    switch (m_attitude)
    {
    case ATT_FRIENDLY:
        //out << " (friendly)";
        desc_color = GREEN;
        break;
    case ATT_GOOD_NEUTRAL:
    case ATT_NEUTRAL:
        //out << " (neutral)";
        desc_color = BROWN;
        break;
    case ATT_HOSTILE:
        // out << " (hostile)";
        desc_color = LIGHTGREY;
        break;
    }

    // Evilness of attacking
    switch (m_attitude)
    {
    case ATT_NEUTRAL:
    case ATT_HOSTILE:
        if (count == 1 && you.religion == GOD_SHINING_ONE
            && !tso_unchivalric_attack_safe_monster(m_mon)
            && is_unchivalric_attack(&you, m_mon))
        {
            desc_color = Options.evil_colour;
        }
        break;
    default:
        break;
    }

    desc = out.str();
}

#ifndef USE_TILE
static char _mlist_index_to_letter(int index)
{
    index += 'a';

    if (index >= 'b')
        index++;
    if (index >= 'h')
        index++;
    if (index >= 'j')
        index++;
    if (index >= 'k')
        index++;
    if (index >= 'l')
        index++;

    return (index);
}

static void _print_next_monster_desc(const std::vector<monster_pane_info>& mons,
                                     int& start, bool zombified = false,
                                     int idx = -1)
{
    // Skip forward to past the end of the range of identical monsters.
    unsigned int end;
    for (end = start + 1; end < mons.size(); ++end)
    {
        // Array is sorted, so if !(m1 < m2), m1 and m2 are "equal".
        if (monster_pane_info::less_than(mons[start], mons[end], zombified))
            break;
    }
    // Postcondition: all monsters in [start, end) are "equal"

    // Print info on the monsters we've found.
    {
        int printed = 0;

        // for targeting
        if (idx >= 0)
        {
            textcolor(WHITE);
            cprintf( stringize_glyph(_mlist_index_to_letter(idx)).c_str() );
            cprintf(" - ");
            printed += 4;
        }

        // One glyph for each monster.
        for (unsigned int i_mon = start; i_mon < end; i_mon++)
        {
            unsigned int glyph;
            unsigned short glyph_color;
            get_mons_glyph(mons[i_mon].m_mon, &glyph, &glyph_color);
            textcolor(glyph_color);
            // XXX: Hack to make the death cob (%) show up correctly.
            if (glyph == '%')
                cprintf("%%");
            else
                cprintf( stringize_glyph(glyph).c_str() );
            ++printed;

            // Printing too many looks pretty bad, though.
            if (i_mon > 6)
                break;
        }
        textcolor(LIGHTGREY);

        const int count = (end - start);

        if (count == 1)
        {
            // Print an "icon" representing damage level.
            const monsters *mon = mons[start].m_mon;
            std::string damage_desc;

            mon_dam_level_type damage_level;
            mons_get_damage_level(mon, damage_desc, damage_level);

            // If no messages about wounds, don't display damage level either.
            if (monster_descriptor(mon->type, MDSC_NOMSG_WOUNDS))
                damage_level = MDAM_OKAY;

            int dam_color;
            switch (damage_level)
            {
            // NOTE: In os x, light versions of foreground colors are OK,
            //       but not background colors.  So stick wth standards.
            case MDAM_DEAD:
            case MDAM_ALMOST_DEAD:
            case MDAM_SEVERELY_DAMAGED:   dam_color = RED;       break;
            case MDAM_HEAVILY_DAMAGED:    dam_color = MAGENTA;   break;
            case MDAM_MODERATELY_DAMAGED: dam_color = BROWN;     break;
            case MDAM_LIGHTLY_DAMAGED:    dam_color = GREEN;     break;
            case MDAM_OKAY:               dam_color = GREEN;     break;
            default:                      dam_color = CYAN;      break;
            }
            cprintf(" ");
            textbackground(dam_color);
            textcolor(dam_color);
            // Temporary, to diagnose 1933260
            cprintf("_");
            textbackground(BLACK);
            textcolor(LIGHTGREY);
            cprintf(" ");
            printed += 3;
        }
        else
        {
            textcolor(LIGHTGREY);
            cprintf("  ");
            printed += 2;
        }

        if (printed < crawl_view.mlistsz.x)
        {
            int desc_color;
            std::string desc;
            mons[start].to_string(count, desc, desc_color);
            textcolor(desc_color);
            desc.resize(crawl_view.mlistsz.x-printed, ' ');
            cprintf("%s", desc.c_str());
        }
    }

    // Set start to the next un-described monster.
    start = end;
    textcolor(LIGHTGREY);
}
#endif

void get_monster_pane_info(std::vector<monster_pane_info>& mons)
{
    std::vector<monsters*> visible = get_nearby_monsters();
    for (unsigned int i = 0; i < visible.size(); i++)
    {
        if (Options.target_zero_exp
            || !mons_class_flag( visible[i]->type, M_NO_EXP_GAIN ))
        {
            mons.push_back(monster_pane_info(visible[i]));
        }
    }
    std::sort(mons.begin(), mons.end(), monster_pane_info::less_than_wrapper);
}

#ifndef USE_TILE
#define BOTTOM_JUSTIFY_MONSTER_LIST 0
// Returns -1 if the monster list is empty, 0 if there are so many monsters
// they have to be consolidated, and 1 otherwise.
int update_monster_pane()
{
    if (!map_bounds(you.pos()))
        return (-1);

    const int max_print = crawl_view.mlistsz.y;
    textbackground(BLACK);

    if (max_print <= 0)
        return (-1);

    std::vector<monster_pane_info> mons;
    get_monster_pane_info(mons);
    std::sort(mons.begin(), mons.end(), monster_pane_info::less_than_wrapper);

    // Count how many groups of monsters there are.
    unsigned int lines_needed = mons.size();
    for (unsigned int i = 1; i < mons.size(); i++)
        if (!monster_pane_info::less_than(mons[i-1], mons[i]))
            --lines_needed;

    bool full_info = true;
    if (lines_needed > (unsigned int) max_print)
    {
        full_info = false;

        // Use type names rather than full names ("small zombie" vs
        // "rat zombie") in order to take up fewer lines.
        for (unsigned int i = 0; i < mons.size(); i++)
            mons[i].m_fullname = false;

        std::sort(mons.begin(), mons.end(),
                  monster_pane_info::less_than_wrapper);

        lines_needed = mons.size();
        for (unsigned int i = 1; i < mons.size(); i++)
            if (!monster_pane_info::less_than(mons[i-1], mons[i], false))
                --lines_needed;
    }

#if BOTTOM_JUSTIFY_MONSTER_LIST
    const int skip_lines = std::max<int>(0, crawl_view.mlistsz.y-lines_needed);
#else
    const int skip_lines = 0;
#endif

    // Print the monsters!
    std::string blank; blank.resize(crawl_view.mlistsz.x, ' ');
    int i_mons = 0;
    for (int i_print = 0; i_print < max_print; ++i_print)
    {
        cgotoxy(1, 1 + i_print, GOTO_MLIST);
        // i_mons is incremented by _print_next_monster_desc
        if (i_print >= skip_lines && i_mons < (int) mons.size())
        {
             _print_next_monster_desc(mons, i_mons, full_info,
                        Options.mlist_targetting == MLIST_TARGET_ON ? i_print
                                                                    : -1);
        }
        else
            cprintf("%s", blank.c_str());
    }

    if (i_mons < (int) mons.size())
    {
        // Didn't get to all of them.
        cgotoxy(crawl_view.mlistsz.x - 4, crawl_view.mlistsz.y, GOTO_MLIST);
        cprintf(" ... ");
    }

    if (mons.empty())
        return (-1);

    return full_info;
}
#else
// FIXME: Implement this for Tiles!
int update_monster_pane()
{
    return (false);
}
#endif

const char* itosym1(int stat)
{
    return ( (stat >= 1) ? "+  " : ".  " );
}

const char* itosym3(int stat)
{
    return ( (stat >=  3) ? "+ + +" :
             (stat ==  2) ? "+ + ." :
             (stat ==  1) ? "+ . ." :
             (stat ==  0) ? ". . ." :
             (stat == -1) ? "x . ." :
             (stat == -2) ? "x x ." :
                            "x x x");
}

static const char *s_equip_slot_names[] =
{
    "Weapon", "Cloak",  "Helmet", "Gloves", "Boots",
    "Shield", "Armour", "Left Ring", "Right Ring", "Amulet",
};

const char *equip_slot_to_name(int equip)
{
    if (equip == EQ_RINGS || equip == EQ_LEFT_RING || equip == EQ_RIGHT_RING)
        return "Ring";

    if (equip == EQ_BOOTS
        && (you.species == SP_CENTAUR || you.species == SP_NAGA))
    {
        return "Barding";
    }

    if (equip < 0 || equip >= NUM_EQUIP)
        return "";

    return s_equip_slot_names[equip];
}

int equip_name_to_slot(const char *s)
{
    for (int i = 0; i < NUM_EQUIP; ++i)
        if (!stricmp(s_equip_slot_names[i], s))
            return i;

    return -1;
}

// Colour the string according to the level of an ability/resistance.
// Take maximum possible level into account.
static const char* _determine_colour_string( int level, int max_level )
{
    switch (level)
    {
    case 3:
    case 2:
        if (max_level > 1)
            return "<lightgreen>";
        // else fall-through
    case 1:
        return "<green>";
    case -2:
    case -3:
        if (max_level > 1)
            return "<lightred>";
        // else fall-through
    case -1:
        return "<red>";
    default:
        return "<lightgrey>";
    }
}

static std::string _status_mut_abilities(void);

// helper for print_overview_screen
static void _print_overview_screen_equip(column_composer& cols,
                                         std::vector<char>& equip_chars)
{
    const int e_order[] =
    {
        EQ_WEAPON, EQ_BODY_ARMOUR, EQ_SHIELD, EQ_HELMET, EQ_CLOAK,
        EQ_GLOVES, EQ_BOOTS, EQ_AMULET, EQ_RIGHT_RING, EQ_LEFT_RING
    };

    char buf[100];
    for (int i = 0; i < NUM_EQUIP; i++)
    {
        int eqslot = e_order[i];

        char slot_name_lwr[15];
        snprintf(slot_name_lwr, sizeof slot_name_lwr, "%s",
                 equip_slot_to_name(eqslot));
        strlwr(slot_name_lwr);

        char slot[15] = "";
        // uncomment (and change 42 to 33) to bring back slot names
        // snprintf(slot, sizeof slot, "%-7s: ", equip_slot_to_name(eqslot);

        if (you.equip[ e_order[i] ] != -1)
        {
            const int item_idx    = you.equip[e_order[i]];
            const item_def& item  = you.inv[item_idx];
            const bool not_melded = player_wearing_slot(e_order[i]);

            // Colour melded equipment dark grey.
            const char* colname   =
                not_melded ? colour_to_str(item.colour).c_str()
                           : "darkgrey";

            const char equip_char = index_to_letter(item_idx);

            snprintf(buf, sizeof buf,
                     "%s<w>%c</w> - <%s>%s%s</%s>",
                     slot,
                     equip_char,
                     colname,
                     not_melded ? "" : "melded ",
                     item.name(DESC_PLAIN, true).substr(0,42).c_str(),
                     colname);
            equip_chars.push_back(equip_char);
        }
        else if (e_order[i] == EQ_WEAPON
                 && you.skills[SK_UNARMED_COMBAT])
        {
            snprintf(buf, sizeof buf, "%s  - Unarmed", slot);
        }
        else if (e_order[i] == EQ_WEAPON
                 && you.attribute[ATTR_TRANSFORMATION] == TRAN_BLADE_HANDS)
        {
            snprintf(buf, sizeof buf, "%s  - Blade Hands", slot);
        }
        else if (e_order[i] == EQ_BOOTS
                 && (you.species == SP_NAGA || you.species == SP_CENTAUR))
        {
            snprintf(buf, sizeof buf,
                     "<darkgrey>(no %s)</darkgrey>", slot_name_lwr);
        }
        else if (!you_can_wear(e_order[i], true))
        {
            snprintf(buf, sizeof buf,
                     "<darkgrey>(%s unavailable)</darkgrey>", slot_name_lwr);
        }
        else if (!you_tran_can_wear(e_order[i], true))
        {
            snprintf(buf, sizeof buf,
                     "<darkgrey>(%s currently unavailable)</darkgrey>",
                     slot_name_lwr);
        }
        else if (!you_can_wear(e_order[i]))
        {
            snprintf(buf, sizeof buf,
                     "<lightgrey>(%s restricted)</lightgrey>", slot_name_lwr);
        }
        else
        {
            snprintf(buf, sizeof buf,
                     "<darkgrey>(no %s)</darkgrey>", slot_name_lwr);
        }
        cols.add_formatted(2, buf, false);
    }
}

static std::string _overview_screen_title()
{
    char title[50];
    snprintf(title, sizeof title, " the %s ", player_title().c_str());

    char race_class[50];
    snprintf(race_class, sizeof race_class,
             "(%s %s)",
             species_name(you.species, you.experience_level).c_str(),
             you.class_name);

    char time_turns[50] = "";

    if (you.real_time != -1)
    {
        const time_t curr = you.real_time + (time(NULL) - you.start_time);
        snprintf(time_turns, sizeof time_turns,
                 " Turns: %ld, Time: %s",
                 you.num_turns, make_time_string(curr, true).c_str() );
    }

    int linelength = strlen(you.your_name) + strlen(title)
                     + strlen(race_class) + strlen(time_turns);
    for (int count = 0; linelength >= get_number_of_cols() && count < 2;
         count++)
    {
        switch (count)
        {
          case 0:
              snprintf(race_class, sizeof race_class,
                       "(%s%s)",
                       get_species_abbrev(you.species),
                       get_class_abbrev(you.char_class) );
              break;
          case 1:
              strcpy(title, "");
              break;
          default:
              break;
        }
        linelength = strlen(you.your_name) + strlen(title)
                     + strlen(race_class) + strlen(time_turns);
    }

    std::string text;
    text = "<yellow>";
    text += you.your_name;
    text += title;
    text += race_class;
    text += std::string(get_number_of_cols() - linelength - 1, ' ');
    text += time_turns;
    text += "</yellow>\n";

    return text;
}

static std::vector<formatted_string> _get_overview_stats()
{
    char buf[1000];

    // 4 columns
    column_composer cols1(4, 18, 28, 40);

    if (!player_rotted())
        snprintf(buf, sizeof buf, "HP %3d/%d", you.hp, you.hp_max);
    else
    {
        snprintf(buf, sizeof buf, "HP %3d/%d (%d)",
                 you.hp, you.hp_max, get_real_hp(true, true) );
    }
    cols1.add_formatted(0, buf, false);

    snprintf(buf, sizeof buf, "MP %3d/%d",
             you.magic_points, you.max_magic_points);
    cols1.add_formatted(0, buf, false);

    snprintf(buf, sizeof buf, "Gold %d", you.gold);
    cols1.add_formatted(0, buf, false);

    snprintf(buf, sizeof buf, "AC %2d" , player_AC());
    cols1.add_formatted(1, buf, false);

    snprintf(buf, sizeof buf, "EV %2d" , player_evasion());
    cols1.add_formatted(1, buf, false);

    snprintf(buf, sizeof buf, "SH %2d", player_shield_class());
    cols1.add_formatted(1, buf, false);

    if (you.strength == you.max_strength)
        snprintf(buf, sizeof buf, "Str %2d", you.strength);
    else
    {
        snprintf(buf, sizeof buf, "Str <yellow>%2d</yellow> (%d)",
                 you.strength, you.max_strength);
    }
    cols1.add_formatted(2, buf, false);

    if (you.intel == you.max_intel)
        snprintf(buf, sizeof buf, "Int %2d", you.intel);
    else
    {
        snprintf(buf, sizeof buf, "Int <yellow>%2d</yellow> (%d)",
                 you.intel, you.max_intel);
    }
    cols1.add_formatted(2, buf, false);

    if (you.dex == you.max_dex)
        snprintf(buf, sizeof buf, "Dex %2d", you.dex);
    else
    {
        snprintf(buf, sizeof buf, "Dex <yellow>%2d</yellow> (%d)",
                 you.dex, you.max_dex);
    }
    cols1.add_formatted(2, buf, false);

    char god_colour_tag[20];
    god_colour_tag[0] = 0;
    std::string godpowers(god_name(you.religion));
    if (you.religion != GOD_NO_GOD)
    {
        if (player_under_penance())
            strcpy(god_colour_tag, "<red>*");
        else
        {
            snprintf(god_colour_tag, sizeof god_colour_tag, "<%s>",
                     colour_to_str(god_colour(you.religion)).c_str());
            // piety rankings
            int prank = piety_rank() - 1;
            if (prank < 0 || you.religion == GOD_XOM)
                prank = 0;

            // Careful about overflow. We erase some of the god's name
            // if necessary.
            godpowers = godpowers.substr(0, 29 - prank)
                         + " " + std::string(prank, '*');
        }
    }

    int xp_needed = (exp_needed(you.experience_level + 2) - you.experience) + 1;
    snprintf(buf, sizeof buf,
             "Exp: %d/%lu (%d)%s\n"
             "God: %s%s<lightgrey>\n"
             "Spells: %2d memorised, %2d level%s left\n",
             you.experience_level, you.experience, you.exp_available,
             (you.experience_level < 27?
              make_stringf(", need: %d", xp_needed).c_str() : ""),
             god_colour_tag, godpowers.c_str(),
             you.spell_no, player_spell_levels(),
             (player_spell_levels() == 1) ? "" : "s");
    cols1.add_formatted(3, buf, false);

    return cols1.formatted_lines();
}

static std::vector<formatted_string> _get_overview_resistances(
    std::vector<char> &equip_chars,
    bool calc_unid = false)
{
    char buf[1000];

    // 3 columns, splits at columns 21, 38
    column_composer cols(3, 21, 38);

    const int rfire = player_res_fire(calc_unid);
    const int rcold = player_res_cold(calc_unid);
    const int rlife = player_prot_life(calc_unid);
    const int rpois = player_res_poison(calc_unid);
    const int relec = player_res_electricity(calc_unid);
    const int rsust = player_sust_abil(calc_unid);
    const int rmuta = (wearing_amulet(AMU_RESIST_MUTATION, calc_unid)
                       || player_mutation_level(MUT_MUTATION_RESISTANCE) == 3
                       || you.religion == GOD_ZIN && you.piety >= 150);

    const int rslow = wearing_amulet(AMU_RESIST_SLOW, calc_unid);

    snprintf(buf, sizeof buf,
             "%sRes.Fire  : %s\n"
             "%sRes.Cold  : %s\n"
             "%sLife Prot.: %s\n"
             "%sRes.Poison: %s\n"
             "%sRes.Elec. : %s\n"
             "\n"
             "%sSust.Abil.: %s\n"
             "%sRes.Mut.  : %s\n"
             "%sRes.Slow  : %s\n",
             _determine_colour_string(rfire, 3), itosym3(rfire),
             _determine_colour_string(rcold, 3), itosym3(rcold),
             _determine_colour_string(rlife, 3), itosym3(rlife),
             _determine_colour_string(rpois, 1), itosym1(rpois),
             _determine_colour_string(relec, 1), itosym1(relec),
             _determine_colour_string(rsust, 1), itosym1(rsust),
             _determine_colour_string(rmuta, 1), itosym1(rmuta),
             _determine_colour_string(rslow, 1), itosym1(rslow));
    cols.add_formatted(0, buf, false);

    int saplevel = player_mutation_level(MUT_SAPROVOROUS);
    const char* pregourmand;
    const char* postgourmand;
    if ( wearing_amulet(AMU_THE_GOURMAND, calc_unid) )
    {
        pregourmand = "Gourmand  : ";
        postgourmand = itosym1(1);
        saplevel = 1;
    }
    else
    {
        pregourmand = "Saprovore : ";
        postgourmand = itosym3(saplevel);
    }
    snprintf(buf, sizeof buf, "%s%s%s",
             _determine_colour_string(saplevel, 3), pregourmand, postgourmand);
    cols.add_formatted(0, buf, false);


    const int rinvi = player_see_invis(calc_unid);
    const int rward = (wearing_amulet(AMU_WARDING, calc_unid)
                       || you.religion == GOD_VEHUMET && !player_under_penance()
                          && you.piety >= piety_breakpoint(2));
    const int rcons = wearing_amulet(AMU_CONSERVATION, calc_unid);
    const int rcorr = wearing_amulet(AMU_RESIST_CORROSION, calc_unid);
    const int rclar = wearing_amulet(AMU_CLARITY, calc_unid);
    snprintf(buf, sizeof buf,
             "%sSee Invis. : %s\n"
             "%sWarding    : %s\n"
             "%sConserve   : %s\n"
             "%sRes.Corr.  : %s\n"
             "%sClarity    : %s\n"
             "\n",
             _determine_colour_string(rinvi, 1), itosym1(rinvi),
             _determine_colour_string(rward, 1), itosym1(rward),
             _determine_colour_string(rcons, 1), itosym1(rcons),
             _determine_colour_string(rcorr, 1), itosym1(rcorr),
             _determine_colour_string(rclar, 1), itosym1(rclar));
    cols.add_formatted(1, buf, false);

    if ( scan_randarts(RAP_PREVENT_TELEPORTATION, calc_unid) )
    {
        snprintf(buf, sizeof buf, "\n%sPrev.Telep.: %s",
                 _determine_colour_string(-1, 1), itosym1(1));
    }
    else
    {
        const int rrtel = !!player_teleport(calc_unid);
        snprintf(buf, sizeof buf, "\n%sRnd.Telep. : %s",
                 _determine_colour_string(rrtel, 1), itosym1(rrtel));
    }
    cols.add_formatted(1, buf, false);

    const int rctel = player_control_teleport(calc_unid);
    const int rlevi = player_is_airborne();
    const int rcfli = wearing_amulet(AMU_CONTROLLED_FLIGHT, calc_unid);
    snprintf(buf, sizeof buf,
             "%sCtrl.Telep.: %s\n"
             "%sLevitation : %s\n"
             "%sCtrl.Flight: %s\n",
             _determine_colour_string(rctel, 1), itosym1(rctel),
             _determine_colour_string(rlevi, 1), itosym1(rlevi),
             _determine_colour_string(rcfli, 1), itosym1(rcfli));
    cols.add_formatted(1, buf, false);

    _print_overview_screen_equip(cols, equip_chars);

    return cols.formatted_lines();
}

// New scrollable status overview screen, including stats, mutations etc.
char _get_overview_screen_results()
{
    bool calc_unid = false;
    formatted_scroller overview;
    // Set flags, and don't use easy exit.
    overview.set_flags(MF_SINGLESELECT | MF_ALWAYS_SHOW_MORE | MF_NOWRAP, false);
    overview.set_more( formatted_string::parse_string(
                       "<cyan>[ + : Page down.   - : Page up.   Esc exits.]"));
    overview.set_tag("resists");

    overview.add_text(_overview_screen_title());

    {
        std::vector<formatted_string> blines = _get_overview_stats();
        for (unsigned int i = 0; i < blines.size(); ++i )
            overview.add_item_formatted_string(blines[i]);
        overview.add_text(" ");
    }


    {
        std::vector<char> equip_chars;
        std::vector<formatted_string> blines =
            _get_overview_resistances(equip_chars, calc_unid);

        for (unsigned int i = 0; i < blines.size(); ++i )
        {
            // Kind of a hack -- we don't really care what items these
            // hotkeys go to.  So just pick the first few.
            const char hotkey = (i < equip_chars.size()) ? equip_chars[i] : 0;
            overview.add_item_formatted_string(blines[i], hotkey);
        }
    }

    overview.add_text(" ");
    overview.add_text(_status_mut_abilities());

    std::vector<MenuEntry *> results = overview.show();
    return (results.size() > 0) ? results[0]->hotkeys[0] : 0;
}

std::string dump_overview_screen(bool full_id)
{
    std::string text = formatted_string::parse_string(_overview_screen_title());
    text += EOL;

    std::vector<formatted_string> blines = _get_overview_stats();
    for (unsigned int i = 0; i < blines.size(); ++i)
    {
        text += blines[i];
        text += EOL;
    }
    text += EOL;

    std::vector<char> equip_chars;
    blines = _get_overview_resistances(equip_chars, full_id);
    for (unsigned int i = 0; i < blines.size(); ++i)
    {
        text += blines[i];
        text += EOL;
    }
    text += EOL;

    text += formatted_string::parse_string(_status_mut_abilities());
    text += EOL;

    return text;
}

void print_overview_screen()
{
    while (true)
    {
        char c = _get_overview_screen_results();
        if (!c)
        {
            redraw_screen();
            break;
        }

        item_def& item = you.inv[letter_to_index(c)];
        describe_item(item, true);
        // loop around for another go.
    }
}

std::string _get_expiration_string(duration_type dur, const char* msg)
{
    std::string help = msg;
    if (dur_expiring(dur))
        help += " (expiring)";

    return (help);
}

// Creates rows of short descriptions for current
// status, mutations and abilities.
std::string _status_mut_abilities()
{
    //----------------------------
    // print status information
    //----------------------------
    std::string text = "<w>@:</w> ";
    std::vector<std::string> status;

    // These are not so unreasonable anymore now that the new overview screen
    // is dumped. When the player dies while paralysed it's important
    // information. If so, move them to the front. (jpeg)
    if (you.paralysed())
        status.push_back("paralysed");

    if (you.duration[DUR_PETRIFIED])
        status.push_back("petrified");

    if (you.duration[DUR_SLEEP])
        status.push_back("sleeping");

    if (you.burden_state == BS_ENCUMBERED)
        status.push_back("burdened");
    else if (you.burden_state == BS_OVERLOADED)
        status.push_back("overloaded");

    if (you.duration[DUR_BREATH_WEAPON])
        status.push_back("short of breath");

    if (you.duration[DUR_REPEL_UNDEAD])
    {
        status.push_back(_get_expiration_string(DUR_REPEL_UNDEAD,
                                                "repel undead"));
    }

    // TODO: Differentiate between mermaids and sirens!
    if (you.duration[DUR_MESMERISED])
        status.push_back("mesmerised");

    if (you.duration[DUR_LIQUID_FLAMES])
        status.push_back("liquid flames");

    if (you.duration[DUR_ICY_ARMOUR])
        status.push_back(_get_expiration_string(DUR_ICY_ARMOUR, "icy armour"));

    if (you.duration[DUR_DEFLECT_MISSILES])
    {
        status.push_back(_get_expiration_string(DUR_DEFLECT_MISSILES,
                                                "deflect missiles"));
    }
    else if (you.duration[DUR_REPEL_MISSILES])
    {
        status.push_back(_get_expiration_string(DUR_REPEL_MISSILES,
                                                "repel missiles"));
    }

    if (you.duration[DUR_PRAYER])
        status.push_back("praying");

    if (you.disease && !you.duration[DUR_REGENERATION]
        || you.species == SP_VAMPIRE && you.hunger_state == HS_STARVING
        || you.species == SP_DEEP_DWARF)
    {
        status.push_back("non-regenerating");
    }
    else if (you.duration[DUR_REGENERATION]
             || you.species == SP_VAMPIRE && you.hunger_state != HS_SATIATED)
    {
        std::string help;
        if (you.disease)
            help = "recuperating";
        else
            help = "regenerating";

        if (you.species == SP_VAMPIRE && you.hunger_state != HS_SATIATED)
        {
            if (you.hunger_state < HS_SATIATED)
                help += " slowly";
            else if (you.hunger_state >= HS_FULL)
                help += " quickly";
            else if (you.hunger_state == HS_ENGORGED)
                help += " very quickly";
        }

        if (dur_expiring(DUR_REGENERATION))
            help += " (expires)";

        status.push_back(help);
    }

    if (you.duration[DUR_STONEMAIL])
    {
        status.push_back(_get_expiration_string(DUR_STONEMAIL,
                                                "stone mail"));
    }

    if (you.duration[DUR_STONESKIN])
        status.push_back("stone skin");

    if (you.duration[DUR_TELEPORT])
        status.push_back("about to teleport");

    if (you.duration[DUR_DEATH_CHANNEL])
    {
        status.push_back(_get_expiration_string(DUR_DEATH_CHANNEL,
                                                "death channel"));
    }
    if (you.duration[DUR_FORESCRY])
        status.push_back(_get_expiration_string(DUR_FORESCRY, "forewarned"));

    if (you.duration[DUR_SILENCE])
        status.push_back(_get_expiration_string(DUR_SILENCE, "silence"));

    if (you.duration[DUR_INVIS])
        status.push_back(_get_expiration_string(DUR_INVIS, "invisible"));

    if (you.confused())
        status.push_back("confused");

    if (you.duration[DUR_EXHAUSTED])
        status.push_back("exhausted");

    if (you.duration[DUR_MIGHT])
        status.push_back("mighty");

    if (you.duration[DUR_DIVINE_VIGOUR])
        status.push_back("divinely robust");

    if (you.duration[DUR_DIVINE_STAMINA])
        status.push_back("divinely fortified");

    if (you.duration[DUR_BERSERKER])
        status.push_back("berserking");

    if (player_is_airborne())
    {
        std::string help;
        if (wearing_amulet( AMU_CONTROLLED_FLIGHT ))
            help += "flying";
        else
            help += "levitating";

        if (dur_expiring(DUR_LEVITATION) && !you.permanent_flight())
            help += " (expires)";

        status.push_back(help);
    }

    if (you.duration[DUR_BARGAIN])
        status.push_back(_get_expiration_string(DUR_BARGAIN, "charismatic"));

    if (you.duration[DUR_SLAYING])
        status.push_back("deadly");

    // DUR_STEALTHY handled in stealth printout

    if (you.duration[DUR_SAGE])
    {
        std::string help  = "studying ";
                    help += skill_name(you.sage_bonus_skill);
        status.push_back(_get_expiration_string(DUR_SAGE, help.c_str()));
    }

    if (you.duration[DUR_MAGIC_SHIELD])
        status.push_back("shielded");

    if (you.duration[DUR_FIRE_SHIELD])
    {
        status.push_back(_get_expiration_string(DUR_FIRE_SHIELD,
                                                "immune to fire clouds"));
    }

    if (you.duration[DUR_POISONING])
    {
        std::string help = (you.duration[DUR_POISONING] > 10) ? "extremely" :
                           (you.duration[DUR_POISONING] >  5) ? "very" :
                           (you.duration[DUR_POISONING] >  3) ? "quite"
                                                              : "mildly";
                    help += " poisoned";

        status.push_back(help);
    }

    if (you.disease)
    {
        std::string help = (you.disease > 120) ? "badly " :
                           (you.disease >  40) ? ""
                                               : "mildly ";
                    help += "diseased";

        status.push_back(help);
    }

    if (you.rotting || you.species == SP_GHOUL)
        status.push_back("rotting");

    if (you.duration[DUR_CONFUSING_TOUCH])
    {
        status.push_back(_get_expiration_string(DUR_CONFUSING_TOUCH,
                                                "confusing touch"));
    }

    if (you.duration[DUR_SURE_BLADE])
        status.push_back("bonded with blade");

    int move_cost = (player_speed() * player_movement_speed()) / 10;

    if (move_cost != 10)
    {
        std::string help = (move_cost <   8) ? "very quick" :
                           (move_cost <  10) ? "quick" :
                           (move_cost <  13) ? "slow"
                                             : "";
        if (!help.empty())
            status.push_back(help);
    }

    if (you.duration[DUR_SLOW] && !you.duration[DUR_HASTE])
        status.push_back("slowed");
    else if (you.duration[DUR_HASTE] && !you.duration[DUR_SLOW])
        status.push_back(_get_expiration_string(DUR_HASTE, "hasted"));
    else if (!you.duration[DUR_HASTE] && you.duration[DUR_SWIFTNESS])
        status.push_back("swift");

    if (you.attribute[ATTR_HELD])
        status.push_back("held");

    const int mr = player_res_magic();
    snprintf(info, INFO_SIZE, "%s resistant to magic",
             (mr <  10) ? "not" :
             (mr <  30) ? "slightly" :
             (mr <  60) ? "somewhat" :
             (mr <  90) ? "quite" :
             (mr < 120) ? "very" :
             (mr < 140) ? "extremely"
                        : "incredibly");

    status.push_back(info);

    // character evaluates their ability to sneak around:
    const int ustealth = check_stealth();

    snprintf( info, INFO_SIZE, "%sstealthy",
         (ustealth <  10) ? "extremely un" :
         (ustealth <  30) ? "very un" :
         (ustealth <  60) ? "un" :
         (ustealth <  90) ? "fairly " :
         (ustealth < 120) ? "" :
         (ustealth < 160) ? "quite " :
         (ustealth < 220) ? "very " :
         (ustealth < 300) ? "extremely " :
         (ustealth < 400) ? "extraordinarily " :
         (ustealth < 520) ? "incredibly "
                          : "uncannily ");

    status.push_back(info);

    text += comma_separated_line(status.begin(), status.end(), ", ", ", ");

    if (you.duration[DUR_TRANSFORMATION])
    {
        switch (you.attribute[ATTR_TRANSFORMATION])
        {
        case TRAN_SPIDER:
            text += "\nYou are in spider-form.";
            break;
        case TRAN_BAT:
            text += "\nYou are in ";
            if (you.species == SP_VAMPIRE)
                text += "vampire ";
            text += "bat-form.";
            break;
        case TRAN_BLADE_HANDS:
            text += "\nYou have blades for hands.";
            break;
        case TRAN_STATUE:
            text += "\nYou are a statue.";
            break;
        case TRAN_ICE_BEAST:
            text += "\nYou are an ice creature.";
            break;
        case TRAN_DRAGON:
            text += "\nYou are in dragon-form.";
            break;
        case TRAN_LICH:
            text += "\nYou are in lich-form.";
            break;
        case TRAN_SERPENT_OF_HELL:
            text += "\nYou are a huge demonic serpent.";
            break;
        case TRAN_AIR:
            text += "\nYou are a cloud of diffuse gas.";
            break;
        }
        if ((you.species != SP_VAMPIRE
                || you.attribute[ATTR_TRANSFORMATION] != TRAN_BAT)
            && dur_expiring(DUR_TRANSFORMATION))
        {
            text += " (Expiring.)";
        }
    }
/*
//  Commenting out until this information is actually meaningful. (jpeg)
    const int to_hit = calc_your_to_hit( false ) * 2;

    snprintf( info, INFO_SIZE,
          "\n%s in your current equipment.",
          (to_hit <   1) ? "You are completely incapable of fighting" :
          (to_hit <   5) ? "Hitting even clumsy monsters is extremely awkward" :
          (to_hit <  10) ? "Hitting average monsters is awkward" :
          (to_hit <  15) ? "Hitting average monsters is difficult" :
          (to_hit <  20) ? "Hitting average monsters is hard" :
          (to_hit <  30) ? "Very agile monsters are a bit awkward to hit" :
          (to_hit <  45) ? "Very agile monsters are a bit difficult to hit" :
          (to_hit <  60) ? "Very agile monsters are a bit hard to hit" :
          (to_hit < 100) ? "You feel comfortable with your ability to fight"
                         : "You feel confident with your ability to fight" );
    text += info;
*/
    if (you.duration[DUR_DEATHS_DOOR])
        text += "\nYou are standing in death's doorway.";

    //----------------------------
    // print mutation information
    //----------------------------
    text += "\n<w>A:</w> ";

    int AC_change  = 0;
    int EV_change  = 0;
    int Str_change = 0;
    int Int_change = 0;
    int Dex_change = 0;

    std::vector<std::string> mutations;

    switch (you.species)   //mv: following code shows innate abilities - if any
    {
      case SP_MERFOLK:
          mutations.push_back("change form in water");
          break;

      case SP_NAGA:
          // breathe poison replaces spit poison:
          if (!player_mutation_level(MUT_BREATHE_POISON))
              mutations.push_back("spit poison");
          else
              mutations.push_back("breathe poison");
          break;

      case SP_KENKU:
          if (you.experience_level > 4)
          {
              std::string help = "able to fly";
              if (you.experience_level > 14)
                  help += " continuously";
              mutations.push_back(help);
          }
          break;

      case SP_VAMPIRE:
          if (you.experience_level < 13 || you.hunger_state >= HS_SATIATED)
              break;
          // else fall-through
      case SP_MUMMY:
          mutations.push_back("in touch with death");
          break;

      case SP_GREY_DRACONIAN:
          if (you.experience_level > 6)
              mutations.push_back("spiky tail");
          break;

      case SP_GREEN_DRACONIAN:
          if (you.experience_level > 6)
              mutations.push_back("breathe poison");
          break;

      case SP_RED_DRACONIAN:
          if (you.experience_level > 6)
              mutations.push_back("breathe fire");
          break;

      case SP_WHITE_DRACONIAN:
          if (you.experience_level > 6)
              mutations.push_back("breathe frost");
          break;

      case SP_BLACK_DRACONIAN:
          if (you.experience_level > 6)
              mutations.push_back("breathe lightning");
          break;

      case SP_YELLOW_DRACONIAN:
          if (you.experience_level > 6)
          {
              mutations.push_back("spit acid");
              mutations.push_back("acid resistance");
          }
          break;

      case SP_PURPLE_DRACONIAN:
          if (you.experience_level > 6)
              mutations.push_back("breathe power");
          break;

      case SP_MOTTLED_DRACONIAN:
          if (you.experience_level > 6)
              mutations.push_back("breathe sticky flames");
          break;

      case SP_PALE_DRACONIAN:
          if (you.experience_level > 6)
              mutations.push_back("breathe steam");
          break;

      default:
          break;
    }                           //end switch - innate abilities

    // a bit more stuff
    if (you.species == SP_OGRE || you.species == SP_TROLL
        || player_genus(GENPC_DRACONIAN) || you.species == SP_SPRIGGAN)
    {
        mutations.push_back("unfitting armour");
    }

    if (beogh_water_walk())
        mutations.push_back("water walking");

    std::string current;
    for (unsigned i = 0; i < NUM_MUTATIONS; i++)
    {
        int level = player_mutation_level((mutation_type) i);
        if (!level)
            continue;

        current = "";
        bool lowered = (level < you.mutation[i]);
        switch (i)
        {
            case MUT_TOUGH_SKIN:
                AC_change += level;
                break;
            case MUT_STRONG:
                Str_change += level;
                break;
            case MUT_CLEVER:
                Int_change += level;
                break;
            case MUT_AGILE:
                Dex_change += level;
                break;
            case MUT_GREEN_SCALES:
                AC_change += 2*level-1;
                break;
            case MUT_BLACK_SCALES:
                AC_change += 3*level;
                Dex_change -= level;
                break;
            case MUT_GREY_SCALES:
                AC_change += level;
                break;
            case MUT_BONEY_PLATES:
                AC_change += level+1;
                Dex_change -= level;
                break;
            case MUT_REPULSION_FIELD:
                EV_change += 2*level-1;
                if (level == 3)
                    current = "repel missiles";
                break;
            case MUT_POISON_RESISTANCE:
                current = "poison resistance";
                break;
            case MUT_SAPROVOROUS:
                snprintf(info, INFO_SIZE, "saprovore %d", level);
                current = info;
                break;
            case MUT_CARNIVOROUS:
                snprintf(info, INFO_SIZE, "carnivore %d", level);
                current = info;
                break;
            case MUT_HERBIVOROUS:
                snprintf(info, INFO_SIZE, "herbivore %d", level);
                current = info;
                break;
            case MUT_HEAT_RESISTANCE:
                snprintf(info, INFO_SIZE, "fire resistance %d", level);
                current = info;
                break;
            case MUT_COLD_RESISTANCE:
                snprintf(info, INFO_SIZE, "cold resistance %d", level);
                current = info;
                break;
            case MUT_SHOCK_RESISTANCE:
                current = "electricity resistance";
                break;
            case MUT_REGENERATION:
                snprintf(info, INFO_SIZE, "regeneration %d", level);
                current = info;
                break;
            case MUT_FAST_METABOLISM:
                snprintf(info, INFO_SIZE, "fast metabolism %d", level);
                current = info;
                break;
            case MUT_SLOW_METABOLISM:
                snprintf(info, INFO_SIZE, "slow metabolism %d", level);
                current = info;
                break;
            case MUT_WEAK:
                Str_change -= level;
                break;
            case MUT_DOPEY:
                Int_change -= level;
                break;
            case MUT_CLUMSY:
                Dex_change -= level;
                break;
            case MUT_TELEPORT_CONTROL:
                current = "teleport control";
                break;
            case MUT_TELEPORT:
                snprintf(info, INFO_SIZE, "teleportitis %d", level);
                current = info;
                break;
            case MUT_MAGIC_RESISTANCE:
                snprintf(info, INFO_SIZE, "magic resistance %d", level);
                current = info;
                break;
            case MUT_FAST:
                snprintf(info, INFO_SIZE, "speed %d", level);
                current = info;
                break;
            case MUT_ACUTE_VISION:
                current = "see invisible";
                break;
            case MUT_DEFORMED:
                snprintf(info, INFO_SIZE, "deformed body %d", level);
                current = info;
                break;
            case MUT_TELEPORT_AT_WILL:
                snprintf(info, INFO_SIZE, "teleport at will %d", level);
                current = info;
                break;
            case MUT_SPIT_POISON:
                current = "spit poison";
                break;
            case MUT_MAPPING:
                snprintf(info, INFO_SIZE, "sense surroundings %d", level);
                current = info;
                break;
            case MUT_BREATHE_FLAMES:
                snprintf(info, INFO_SIZE, "breathe flames %d", level);
                current = info;
                break;
            case MUT_BLINK:
                current = "blink";
                break;
            case MUT_HORNS:
                snprintf(info, INFO_SIZE, "horns %d", level);
                current = info;
                break;
            case MUT_BEAK:
                current = "beak";
                break;
            case MUT_STRONG_STIFF:
                Str_change += level;
                Dex_change -= level;
                break;
            case MUT_FLEXIBLE_WEAK:
                Str_change -= level;
                Dex_change += level;
                break;
            case MUT_SCREAM:
                snprintf(info, INFO_SIZE, "screaming %d", level);
                current = info;
                break;
            case MUT_CLARITY:
                snprintf(info, INFO_SIZE, "clarity %d", level);
                current = info;
                break;
            case MUT_BERSERK:
                snprintf(info, INFO_SIZE, "berserk %d", level);
                current = info;
                break;
            case MUT_DETERIORATION:
                snprintf(info, INFO_SIZE, "deterioration %d", level);
                current = info;
                break;
            case MUT_BLURRY_VISION:
                snprintf(info, INFO_SIZE, "blurry vision %d", level);
                current = info;
                break;
            case MUT_MUTATION_RESISTANCE:
                snprintf(info, INFO_SIZE, "mutation resistance %d", level);
                current = info;
                break;
            case MUT_FRAIL:
                snprintf(info, INFO_SIZE, "-%d%% hp", level*10);
                current = info;
                break;
            case MUT_ROBUST:
                snprintf(info, INFO_SIZE, "+%d%% hp", level*10);
                current = info;
                break;
            case MUT_LOW_MAGIC:
                snprintf(info, INFO_SIZE, "-%d%% mp", level*10);
                current = info;
                break;
            case MUT_HIGH_MAGIC:
                snprintf(info, INFO_SIZE, "+%d%% mp", level*10);
                current = info;
                break;

            // demonspawn mutations
            case MUT_TORMENT_RESISTANCE:
                current = "torment resistance";
                break;
            case MUT_NEGATIVE_ENERGY_RESISTANCE:
                snprintf(info, INFO_SIZE, "life protection %d", level);
                current = info;
                break;
            case MUT_SUMMON_MINOR_DEMONS:
                current = "summon minor demons";
                break;
            case MUT_SUMMON_DEMONS:
                current = "summon demons";
                break;
            case MUT_HURL_HELLFIRE:
                current = "hurl hellfire";
                break;
            case MUT_CALL_TORMENT:
                current = "call torment";
                break;
            case MUT_RAISE_DEAD:
                current = "raise dead";
                break;
            case MUT_CONTROL_DEMONS:
                current = "control demons";
                break;
            case MUT_PANDEMONIUM:
                current = "portal to Pandemonium";
                break;
            case MUT_DEATH_STRENGTH:
                current = "draw strength from death and destruction";
                break;
            case MUT_CHANNEL_HELL:
                current = "channel magical energy from Hell";
                break;
            case MUT_DRAIN_LIFE:
                current = "drain life";
                break;
            case MUT_THROW_FLAMES:
                current = "throw flames of Gehenna";
                break;
            case MUT_THROW_FROST:
                current = "throw frost of Cocytus";
                break;
            case MUT_SMITE:
                current = "invoke powers of Tartarus";
                break;
            // end of demonspawn mutations

            case MUT_CLAWS:
                snprintf(info, INFO_SIZE, "claws %d", level);
                current = info;
                break;
            case MUT_FANGS:
                snprintf(info, INFO_SIZE, "sharp teeth %d", level);
                current = info;
                break;
            case MUT_HOOVES:
                current = "hooves";
                break;
            case MUT_TALONS:
                current = "talons";
                break;
            case MUT_BREATHE_POISON:
                current = "breathe poison";
                break;
            case MUT_STINGER:
                snprintf(info, INFO_SIZE, "stinger %d", level);
                current = info;
                break;
            case MUT_BIG_WINGS:
                current = "large and strong wings";
                break;
            case MUT_BLUE_MARKS:
                snprintf(info, INFO_SIZE, "blue evil mark %d", level);
                current = info;
                break;
            case MUT_GREEN_MARKS:
                snprintf(info, INFO_SIZE, "green evil mark %d", level);
                current = info;
                break;

            // scales etc. -> calculate sum of AC bonus
            case MUT_RED_SCALES:
                AC_change += level;
                if (level == 3)
                    AC_change++;
                break;
            case MUT_NACREOUS_SCALES:
                AC_change += 2*level-1;
                break;
            case MUT_GREY2_SCALES:
                AC_change += 2*level;
                Dex_change -= 1;
                if (level == 3)
                    Dex_change--;
                break;
            case MUT_METALLIC_SCALES:
                AC_change += 3*level+1;
                if (level == 1)
                    AC_change--;
                Dex_change -= level + 1;
                break;
            case MUT_BLACK2_SCALES:
                AC_change += 2*level-1;
                break;
            case MUT_WHITE_SCALES:
                AC_change += 2*level-1;
                break;
            case MUT_YELLOW_SCALES:
                AC_change += 2*level;
                Dex_change -= level-1;
                break;
            case MUT_BROWN_SCALES:
                AC_change += 2*level;
                if (level == 3)
                    AC_change--;
                break;
            case MUT_BLUE_SCALES:
                AC_change += level;
                break;
            case MUT_PURPLE_SCALES:
                AC_change += 2*level;
                break;
            case MUT_SPECKLED_SCALES:
                AC_change += level;
                break;
            case MUT_ORANGE_SCALES:
                AC_change += level;
                if (level > 1)
                    AC_change++;
                break;
            case MUT_INDIGO_SCALES:
                AC_change += 2*level-1;
                if (level == 1)
                    AC_change++;
                break;
            case MUT_RED2_SCALES:
                AC_change += 2*level;
                if (level > 1)
                    AC_change++;
                Dex_change -= level - 1;
                break;
            case MUT_IRIDESCENT_SCALES:
                AC_change += level;
                break;
            case MUT_PATTERNED_SCALES:
                AC_change += level;
                break;
            case MUT_SHAGGY_FUR:
                AC_change += level;
                if (level == 3)
                    current = "shaggy fur";
                break;
            default: break;
        }

        if (!current.empty())
        {
            if (lowered)
                current = "<darkgrey>" + current + "</darkgrey>";
            mutations.push_back(current);
        }
    }

    if (AC_change)
    {
        snprintf(info, INFO_SIZE, "AC %s%d", (AC_change > 0 ? "+" : ""), AC_change);
        mutations.push_back(info);
    }
    if (EV_change)
    {
        snprintf(info, INFO_SIZE, "EV +%d", EV_change);
        mutations.push_back(info);
    }
    if (Str_change)
    {
        snprintf(info, INFO_SIZE, "Str %s%d", (Str_change > 0 ? "+" : ""), Str_change);
        mutations.push_back(info);
    }
    if (Int_change)
    {
        snprintf(info, INFO_SIZE, "Int %s%d", (Int_change > 0 ? "+" : ""), Int_change);
        mutations.push_back(info);
    }
    if (Dex_change)
    {
        snprintf(info, INFO_SIZE, "Dex %s%d", (Dex_change > 0 ? "+" : ""), Dex_change);
        mutations.push_back(info);
    }

    if (mutations.empty())
        text +=  "no striking features";
    else
    {
        text += comma_separated_line(mutations.begin(), mutations.end(),
                                     ", ", ", ");
    }

    //----------------------------
    // print ability information
    //----------------------------

    text += print_abilities();
    linebreak_string2(text, get_number_of_cols());

    return text;
}
