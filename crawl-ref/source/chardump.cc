/*
 *  File:       chardump.cc
 *  Summary:    Dumps character info out to the morgue file.
 *  Written by: Linley Henzell
 *
 *  Modified for Crawl Reference by $Author$ on $Date$
 */

#include "AppHdr.h"
REVISION("$Rev$");

#include "chardump.h"
#include "clua.h"

#include <string>
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <stdlib.h>
#if !defined(__IBMCPP__) && !defined(_MSC_VER)
#include <unistd.h>
#endif
#include <ctype.h>

#ifdef DOS
#include <conio.h>
#endif

#include "externs.h"

#include "debug.h"
#include "describe.h"
#include "hiscores.h"
#include "initfile.h"
#include "itemprop.h"
#include "items.h"
#include "Kills.h"
#include "macro.h"
#include "message.h"
#include "menu.h"
#include "mutation.h"
#include "notes.h"
#include "output.h"
#include "overmap.h"
#include "place.h"
#include "player.h"
#include "randart.h"
#include "religion.h"
#include "shopping.h"
#include "skills2.h"
#include "spl-book.h"
#include "spl-cast.h"
#include "spl-util.h"
#include "stash.h"
#include "stuff.h"
#include "transfor.h"
#include "version.h"
#include "view.h"

struct dump_params;

static void _sdump_header(dump_params &);
static void _sdump_stats(dump_params &);
static void _sdump_location(dump_params &);
static void _sdump_religion(dump_params &);
static void _sdump_burden(dump_params &);
static void _sdump_hunger(dump_params &);
static void _sdump_transform(dump_params &);
static void _sdump_visits(dump_params &);
static void _sdump_gold(dump_params &);
static void _sdump_misc(dump_params &);
static void _sdump_turns_by_place(dump_params &);
static void _sdump_notes(dump_params &);
static void _sdump_inventory(dump_params &);
static void _sdump_skills(dump_params &);
static void _sdump_spells(dump_params &);
static void _sdump_mutations(dump_params &);
static void _sdump_messages(dump_params &);
static void _sdump_screenshot(dump_params &);
static void _sdump_kills_by_place(dump_params &);
static void _sdump_kills(dump_params &);
static void _sdump_newline(dump_params &);
static void _sdump_overview(dump_params &);
static void _sdump_hiscore(dump_params &);
static void _sdump_monster_list(dump_params &);
static void _sdump_separator(dump_params &);
#ifdef CLUA_BINDINGS
static void _sdump_lua(dump_params &);
#endif
static bool write_dump(const std::string &fname, dump_params &);

struct dump_section_handler
{
    const char *name;
    void (*handler)(dump_params &);
};

struct dump_params
{
    std::string &text;
    std::string section;
    bool show_prices;
    bool full_id;
    const scorefile_entry *se;

    dump_params(std::string &_text, const std::string &sec = "",
                bool prices = false, bool id = false,
                const scorefile_entry *s = NULL)
        : text(_text), section(sec), show_prices(prices), full_id(id),
          se(s)
    {
    }
};

static dump_section_handler dump_handlers[] = {
    { "header",         _sdump_header        },
    { "stats",          _sdump_stats         },
    { "location",       _sdump_location      },
    { "religion",       _sdump_religion      },
    { "burden",         _sdump_burden        },
    { "hunger",         _sdump_hunger        },
    { "transform",      _sdump_transform     },
    { "visits",         _sdump_visits        },
    { "gold",           _sdump_gold          },
    { "misc",           _sdump_misc          },
    { "turns_by_place", _sdump_turns_by_place},
    { "notes",          _sdump_notes         },
    { "inventory",      _sdump_inventory     },
    { "skills",         _sdump_skills        },
    { "spells",         _sdump_spells        },
    { "mutations",      _sdump_mutations     },
    { "messages",       _sdump_messages      },
    { "screenshot",     _sdump_screenshot    },
    { "kills_by_place", _sdump_kills_by_place},
    { "kills",          _sdump_kills         },
    { "overview",       _sdump_overview      },
    { "hiscore",        _sdump_hiscore       },
    { "monlist",        _sdump_monster_list  },

    // Conveniences for the .crawlrc artist.
    { "",               _sdump_newline       },
    { "-",              _sdump_separator     },

#ifdef CLUA_BINDINGS
    { NULL,             _sdump_lua           }
#else
    { NULL,             NULL                }
#endif
};

static void dump_section(dump_params &par)
{
    for (int i = 0; ; ++i)
    {
        if (!dump_handlers[i].name || par.section == dump_handlers[i].name)
        {
            if (dump_handlers[i].handler)
                (*dump_handlers[i].handler)(par);
            break;
        }
    }
}

bool dump_char(const std::string &fname, bool show_prices, bool full_id,
               const scorefile_entry *se)
{
    // Start with enough room for 100 80 character lines.
    std::string text;
    text.reserve(100 * 80);

    dump_params par(text, "", show_prices, full_id, se);

    for (int i = 0, size = Options.dump_order.size(); i < size; ++i)
    {
        par.section = Options.dump_order[i];
        dump_section(par);
    }

    return write_dump(fname, par);
}

static void _sdump_header(dump_params &par)
{
    par.text += " " CRAWL " version " VERSION " character file.\n\n";
}

static void _sdump_stats(dump_params &par)
{
    par.text += dump_overview_screen(par.full_id);
    par.text += "\n\n";
}

static void _sdump_burden(dump_params &par)
{
    std::string verb = par.se? "were" : "are";

    switch (you.burden_state)
    {
    case BS_OVERLOADED:
        par.text += "You " + verb + " overloaded with stuff.\n";
        break;
    case BS_ENCUMBERED:
        par.text += "You " + verb + " encumbered.\n";
        break;
    default:
        break;
    }
}

static void _sdump_hunger(dump_params &par)
{
    if (par.se)
        par.text += "You were ";
    else
        par.text += "You are ";

    par.text += hunger_level();
    par.text += ".\n\n";
}

static void _sdump_transform(dump_params &par)
{
    std::string &text(par.text);
    if (you.attribute[ATTR_TRANSFORMATION])
    {
        std::string verb = par.se? "were" : "are";

        switch (you.attribute[ATTR_TRANSFORMATION])
        {
        case TRAN_SPIDER:
            text += "You " + verb + " in spider-form.";
            break;
        case TRAN_BAT:
            text += "You " + verb + " in ";
            if (you.species == SP_VAMPIRE)
                text += "vampire ";
            text += "bat-form.";
            break;
        case TRAN_BLADE_HANDS:
            text += "Your hands " + verb + " blades.";
            break;
        case TRAN_STATUE:
            text += "You " + verb + " a stone statue.";
            break;
        case TRAN_ICE_BEAST:
            text += "You " + verb + " a creature of crystalline ice.";
            break;
        case TRAN_DRAGON:
            text += "You " + verb + " a fearsome dragon!";
            break;
        case TRAN_LICH:
            text += "You " + verb + " in lich-form.";
            break;
        case TRAN_SERPENT_OF_HELL:
            text += "You " + verb + " a huge, demonic serpent!";
            break;
        case TRAN_AIR:
            text += "You " + verb + " a cloud of diffuse gas.";
            break;
        case TRAN_PIG:
            text += "You " + verb + " a filthy swine.";
            break;
        }

        text += "\n\n";
    }
}

static void _sdump_visits(dump_params &par)
{
    std::string &text(par.text);

    std::string have = "have ";
    std::string seen = "seen";
    if (par.se) // you died -> past tense
    {
        have = "";
        seen = "saw";
    }

    std::vector<PlaceInfo> branches_visited =
        you.get_all_place_info(true, true);

    PlaceInfo branches_total;
    for (unsigned int i = 0; i < branches_visited.size(); i++)
        branches_total += branches_visited[i];

    text += make_stringf("You %svisited %ld branch",
                         have.c_str(), branches_visited.size());
    if (branches_visited.size() > 1)
        text += "es";
    text += make_stringf(" of the dungeon, and %s %ld of its levels.\n",
                         seen.c_str(), branches_total.levels_seen);

    PlaceInfo place_info = you.get_place_info(LEVEL_PANDEMONIUM);
    if (place_info.num_visits > 0)
    {
        text += make_stringf("You %svisited Pandemonium %ld time",
                             have.c_str(), place_info.num_visits);
        if (place_info.num_visits > 1)
            text += "s";
        text += make_stringf(", and %s %ld of its levels.\n",
                             seen.c_str(), place_info.levels_seen);
    }

    place_info = you.get_place_info(LEVEL_ABYSS);
    if (place_info.num_visits > 0)
    {
        text += make_stringf("You %svisited the Abyss %ld time",
                             have.c_str(), place_info.num_visits);
        if (place_info.num_visits > 1)
            text += "s";
        text += ".\n";
    }

    place_info = you.get_place_info(LEVEL_LABYRINTH);
    if (place_info.num_visits > 0)
    {
        text += make_stringf("You %svisited %ld Labyrinth",
                             have.c_str(), place_info.num_visits);
        if (place_info.num_visits > 1)
            text += "s";
        text += ".\n";
    }

    place_info = you.get_place_info(LEVEL_PORTAL_VAULT);
    if (place_info.num_visits > 0)
    {
        text += make_stringf("You %svisited %ld portal chamber",
                             have.c_str(), place_info.num_visits);
        if (place_info.num_visits > 1)
            text += "s";
        text += " (including bazaars).\n";
    }

    text += "\n";
}

static void _sdump_gold(dump_params &par)
{
    std::string &text(par.text);

    int lines = 0;

    const char* have = "have ";
    if (par.se) // you died -> past tense
        have = "";

    if (you.attribute[ATTR_GOLD_FOUND] > 0)
    {
        lines++;
        text += make_stringf("You %scollected %d gold pieces.\n", have,
                             you.attribute[ATTR_GOLD_FOUND]);
    }

    if (you.attribute[ATTR_PURCHASES] > 0)
    {
        lines++;
        text += make_stringf("You %sspent %d gold pieces at shops.\n", have,
                             you.attribute[ATTR_PURCHASES]);
    }

    if (you.attribute[ATTR_DONATIONS] > 0)
    {
        lines++;
        text += make_stringf("You %sdonated %d gold pieces.\n", have,
                             you.attribute[ATTR_DONATIONS]);
    }

    if (you.attribute[ATTR_MISC_SPENDING] > 0)
    {
        lines++;
        text += make_stringf("You %sused %d gold pieces for miscellaneous "
                             "purposes.\n", have,
                             you.attribute[ATTR_MISC_SPENDING]);
    }

    if (lines > 0)
        text += "\n";
}

static void _sdump_misc(dump_params &par)
{
    _sdump_location(par);
    _sdump_religion(par);
    _sdump_burden(par);
    _sdump_hunger(par);
    _sdump_transform(par);
    _sdump_visits(par);
    _sdump_gold(par);
}

#define TO_PERCENT(x, y) (100.0f * ((float) (x)) / ((float) (y)))

static std::string _sdump_turns_place_info(PlaceInfo place_info,
                                           std::string name = "")
{
    PlaceInfo   gi = you.global_info;
    std::string out;

    if (name.empty())
        name = place_info.short_name();

    float a, b, c, d, e, f;
    unsigned int non_interlevel =
        place_info.turns_total - place_info.turns_interlevel;
    unsigned int global_non_interlevel =
        gi.turns_total - gi.turns_interlevel;


    a = TO_PERCENT(place_info.turns_total, gi.turns_total);
    b = TO_PERCENT(non_interlevel, global_non_interlevel);
    c = TO_PERCENT(place_info.turns_interlevel, place_info.turns_total);
    d = TO_PERCENT(place_info.turns_resting, non_interlevel);
    e = TO_PERCENT(place_info.turns_explore, non_interlevel);
    f = (float) non_interlevel / (float) place_info.levels_seen;

    out =
        make_stringf("%14s | %5.1f | %5.1f | %5.1f | %5.1f | %5.1f | %13.1f\n",
                     name.c_str(), a, b, c , d, e, f);

    out = replace_all(out, " nan ", " N/A ");

    return out;
}

static void _sdump_turns_by_place(dump_params &par)
{
    std::string &text(par.text);

    std::vector<PlaceInfo> all_visited =
        you.get_all_place_info(true);

    text +=
"Table legend:\n"
" A = Turns spent in this place as a percentage of turns spent in the\n"
"     entire game.\n"
" B = Non-inter-level travel turns spent in this place as a percentage of\n"
"     non-inter-level travel turns spent in the entire game.\n"
" C = Inter-level travel turns spent in this place as a percentage of\n"
"     turns spent in this place.\n"
" D = Turns resting spent in this place as a percentage of non-inter-level\n"
"     travel turns spent in this place.\n"
" E = Turns spent auto-exploring this place as a percentage of\n"
"     non-inter-level travel turns spent in this place.\n"
" F = Non-inter-level travel turns spent in this place divided by the\n"
"     number of levels of this place that you've seen.\n\n";

    text += "               ";
    text += "    A       B       C       D       E               F\n";
    text += "               ";
    text += "+-------+-------+-------+-------+-------+----------------------\n";

    text += _sdump_turns_place_info(you.global_info, "Total");

    for (unsigned int i = 0; i < all_visited.size(); i++)
    {
        PlaceInfo pi = all_visited[i];
        text += _sdump_turns_place_info(pi);
    }

    text += "               ";
    text += "+-------+-------+-------+-------+-------+----------------------\n";

    text += "\n";
}

static void _sdump_newline(dump_params &par)
{
    par.text += "\n";
}

static void _sdump_separator(dump_params &par)
{
    par.text += std::string(79, '-') + "\n";
}

#ifdef CLUA_BINDINGS
// Assume this is an arbitrary Lua function name, call the function and
// dump whatever it returns.
static void _sdump_lua(dump_params &par)
{
    std::string luatext;
    if (!clua.callfn(par.section.c_str(), ">s", &luatext)
        && !clua.error.empty())
    {
        par.text += "Lua dump error: " + clua.error + "\n";
    }
    else
        par.text += luatext;
}
#endif

 //---------------------------------------------------------------
 //
 // munge_description
 //
 // Convert dollar signs to EOL and word wrap to 80 characters.
 // (for some obscure reason get_item_description uses dollar
 // signs instead of EOL).
 //  - It uses $ signs because they're easier to manipulate than the EOL
 //  macro, which is of uncertain length (well, that and I didn't know how
 //  to do it any better at the time) (LH)
 //---------------------------------------------------------------
std::string munge_description(const std::string & inStr)
{
    std::string outStr;
    std::string eol = "\n";

    outStr.reserve(inStr.length() + 32);

    const int kIndent = 3;
    int lineLen = kIndent;

    unsigned int i = 0;

    outStr += std::string(kIndent, ' ');

    while (i < inStr.length())
    {
        const char ch = inStr[i];

        if (ch == '$')
        {
            outStr += eol;

            outStr += std::string(kIndent, ' ');
            lineLen = kIndent;

            while (inStr[++i] == '$')
                ;
        }
        else if (isspace(ch))
        {
            if (lineLen >= 79)
            {
                outStr += eol;
                outStr += std::string(kIndent, ' ');
                lineLen = kIndent;

            }
            else if (lineLen > 0)
            {
                outStr += ch;
                ++lineLen;
            }
            ++i;
        }
        else
        {
            std::string word;

            while (i < inStr.length()
                   && lineLen + word.length() < 79
                   && !isspace(inStr[i]) && inStr[i] != '$')
            {
                word += inStr[i++];
            }

            if (lineLen + word.length() >= 79)
            {
                outStr += eol;
                outStr += std::string(kIndent, ' ');
                lineLen = kIndent;
            }

            outStr += word;
            lineLen += word.length();
        }
    }

    outStr += eol;

    return (outStr);
}                               // end munge_description()

static void _sdump_messages(dump_params &par)
{
    // A little message history:
    if (Options.dump_message_count > 0)
    {
        par.text += "Message History\n\n";
        par.text += get_last_messages(Options.dump_message_count);
    }
}

static void _sdump_screenshot(dump_params &par)
{
    par.text += screenshot();
    par.text += "\n\n";
}

static void _sdump_notes(dump_params &par)
{
    std::string &text(par.text);
    if ( note_list.empty() )
        return;

    text += "\nNotes\nTurn   | Place   | Note\n";
    text += "--------------------------------------------------------------\n";
    for ( unsigned i = 0; i < note_list.size(); ++i )
    {
        text += note_list[i].describe();
        text += "\n";
    }
    text += "\n";
}

 //---------------------------------------------------------------
 //
 // dump_location
 //
 //---------------------------------------------------------------
static void _sdump_location(dump_params &par)
{
    if (you.your_level == -1
        && you.where_are_you == BRANCH_MAIN_DUNGEON
        && you.level_type == LEVEL_DUNGEON)
    {
        par.text += "You escaped";
    }
    else if (par.se)
        par.text += "You were " + prep_branch_level_name();
    else
        par.text += "You are " + prep_branch_level_name();

    par.text += ".";
    par.text += "\n";
}                               // end dump_location()

static void _sdump_religion(dump_params &par)
{
    std::string &text(par.text);
    if (you.religion != GOD_NO_GOD)
    {
        if (par.se)
            text += "You worshipped ";
        else
            text += "You worship ";
        text += god_name(you.religion);
        text += ".\n";

        if (you.religion != GOD_XOM)
        {
            if (!player_under_penance())
            {
                text += god_prayer_reaction();
                text += "\n";
            }
            else
            {
                std::string verb = par.se ? "was" : "is";

                text += god_name(you.religion);
                text += " " + verb + " demanding penance.\n";
            }
        }
    }
}

static bool _dump_item_origin(const item_def &item, int value)
{
#define fs(x) (flags & (x))
    const int flags = Options.dump_item_origins;
    if (flags == IODS_EVERYTHING)
        return (true);

    if (fs(IODS_ARTEFACTS)
        && (is_random_artefact(item) || is_fixed_artefact(item))
        && item_ident(item, ISFLAG_KNOW_PROPERTIES))
    {
        return (true);
    }
    if (fs(IODS_EGO_ARMOUR) && item.base_type == OBJ_ARMOUR
        && item_type_known( item ))
    {
        const int spec_ench = get_armour_ego_type( item );
        return (spec_ench != SPARM_NORMAL);
    }

    if (fs(IODS_EGO_WEAPON) && item.base_type == OBJ_WEAPONS
        && item_type_known( item ))
    {
        return (get_weapon_brand(item) != SPWPN_NORMAL);
    }

    if (fs(IODS_JEWELLERY) && item.base_type == OBJ_JEWELLERY)
        return (true);

    if (fs(IODS_RUNES) && item.base_type == OBJ_MISCELLANY
        && item.sub_type == MISC_RUNE_OF_ZOT)
    {
        return (true);
    }

    if (fs(IODS_RODS) && item.base_type == OBJ_STAVES
        && item_is_rod(item))
    {
        return (true);
    }

    if (fs(IODS_STAVES) && item.base_type == OBJ_STAVES
        && !item_is_rod(item))
    {
        return (true);
    }

    if (fs(IODS_BOOKS) && item.base_type == OBJ_BOOKS)
        return (true);

    const int refpr = Options.dump_item_origin_price;
    if (refpr == -1)
        return (false);
    if (value == -1)
        value = item_value( item, false );
    return (value >= refpr);
#undef fs
}

 //---------------------------------------------------------------
 //
 // dump_inventory
 //
 //---------------------------------------------------------------
static void _sdump_inventory(dump_params &par)
{
    int i, j;

    std::string &text(par.text);
    std::string text2;

    int inv_class2[OBJ_GOLD];
    int inv_count = 0;
    char tmp_quant[20];

    for (i = 0; i < OBJ_GOLD; i++)
        inv_class2[i] = 0;

    for (i = 0; i < ENDOFPACK; i++)
    {
        if (is_valid_item( you.inv[i] ))
        {
            // adds up number of each class in invent.
            inv_class2[you.inv[i].base_type]++;
            inv_count++;
        }
    }

    if (!inv_count)
    {
        text += "You aren't carrying anything.";
        text += "\n";
    }
    else
    {
        text += "Inventory:\n\n";

        for (i = 0; i < OBJ_GOLD; i++)
        {
            if (inv_class2[i] != 0)
            {
                switch (i)
                {
                case OBJ_WEAPONS:    text += "Hand weapons";    break;
                case OBJ_MISSILES:   text += "Missiles";        break;
                case OBJ_ARMOUR:     text += "Armour";          break;
                case OBJ_WANDS:      text += "Magical devices"; break;
                case OBJ_FOOD:       text += "Comestibles";     break;
                case OBJ_SCROLLS:    text += "Scrolls";         break;
                case OBJ_JEWELLERY:  text += "Jewellery";       break;
                case OBJ_POTIONS:    text += "Potions";         break;
                case OBJ_BOOKS:      text += "Books";           break;
                case OBJ_STAVES:     text += "Magical staves";  break;
                case OBJ_ORBS:       text += "Orbs of Power";   break;
                case OBJ_MISCELLANY: text += "Miscellaneous";   break;
                case OBJ_CORPSES:    text += "Carrion";         break;

                default:
                    DEBUGSTR("Bad item class");
                }
                text += "\n";

                for (j = 0; j < ENDOFPACK; j++)
                {
                    if (is_valid_item(you.inv[j]) && you.inv[j].base_type == i)
                    {
                        text += " ";
                        text += you.inv[j].name(DESC_INVENTORY_EQUIP);

                        inv_count--;

                        int ival = -1;
                        if (par.show_prices)
                        {
                            text += " (";

                            itoa( ival = item_value( you.inv[j], true ),
                                  tmp_quant, 10 );

                            text += tmp_quant;
                            text += " gold)";
                        }

                        if (origin_describable(you.inv[j])
                            && _dump_item_origin(you.inv[j], ival))
                        {
                            text += "\n" "   (" + origin_desc(you.inv[j]) + ")";
                        }

                        if (is_dumpable_artefact( you.inv[j], false ))
                        {
                            text2 = get_item_description( you.inv[j],
                                                          false,
                                                          true );

                            text += munge_description(text2);
                        }
                        else
                        {
                            text += "\n";
                        }
                    }
                }
            }
        }
    }
    text += "\n\n";
}                               // end dump_inventory()

//---------------------------------------------------------------
//
// dump_skills
//
//---------------------------------------------------------------
static void _sdump_skills(dump_params &par)
{
    std::string &text(par.text);
    char tmp_quant[20];

    if (par.se)
        text += " You had ";
    else
        text += " You have ";

    itoa( you.exp_available, tmp_quant, 10 );
    text += tmp_quant;
    text += " experience left.";

    text += "\n";
    text += "\n";
    text += "   Skills:";
    text += "\n";

    for (unsigned char i = 0; i < 50; i++)
    {
        if (you.skills[i] > 0)
        {
            text += ( (you.skills[i] == 27)   ? " * " :
                      (you.practise_skill[i]) ? " + "
                                              : " - " );

            text += "Level ";
            itoa( you.skills[i], tmp_quant, 10 );
            text += tmp_quant;
            text += " ";
            text += skill_name(i);
            text += "\n";
        }
    }

    text += "\n";
    text += "\n";
}                               // end dump_skills()

//---------------------------------------------------------------
//
// Return string of the i-th spell type, with slash if required
//
//---------------------------------------------------------------
static std::string spell_type_shortname(int spell_class, bool slash)
{
    std::string ret;

    if (slash)
        ret = "/";

    ret += spelltype_short_name(spell_class);

    return (ret);
}                               // end spell_type_shortname()

//---------------------------------------------------------------
//
// dump_spells
//
//---------------------------------------------------------------
static void _sdump_spells(dump_params &par)
{
    std::string &text(par.text);
    char tmp_quant[20];

// This array helps output the spell types in the traditional order.
// this can be tossed as soon as I reorder the enum to the traditional order {dlb}
    const int spell_type_index[] =
    {
        SPTYP_HOLY,
        SPTYP_POISON,
        SPTYP_FIRE,
        SPTYP_ICE,
        SPTYP_EARTH,
        SPTYP_AIR,
        SPTYP_CONJURATION,
        SPTYP_ENCHANTMENT,
        SPTYP_DIVINATION,
        SPTYP_TRANSLOCATION,
        SPTYP_SUMMONING,
        SPTYP_TRANSMUTATION,
        SPTYP_NECROMANCY,
        0
    };

    int spell_levels = player_spell_levels();

    std::string verb = par.se? "had" : "have";

    if (spell_levels == 1)
        text += "You " + verb + " one spell level left.";
    else if (spell_levels == 0)
    {
        verb = par.se? "couldn't" : "cannot";

        text += "You " + verb + " memorise any spells.";
    }
    else
    {
        if (par.se)
            text += "You had ";
        else
            text += "You have ";
        itoa( spell_levels, tmp_quant, 10 );
        text += tmp_quant;
        text += " spell levels left.";
    }

    text += "\n";

    if (!you.spell_no)
    {
        verb = par.se? "didn't" : "don't";

        text += "You " + verb + " know any spells.\n\n";
    }
    else
    {
        verb = par.se? "knew" : "know";

        text += "You " + verb + " the following spells:\n\n";

        text += " Your Spells              Type           Power          Success   Level" "\n";

        for (int j = 0; j < 52; j++)
        {
            const char letter = index_to_letter( j );
            const spell_type spell  = get_spell_by_letter( letter );

            if (spell != SPELL_NO_SPELL)
            {
                std::string spell_line;

                spell_line += letter;
                spell_line += " - ";
                spell_line += spell_title( spell );

                if ( spell_line.length() > 24 )
                    spell_line = spell_line.substr(0, 24);

                for (int i = spell_line.length(); i < 26; i++)
                    spell_line += ' ';

                bool already = false;

                for (int i = 0; spell_type_index[i] != 0; i++)
                {
                    if (spell_typematch( spell, spell_type_index[i] ))
                    {
                        spell_line += spell_type_shortname(spell_type_index[i],
                                                           already);
                        already = true;
                    }
                }

                for (int i = spell_line.length(); i < 41; ++i )
                    spell_line += ' ';

                spell_line += spell_power_string(spell);

                for (int i = spell_line.length(); i < 56; ++i )
                    spell_line += ' ';

                spell_line += failure_rate_to_string(spell_fail(spell));

                for (int i = spell_line.length(); i < 68; i++)
                    spell_line += ' ';

                itoa(spell_difficulty(spell), tmp_quant, 10 );
                spell_line += tmp_quant;
                spell_line += "\n";

                text += spell_line;
            }
        }
        text += "\n\n";
    }
}                               // end dump_spells()


static void _sdump_kills(dump_params &par)
{
    par.text += you.kills->kill_info();
}

static std::string _sdump_kills_place_info(PlaceInfo place_info,
                                          std::string name = "")
{
    PlaceInfo   gi = you.global_info;
    std::string out;

    if (name.empty())
        name = place_info.short_name();

    unsigned int global_total_kills = 0;
    for (int i = 0; i < KC_NCATEGORIES; i++)
        global_total_kills += you.global_info.mon_kill_num[i];

    unsigned int total_kills = 0;
    for (int i = 0; i < KC_NCATEGORIES; i++)
        total_kills += place_info.mon_kill_num[i];

    // Skip places where nothing was killed.
    if (total_kills == 0)
        return "";

    float a, b, c, d, e, f, g;

    a = TO_PERCENT(total_kills, global_total_kills);
    b = TO_PERCENT(place_info.mon_kill_num[KC_YOU],
                   you.global_info.mon_kill_num[KC_YOU]);
    c = TO_PERCENT(place_info.mon_kill_num[KC_FRIENDLY],
                   you.global_info.mon_kill_num[KC_FRIENDLY]);
    d = TO_PERCENT(place_info.mon_kill_num[KC_OTHER],
                   you.global_info.mon_kill_num[KC_OTHER]);
    e = TO_PERCENT(place_info.mon_kill_exp,
                   you.global_info.mon_kill_exp);
    f = TO_PERCENT(place_info.mon_kill_exp_avail,
                   you.global_info.mon_kill_exp_avail);

    g = (float) MAXIMUM(place_info.mon_kill_exp,
                        place_info.mon_kill_exp_avail) /
        (float) place_info.levels_seen;

    out =
        make_stringf("%14s | %5.1f | %5.1f | %5.1f | %5.1f | %5.1f |"
                     " %5.1f | %13.1f\n",
                     name.c_str(), a, b, c , d, e, f, g);

    out = replace_all(out, " nan ", " N/A ");

    return out;
}

static void _sdump_kills_by_place(dump_params &par)
{
    std::string &text(par.text);

    std::vector<PlaceInfo> all_visited =
        you.get_all_place_info(true);

    std::string result = "";

    std::string header =
    "Table legend:\n"
    " A = Kills in this place as a percentage of kills in entire the game.\n"
    " B = Kills by you in this place as a percentage of kills by you in\n"
    "     the entire game.\n"
    " C = Kills by friends in this place as a percentage of kills by\n"
    "     friends in the entire game.\n"
    " D = Other kills in this place as a percentage of other kills in the\n"
    "     entire game.\n"
    " E = Character level experience gained in this place as a percentage of\n"
    "     character level experience gained in the entire game.\n"
    " F = Skills experience gained in this place as a percentage of skills\n"
    "     experience gained in the entire game.\n"
    " G = Experience gained in this place divided by the number of levels of\n"
    "     this place that you have seen.\n\n";

    header += "               ";
    header += "    A       B       C       D       E       F          G\n";
    header += "               ";
    header += "+-------+-------+-------+-------+-------+-------+--------------\n";

    std::string footer = "               ";
    footer += "+-------+-------+-------+-------+-------+-------+--------------\n";

    result += _sdump_kills_place_info(you.global_info, "Total");

    for (unsigned int i = 0; i < all_visited.size(); i++)
    {
        PlaceInfo pi = all_visited[i];
        result += _sdump_kills_place_info(pi);
    }

    if (result.length() > 0)
        text += header + result + footer + "\n";
}

static void _sdump_overview(dump_params &par)
{
    std::string overview =
        formatted_string::parse_string(overview_description_string());
    trim_string(overview);
    par.text += overview;
    par.text += "\n\n";
}

static void _sdump_hiscore(dump_params &par)
{
    if (!par.se)
        return;

    std::string hiscore = hiscores_format_single_long( *(par.se), true );
    trim_string(hiscore);
    par.text += hiscore;
    par.text += "\n\n";
}

static void _sdump_monster_list(dump_params &par)
{
    std::string monlist = mpr_monster_list(par.se);
    trim_string(monlist);
    par.text += monlist;
    par.text += "\n\n";
}

static void _sdump_mutations(dump_params &par)
{
    std::string &text(par.text);

    if (how_mutated(true, false))
    {
        text += "\n";
        text += describe_mutations();
        text += "\n\n";
    }
}                               // end dump_mutations()

// ========================================================================
//      Public Functions
// ========================================================================

const char *hunger_level(void)
{
    const bool vamp = (you.species == SP_VAMPIRE);

    return ((you.hunger <= 1000) ? (vamp ? "bloodless" : "starving") :
            (you.hunger <= 1533) ? (vamp ? "near bloodless" : "near starving") :
            (you.hunger <= 2066) ? (vamp ? "very thirsty" : "very hungry") :
            (you.hunger <= 2600) ? (vamp ? "thirsty" : "hungry") :
            (you.hunger <  7000) ? (vamp ? "not thirsty" : "not hungry") :
            (you.hunger <  9000) ? "full" :
            (you.hunger < 11000) ? "very full"
                                 : (vamp ? "almost alive" : "completely stuffed"));
}

static std::string morgue_directory()
{
    std::string dir = (!Options.morgue_dir.empty() ? Options.morgue_dir :
                       !SysEnv.crawl_dir.empty()   ? SysEnv.crawl_dir
                                                   : "");

    if (!dir.empty() && dir[dir.length() - 1] != FILE_SEPARATOR)
        dir += FILE_SEPARATOR;

    return (dir);
}

void dump_map(FILE *fp, bool debug)
{
    // Duplicate the screenshot() trick.
    FixedVector<unsigned, NUM_DCHAR_TYPES> char_table_bk;
    char_table_bk = Options.char_table;

    init_char_table(CSET_ASCII);
    init_feature_table();

    if (debug)
    {
        // Write the whole map out without checking for mappedness. Handy
        // for debugging level-generation issues.
        for (int y = 0; y < GYM; ++y)
        {
            for (int x = 0; x < GXM; ++x)
                fputc(grid_character_at(coord_def(x,y)), fp);
            fputc('\n', fp);
        }
    }
    else
    {
        int min_x = GXM-1, max_x = 0, min_y = GYM-1, max_y = 0;

        for (int i = X_BOUND_1; i <= X_BOUND_2; i++)
            for (int j = Y_BOUND_1; j <= Y_BOUND_2; j++)
                if (env.map[i][j].known())
                {
                    if (i > max_x) max_x = i;
                    if (i < min_x) min_x = i;
                    if (j > max_y) max_y = j;
                    if (j < min_y) min_y = j;
                }

        for (int y = min_y; y <= max_y; ++y)
        {
            for (int x = min_x; x <= max_x; ++x)
                fputc( env.map[x][y].glyph(), fp );

            fputc('\n', fp);
        }
    }

    // Restore char and feature tables
    Options.char_table = char_table_bk;
    init_feature_table();
}

void dump_map(const char* fname, bool debug)
{
    FILE* fp = fopen(fname, "w");
    if (!fp)
        return;

    dump_map(fp, debug);

    fclose(fp);
}

static bool write_dump( const std::string &fname, dump_params &par)
{
    bool succeeded = false;

    std::string file_name = morgue_directory();

    file_name += strip_filename_unsafe_chars(fname);

    StashTrack.update_corpses();

    std::string stash_file_name;
    stash_file_name = file_name;
    stash_file_name += ".lst";
    StashTrack.dump(stash_file_name.c_str(), par.full_id);

    std::string map_file_name = file_name + ".map";
    dump_map(map_file_name.c_str());

    file_name += ".txt";
    FILE *handle = fopen(file_name.c_str(), "w");

#if DEBUG_DIAGNOSTICS
    mprf(MSGCH_DIAGNOSTICS, "File name: %s", file_name.c_str());
#endif

    if (handle != NULL)
    {
        fputs(par.text.c_str(), handle);
        fclose(handle);
        succeeded = true;
    }
    else
        mprf(MSGCH_ERROR, "Error opening file '%s'", file_name.c_str());

    return (succeeded);
}

void display_notes()
{
    Menu scr;
    scr.set_tag("notes");
    scr.set_title(new MenuEntry("Turn   | Place   | Note"));
    for (unsigned int i = 0; i < note_list.size(); ++i)
    {
        std::string prefix = note_list[i].describe(true, true, false);
        std::string suffix = note_list[i].describe(false, false, true);
        if (suffix.empty())
            continue;

        int spaceleft = get_number_of_cols() - prefix.length() - 1;
        if (spaceleft <= 0)
            return;

        // Use smarter linebreak function.
        // was:  linebreak_string(suffix, spaceleft - 4, spaceleft);
        linebreak_string2(suffix, spaceleft);
        std::vector<std::string> parts = split_string("\n", suffix);
        if (parts.empty()) // Disregard pure-whitespace notes.
            continue;

        scr.add_entry(new MenuEntry(prefix + parts[0]));
        for (unsigned int j = 1; j < parts.size(); ++j)
        {
            scr.add_entry(new MenuEntry(std::string(prefix.length()-2, ' ') +
                                        std::string("| ") + parts[j]));
        }
    }
    scr.show();
    redraw_screen();
}

#ifdef DGL_WHEREIS
///////////////////////////////////////////////////////////////////////////
// whereis player
void whereis_record(const char *status)
{
    const std::string file_name =
        morgue_directory()
        + strip_filename_unsafe_chars(you.your_name)
        + std::string(".where");

    if (FILE *handle = fopen(file_name.c_str(), "w"))
    {
        fprintf(handle, "%s:status=%s\n",
                xlog_status_line().c_str(),
                status? status : "");
        fclose(handle);
    }
}
#endif
