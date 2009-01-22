/*
 *  File:       spl-mis.cc
 *  Summary:    Spell miscast class.
 *  Written by: Matthew Cline
 *
 *  Modified for Crawl Reference by $Author$ on $Date: 2008-06-28 22:
16:39 -0700 (Sat, 28 Jun 2008) $
 */

#include "AppHdr.h"
REVISION("$Rev$");

#include "spl-mis.h"

#include "externs.h"

#include <sstream>

#include "cloud.h"
#include "effects.h"
#include "it_use2.h"
#include "Kills.h"
#include "misc.h"
#include "monplace.h"
#include "monstuff.h"
#include "mon-util.h"
#include "mutation.h"
#include "player.h"
#include "religion.h"
#include "spells1.h"
#include "state.h"
#include "stuff.h"
#include "terrain.h"
#include "transfor.h"
#include "view.h"
#include "xom.h"

// This determines how likely it is that more powerful wild magic
// effects will occur.  Set to 100 for the old probabilities (although
// the individual effects have been made much nastier since then).
#define WILD_MAGIC_NASTINESS 150

#define MAX_RECURSE 100

MiscastEffect::MiscastEffect(actor* _target, int _source, spell_type _spell,
                             int _pow, int _fail, std::string _cause,
                             nothing_happens_when_type _nothing_happens,
                             int _lethality_margin, std::string _hand_str,
                             bool _can_plural) :
    target(_target), source(_source), cause(_cause), spell(_spell),
    school(SPTYP_NONE), pow(_pow), fail(_fail), level(-1), kc(KC_NCATEGORIES),
    kt(KILL_NONE), nothing_happens_when(_nothing_happens),
    lethality_margin(_lethality_margin), hand_str(_hand_str),
    can_plural_hand(_can_plural)
{
    ASSERT(is_valid_spell(_spell));
    unsigned int schools = get_spell_disciplines(_spell);
    ASSERT(schools != SPTYP_NONE);
    ASSERT(!(schools & SPTYP_HOLY));
    UNUSED(schools);

    init();
    do_miscast();
}

MiscastEffect::MiscastEffect(actor* _target, int _source,
                             spschool_flag_type _school, int _level,
                             std::string _cause,
                             nothing_happens_when_type _nothing_happens,
                             int _lethality_margin, std::string _hand_str,
                             bool _can_plural) :
    target(_target), source(_source), cause(_cause), spell(SPELL_NO_SPELL),
    school(_school), pow(-1), fail(-1), level(_level), kc(KC_NCATEGORIES),
    kt(KILL_NONE), nothing_happens_when(_nothing_happens),
    lethality_margin(_lethality_margin), hand_str(_hand_str),
    can_plural_hand(_can_plural)
{
    ASSERT(!_cause.empty());
    ASSERT(count_bits(_school) == 1);
    ASSERT(_school < SPTYP_HOLY || _school == SPTYP_RANDOM);
    ASSERT(level >= 0 && level <= 3);

    init();
    do_miscast();
}

MiscastEffect::MiscastEffect(actor* _target, int _source,
                             spschool_flag_type _school, int _pow, int _fail,
                             std::string _cause,
                             nothing_happens_when_type _nothing_happens,
                             int _lethality_margin, std::string _hand_str,
                             bool _can_plural) :
    target(_target), source(_source), cause(_cause), spell(SPELL_NO_SPELL),
    school(_school), pow(_pow), fail(_fail), level(-1), kc(KC_NCATEGORIES),
    kt(KILL_NONE), nothing_happens_when(_nothing_happens),
    lethality_margin(_lethality_margin), hand_str(_hand_str),
    can_plural_hand(_can_plural)
{
    ASSERT(!_cause.empty());
    ASSERT(count_bits(_school) == 1);
    ASSERT(_school < SPTYP_HOLY || _school == SPTYP_RANDOM);

    init();
    do_miscast();
}

MiscastEffect::~MiscastEffect()
{
    ASSERT(recursion_depth == 0);
}

void MiscastEffect::init()
{
    ASSERT(spell != SPELL_NO_SPELL && school == SPTYP_NONE
           || spell == SPELL_NO_SPELL && school != SPTYP_NONE);
    ASSERT(pow != -1 && fail != -1 && level == -1
           || pow == -1 && fail == -1 && level >= 0 && level <= 3);

    ASSERT(target != NULL);
    ASSERT(target->alive());

    ASSERT(lethality_margin == 0 || target->atype() == ACT_PLAYER);

    recursion_depth = 0;

    source_known = target_known = false;

    act_source = NULL;

    const bool death_curse = (cause.find("death curse") != std::string::npos);

    if (target->atype() == ACT_MONSTER)
        target_known = you.can_see(target);
    else
        target_known = true;

    kill_source = source;
    if (source == WIELD_MISCAST || source == MELEE_MISCAST)
    {
        if (target->atype() == ACT_MONSTER)
            kill_source = target->mindex();
        else
            kill_source = NON_MONSTER;
    }

    if (kill_source == NON_MONSTER)
    {
        kc           = KC_YOU;
        kt           = KILL_YOU_MISSILE;
        act_source   = &you;
        source_known = true;
    }
    else if (!invalid_monster_index(kill_source))
    {
        monsters* mon_source = &menv[kill_source];
        ASSERT(mon_source->type != -1);

        act_source = mon_source;

        if (death_curse && target->atype() == ACT_MONSTER
            && target_as_monster()->confused_by_you())
        {
            kt = KILL_YOU_CONF;
        }
        else if (!death_curse && mon_source->confused_by_you()
                 && !mons_friendly(mon_source))
        {
            kt = KILL_YOU_CONF;
        }
        else
            kt = KILL_MON_MISSILE;

        if (mons_friendly(mon_source))
            kc = KC_FRIENDLY;
        else
            kc = KC_OTHER;

        source_known = you.can_see(mon_source);

        if (target_known && death_curse)
            source_known = true;
    }
    else
    {
        ASSERT(source == ZOT_TRAP_MISCAST
               || source == MISC_KNOWN_MISCAST
               || source == MISC_UNKNOWN_MISCAST
               || (source < 0 && -source < NUM_GODS));

        act_source = target;

        kc = KC_OTHER;
        kt = KILL_MISC;

        if (source == ZOT_TRAP_MISCAST)
        {
            source_known = target_known;

            if (target->atype() == ACT_MONSTER
                && target_as_monster()->confused_by_you())
            {
                kt = KILL_YOU_CONF;
            }
        }
        else if (source == MISC_KNOWN_MISCAST)
            source_known = true;
        else if (source == MISC_UNKNOWN_MISCAST)
            source_known = false;
        else
            source_known = true;
    }

    ASSERT(kc != KC_NCATEGORIES && kt != KILL_NONE);

    if (death_curse && !invalid_monster_index(kill_source))
    {
        if (starts_with(cause, "a "))
            cause.replace(cause.begin(), cause.begin() + 1, "an indirect");
        else if (starts_with(cause, "an "))
            cause.replace(cause.begin(), cause.begin() + 2, "an indirect");
        else
            cause = replace_all(cause, "death curse", "indirect death curse");
   }

    // source_known = false for MELEE_MISCAST so that melee miscasts
    // won't give a "nothing happens" message.
    if (source == MELEE_MISCAST)
        source_known = false;

    if (hand_str.empty())
        target->hand_name(true, &can_plural_hand);

    // Explosion stuff.
    beam.is_explosion = true;

    if (cause.empty())
        cause = get_default_cause();
    beam.aux_source  = cause;
    beam.beam_source = kill_source;
    beam.thrower     = kt;
}

std::string MiscastEffect::get_default_cause()
{
    // This is only for true miscasts, which means both a spell and that
    // the source of the miscast is the same as the target of the miscast.
    ASSERT(source >= 0 && source <= NON_MONSTER);
    ASSERT(spell != SPELL_NO_SPELL);
    ASSERT(school == SPTYP_NONE);

    if (source == NON_MONSTER)
    {
        ASSERT(target->atype() == ACT_PLAYER);
        std::string str = "your miscasting ";
        str += spell_title(spell);
        return str;
    }

    ASSERT(act_source->atype() == ACT_MONSTER);
    ASSERT(act_source == target);

    if (you.can_see(act_source))
    {
        return apostrophise(source_as_monster()->base_name(DESC_PLAIN))
            + " spell miscasting";
    }
    else
        return "something's spell miscasting";
}

bool MiscastEffect::neither_end_silenced()
{
    return (!silenced(you.pos()) && !silenced(target->pos()));
}

void MiscastEffect::do_miscast()
{
    ASSERT(recursion_depth >= 0 && recursion_depth < MAX_RECURSE);

    if (recursion_depth == 0)
        did_msg = false;

    unwind_var<int> unwind_depth(recursion_depth);
    recursion_depth++;

    // Repeated calls to do_miscast() on a single object instance have
    // killed a target which was alive when the object was created.
    if (!target->alive())
    {
#ifdef DEBUG_DIAGNOSTICS
        mprf(MSGCH_DIAGNOSTICS, "Miscast target '%s' already dead",
             target->name(DESC_PLAIN, true).c_str());
#endif
        return;
    }

    spschool_flag_type sp_type;
    int                severity;

    if (spell != SPELL_NO_SPELL)
    {
        std::vector<int> school_list;
        for (int i = 0; i < SPTYP_LAST_EXPONENT; i++)
            if (spell_typematch(spell, 1 << i))
                school_list.push_back(i);

        unsigned int _school = school_list[random2(school_list.size())];
        sp_type = static_cast<spschool_flag_type>(1 << _school);
    }
    else
    {
        sp_type = school;
        if (sp_type == SPTYP_RANDOM)
        {
            int exp = (random2(SPTYP_LAST_EXPONENT));
            sp_type = (spschool_flag_type) (1 << exp);
        }
    }

    if (level != -1)
        severity = level;
    else
    {
        severity = (pow * fail * (10 + pow) / 7 * WILD_MAGIC_NASTINESS) / 100;

#if DEBUG_DIAGNOSTICS || DEBUG_MISCAST
        mprf(MSGCH_DIAGNOSTICS, "'%s' miscast power: %d",
             spell != SPELL_NO_SPELL ? spell_title(spell)
                                     : spelltype_short_name(sp_type),
             severity);
#endif

        if (random2(40) > severity && random2(40) > severity)
        {
            if (target->atype() == ACT_PLAYER)
                canned_msg(MSG_NOTHING_HAPPENS);
            return;
        }

        severity /= 100;
        severity = random2(severity);
        if (severity > 3)
            severity = 3;
        else if (severity < 0)
            severity = 0;
    }

#if DEBUG_DIAGNOSTICS || DEBUG_MISCAST
    mprf(MSGCH_DIAGNOSTICS, "Sptype: %s, severity: %d",
         spelltype_short_name(sp_type), severity );
#endif

    beam.ex_size            = 1;
    beam.name               = "";
    beam.damage             = dice_def(0, 0);
    beam.flavour            = BEAM_NONE;
    beam.msg_generated      = false;
    beam.in_explosion_phase = false;

    // Do this here since multiple miscasts (wizmode testing) might move
    // the target around.
    beam.source = target->pos();
    beam.target = target->pos();
    beam.use_target_as_pos = true;

    all_msg = you_msg = mon_msg = mon_msg_seen = mon_msg_unseen = "";
    msg_ch  = MSGCH_PLAIN;

    switch (sp_type)
    {
    case SPTYP_CONJURATION:    _conjuration(severity);    break;
    case SPTYP_ENCHANTMENT:    _enchantment(severity);    break;
    case SPTYP_TRANSLOCATION:  _translocation(severity);  break;
    case SPTYP_SUMMONING:      _summoning(severity);      break;
    case SPTYP_NECROMANCY:     _necromancy(severity);     break;
    case SPTYP_TRANSMUTATION:  _transmutation(severity); break;
    case SPTYP_FIRE:           _fire(severity);           break;
    case SPTYP_ICE:            _ice(severity);            break;
    case SPTYP_EARTH:          _earth(severity);          break;
    case SPTYP_AIR:            _air(severity);            break;
    case SPTYP_POISON:         _poison(severity);         break;
    case SPTYP_DIVINATION:
        // Divination miscasts have nothing in common between the player
        // and monsters.
        if (target->atype() == ACT_PLAYER)
            _divination_you(severity);
        else
            _divination_mon(severity);
        break;

    default:
        DEBUGSTR("Invalid miscast spell discipline.");
    }

    if (target->atype() == ACT_PLAYER)
        xom_is_stimulated(severity * 50);
}

void MiscastEffect::do_msg(bool suppress_nothing_happnes)
{
    ASSERT(!did_msg);

    if (target->atype() == ACT_MONSTER && !mons_near(target_as_monster()))
        return;

    did_msg = true;

    std::string msg;

    if (!all_msg.empty())
        msg = all_msg;
    else if (target->atype() == ACT_PLAYER)
        msg = you_msg;
    else if (!mon_msg.empty())
    {
        msg = mon_msg;
        // Monster might be unseen with hands that can't be seen.
        ASSERT(msg.find("@hand") == std::string::npos);
    }
    else
    {
        if (you.can_see(target))
            msg = mon_msg_seen;
        else
        {
            msg = mon_msg_unseen;
            // Can't see the hands of invisible monsters.
            ASSERT(msg.find("@hand") == std::string::npos);
        }
    }

    if (msg.empty())
    {
        if (!suppress_nothing_happnes
            && (nothing_happens_when == NH_ALWAYS
                || (nothing_happens_when == NH_DEFAULT && source_known
                    && target_known)))
        {
            canned_msg(MSG_NOTHING_HAPPENS);
        }

        return;
    }

    bool plural;

    if (hand_str.empty())
    {
        msg = replace_all(msg, "@hand@",  target->hand_name(false, &plural));
        msg = replace_all(msg, "@hands@", target->hand_name(true));
    }
    else
    {
        plural = can_plural_hand;
        msg = replace_all(msg, "@hand@",  hand_str);
        if (can_plural_hand)
            msg = replace_all(msg, "@hands@", pluralise(hand_str));
        else
            msg = replace_all(msg, "@hands@", hand_str);
    }

    if (plural)
        msg = replace_all(msg, "@hand_conj@", "");
    else
        msg = replace_all(msg, "@hand_conj@", "s");

    if (target->atype() == ACT_MONSTER)
        msg = do_mon_str_replacements(msg, target_as_monster(), S_SILENT);

    mpr(msg.c_str(), msg_ch);
}

bool MiscastEffect::_ouch(int dam, beam_type flavour)
{
    // Delay do_msg() until after avoid_lethal().
    if (target->atype() == ACT_MONSTER)
    {
        monsters* mon_target = target_as_monster();

        do_msg(true);

        bolt beem;

        beem.flavour = flavour;
        dam = mons_adjust_flavoured(mon_target, beem, dam, true);
        mon_target->hurt(NULL, dam, BEAM_MISSILE, false);

        if (!mon_target->alive())
            monster_die(mon_target, kt, kill_source);
    }
    else
    {
        dam = check_your_resists(dam, flavour);

        if (avoid_lethal(dam))
            return (false);

        do_msg(true);

        kill_method_type method;

        if (source == NON_MONSTER && spell != SPELL_NO_SPELL)
            method = KILLED_BY_WILD_MAGIC;
        else if (source == ZOT_TRAP_MISCAST)
            method = KILLED_BY_TRAP;
        else if (source < 0 && -source < NUM_GODS)
        {
            god_type god = static_cast<god_type>(-source);

            if (god == GOD_XOM && you.penance[GOD_XOM] == 0)
                method = KILLED_BY_XOM;
            else
                method = KILLED_BY_DIVINE_WRATH;
        }
        else if (!invalid_monster_index(source))
            method = KILLED_BY_MONSTER;
        else
            method = KILLED_BY_SOMETHING;

        bool see_source = act_source && you.can_see(act_source);
        ouch(dam, kill_source, method, cause.c_str(), see_source);
    }

    return (true);
}

bool MiscastEffect::_explosion()
{
    ASSERT(!beam.name.empty());
    ASSERT(beam.damage.num != 0 && beam.damage.size != 0);
    ASSERT(beam.flavour != BEAM_NONE);

    int max_dam = beam.damage.num * beam.damage.size;
    max_dam = check_your_resists(max_dam, beam.flavour);
    if (avoid_lethal(max_dam))
        return (false);

    do_msg(true);
    beam.explode();

    return (true);
}

bool MiscastEffect::_lose_stat(unsigned char which_stat,
                               unsigned char stat_loss)
{
    if (lethality_margin <= 0)
        return lose_stat(which_stat, stat_loss, false, cause);

    if (which_stat == STAT_RANDOM)
    {
        const int might = you.duration[DUR_MIGHT] ? 5 : 0;

        std::vector<unsigned char> stat_types;
        if ((you.strength - might - stat_loss) > 0)
            stat_types.push_back(STAT_STRENGTH);
        if ((you.intel - stat_loss) > 0)
            stat_types.push_back(STAT_INTELLIGENCE);
        if ((you.dex - stat_loss) > 0)
            stat_types.push_back(STAT_DEXTERITY);

        if (stat_types.size() == 0)
        {
            if (avoid_lethal(you.hp))
                return (false);
            else
                return lose_stat(which_stat, stat_loss, false, cause);
        }

        which_stat = stat_types[random2(stat_types.size())];
    }

    int val;

    switch (which_stat)
    {
        case STAT_STRENGTH:     val = you.strength; break;
        case STAT_INTELLIGENCE: val = you.intel;    break;
        case STAT_DEXTERITY:    val = you.dex;      break;

        default: DEBUGSTR("Invalid stat type."); return (false);
    }

    if ((val - stat_loss) <= 0)
    {
        if (avoid_lethal(you.hp))
            return (false);
    }

    return lose_stat(which_stat, stat_loss, false, cause);
}

void MiscastEffect::_potion_effect(int pot_eff, int pot_pow)
{
    if (target->atype() == ACT_PLAYER)
    {
        potion_effect(static_cast<potion_type>(pot_eff), pot_pow, false);
        return;
    }

    monsters* mon_target = target_as_monster();

    switch (pot_eff)
    {
        case POT_LEVITATION:
            // There's no levitation enchantment for monsters, and,
            // anyway, it's not nearly as inconvenient for monsters as
            // for the player, so backlight them instead.
            mon_target->add_ench(mon_enchant(ENCH_BACKLIGHT, pot_pow, kc));
            break;
        case POT_BERSERK_RAGE:
            if (target->can_go_berserk())
            {
                target->go_berserk(false);
                break;
            }
            // Intentional fallthrough if that didn't work.
        case POT_SLOWING:
            target->slow_down(act_source, pot_pow);
            break;
        case POT_PARALYSIS:
            target->paralyse(act_source, pot_pow);
            break;
        case POT_CONFUSION:
            target->confuse(act_source, pot_pow);
            break;

        default:
            ASSERT(false);
    }
}

void MiscastEffect::send_abyss()
{
    if (you.level_type == LEVEL_ABYSS)
    {
        you_msg        = "The world appears momentarily distorted.";
        mon_msg_seen   = "@The_monster@ wobbles for a moment.";
        mon_msg_unseen = "An empty piece of space momentarily distorts.";
        do_msg();
        return;
    }

    target->banish(cause);
}

bool MiscastEffect::avoid_lethal(int dam)
{
    if (lethality_margin <= 0 || (you.hp - dam) > lethality_margin)
        return (false);

    if (recursion_depth == MAX_RECURSE)
    {
#if DEBUG_DIAGNOSTICS || DEBUG_MISCAST
        mpr("Couldn't avoid lethal miscast: too much recursion.",
            MSGCH_ERROR);
#endif
        return (false);
    }

    if (did_msg)
    {
#if DEBUG_DIAGNOSTICS || DEBUG_MISCAST
        mpr("Couldn't avoid lethal miscast: already printed message for this "
            "miscast.", MSGCH_ERROR);
#endif
        return (false);
    }

#if DEBUG_DIAGNOSTICS || DEBUG_MISCAST
    mpr("Avoided lethal miscast.", MSGCH_DIAGNOSTICS);
#endif

    do_miscast();

    return (true);
}

bool MiscastEffect::_create_monster(monster_type what, int abj_deg,
                                    bool alert)
{
    const god_type god =
        (crawl_state.is_god_acting()) ? crawl_state.which_god_acting()
                                      : GOD_NO_GOD;

    mgen_data data = mgen_data::hostile_at(what, target->pos(),
                                           abj_deg, 0, alert, god);

    // hostile_at() assumes the monster is hostile to the player,
    // but should be hostile to the target monster unless the miscast
    // is a result of either divine wrath or a Zot trap.
    if (target->atype() == ACT_MONSTER && you.penance[god] == 0
        && source != ZOT_TRAP_MISCAST)
    {
        monsters* mon_target = target_as_monster();

        switch (mon_target->temp_attitude())
        {
            case ATT_FRIENDLY:     data.behaviour = BEH_HOSTILE; break;
            case ATT_HOSTILE:      data.behaviour = BEH_FRIENDLY; break;
            case ATT_GOOD_NEUTRAL:
            case ATT_NEUTRAL:
                data.behaviour = BEH_NEUTRAL;
            break;
        }

        if (alert)
            data.foe = monster_index(mon_target);

        // No permanent allies from miscasts.
        if (data.behaviour == BEH_FRIENDLY && abj_deg == 0)
            data.abjuration_duration = 6;
    }

    // If data.abjuration_duration == 0, then data.summon_type will
    // simply be ignored.
    if (data.abjuration_duration != 0)
    {
        if (you.penance[god] > 0)
            data.summon_type = MON_SUMM_WRATH;
        else if (source == ZOT_TRAP_MISCAST)
            data.summon_type = MON_SUMM_ZOT;
        else
            data.summon_type = MON_SUMM_MISCAST;
    }

    return (create_monster(data) != -1);
}

static bool _has_hair(actor* target)
{
    // Don't bother for monsters.
    if (target->atype() == ACT_MONSTER)
        return (false);

    return (!transform_changed_physiology() && you.species != SP_GHOUL
            && you.species != SP_KENKU && !player_genus(GENPC_DRACONIAN));
}

static std::string _hair_str(actor* target, bool &plural)
{
    ASSERT(target->atype() == ACT_PLAYER);

    if (you.species == SP_MUMMY)
    {
        plural = true;
        return "bandages";
    }
    else
    {
        plural = false;
        return "hair";
    }
}

void MiscastEffect::_conjuration(int severity)
{
    int num;
    switch (severity)
    {
    case 0:         // just a harmless message
        num = 10 + (_has_hair(target) ? 1 : 0);
        switch (random2(num))
        {
        case 0:
            you_msg      = "Sparks fly from your @hands@!";
            mon_msg_seen = "Sparks fly from @the_monster@'s @hands@!";
            break;
        case 1:
            you_msg      = "The air around you crackles with energy!";
            mon_msg_seen = "The air around @the_monster@ crackles "
                           "with energy!";
            break;
        case 2:
            you_msg      = "Wisps of smoke drift from your @hands@.";
            mon_msg_seen = "Wisps of smoke drift from @the_monster@'s "
                           "@hands@.";
            break;
        case 3:
            you_msg = "You feel a strange surge of energy!";
            // Monster messages needed.
            break;
        case 4:
            you_msg      = "You are momentarily dazzled by a flash of light!";
            mon_msg_seen = "@The_monster@ emits a flash of light!";
            break;
        case 5:
            you_msg      = "Strange energies run through your body.";
            mon_msg_seen = "@The_monster@ glows " + weird_glowing_colour() +
                           " for a moment.";
            break;
        case 6:
            you_msg      = "Your skin tingles.";
            mon_msg_seen = "@The_monster@ twitches.";
            break;
        case 7:
            you_msg      = "Your skin glows momentarily.";
            mon_msg_seen = "@The_monster@ glows momentarily.";
            // A small glow isn't going to make it past invisibility.
            break;
        case 8:
            // Set nothing; canned_msg(MSG_NOTHING_HAPPENS) will be taken
            // care of elsewhere.
            break;
        case 9:
            if (player_can_smell())
                all_msg = "You smell something strange.";
            else if (you.species == SP_MUMMY)
                you_msg = "Your bandages flutter.";
            break;
        case 10:
        {
            // Player only (for now).
            bool plural;
            std::string hair = _hair_str(target, plural);
            you_msg = make_stringf("Your %s stand%s on end.", hair.c_str(),
                                   plural ? "" : "s");
        }
        }
        do_msg();
        break;

    case 1:         // a bit less harmless stuff
        switch (random2(2))
        {
        case 0:
            you_msg        = "Smoke pours from your @hands@!";
            mon_msg_seen   = "Smoke pours from @the_monster@'s @hands@!";
            mon_msg_unseen = "Smoke appears from out of nowhere!";

            do_msg();
            big_cloud(CLOUD_GREY_SMOKE, kc, kt, target->pos(), 20,
                      7 + random2(7));
            break;
        case 1:
            you_msg      = "A wave of violent energy washes through your body!";
            mon_msg_seen = "@The_monster@ lurches violently!";
            _ouch(6 + random2avg(7, 2));
            break;
        }
        break;

    case 2:         // rather less harmless stuff
        switch (random2(2))
        {
        case 0:
            you_msg      = "Energy rips through your body!";
            mon_msg_seen = "@The_monster@ jerks violently!";
            _ouch(9 + random2avg(17, 2));
            break;
        case 1:
            you_msg        = "You are caught in a violent explosion!";
            mon_msg_seen   = "@The_monster@ is caught in a violent explosion!";
            mon_msg_unseen = "A violent explosion happens from out of thin "
                             "air!";

            beam.flavour = BEAM_MISSILE;
            beam.damage  = dice_def(3, 12);
            beam.name    = "explosion";
            beam.colour  = random_colour();

            _explosion();
            break;
        }
        break;

    case 3:         // considerably less harmless stuff
        switch (random2(2))
        {
        case 0:
            you_msg      = "You are blasted with magical energy!";
            mon_msg_seen = "@The_monster@ is blasted with magical energy!";
            // No message for invis monsters?
            _ouch(12 + random2avg(29, 2));
            break;
        case 1:
            all_msg = "There is a sudden explosion of magical energy!";

            beam.flavour = BEAM_MISSILE;
            beam.type    = dchar_glyph(DCHAR_FIRED_BURST);
            beam.damage  = dice_def(3, 20);
            beam.name    = "explosion";
            beam.colour  = random_colour();
            beam.ex_size = coinflip() ? 1 : 2;

            _explosion();
            break;
        }
    }
}

static void _your_hands_glow(actor* target, std::string& you_msg,
                             std::string& mon_msg_seen, bool pluralize)
{
    you_msg      = "Your @hands@ ";
    mon_msg_seen = "@The_monster@'s @hands@ ";
    // No message for invisible monsters.

    if (pluralize)
    {
       you_msg      += "glow";
       mon_msg_seen += "glow";
    }
    else
    {
        you_msg      += "glows";
        mon_msg_seen += "glows";
    }
    you_msg      += " momentarily.";
    mon_msg_seen += " momentarily.";
}

void MiscastEffect::_enchantment(int severity)
{
    switch (severity)
    {
    case 0:         // harmless messages only
        switch (random2(10))
        {
        case 0:
            _your_hands_glow(target, you_msg, mon_msg_seen, can_plural_hand);
            break;
        case 1:
            you_msg      = "The air around you crackles with energy!";
            mon_msg_seen = "The air around @the_monster@ crackles with"
                           " energy!";
            break;
        case 2:
            you_msg        = "Multicoloured lights dance before your eyes!";
            mon_msg_seen   = "Multicoloured lights dance around @the_monster@!";
            mon_msg_unseen = "Multicoloured lights dance in the air!";
            break;
        case 3:
            you_msg = "You feel a strange surge of energy!";
            // Monster messages needed.
            break;
        case 4:
            you_msg      = "Waves of light ripple over your body.";
            mon_msg_seen = "Waves of light ripple over @the_monster@'s body.";
            break;
        case 5:
            you_msg      = "Strange energies run through your body.";
            mon_msg_seen = "@The_monster@ glows " + weird_glowing_colour() +
                           " for a moment.";
            break;
        case 6:
            you_msg      = "Your skin tingles.";
            mon_msg_seen = "@The_monster@ twitches.";
            break;
        case 7:
            you_msg      = "Your skin glows momentarily.";
            mon_msg_seen = "@The_monster@'s body glows momentarily.";
            break;
        case 8:
            // Set nothing; canned_msg(MSG_NOTHING_HAPPENS) will be taken
            // care of elsewhere.
            break;
        case 9:
            if (neither_end_silenced())
            {
                // XXX: Should use noisy().
                all_msg = "You hear something strange.";
                msg_ch  = MSGCH_SOUND;
                return;
            }
            else if (target->atype() == ACT_PLAYER
                     && you.attribute[ATTR_TRANSFORMATION] != TRAN_AIR)
            {
                you_msg = "Your skull vibrates slightly.";
            }
            break;
        }
        do_msg();
        break;

    case 1:         // slightly annoying
        switch (random2(crawl_state.arena ? 1 : 2))
        {
        case 0:
            _potion_effect(POT_LEVITATION, 20);
            break;
        case 1:
            // XXX: Something else for monsters?
            random_uselessness();
            break;
        }
        break;

    case 2:         // much more annoying
        switch (random2(7))
        {
        case 0:
        case 1:
        case 2:
            if (target->atype() == ACT_PLAYER)
            {
                mpr("You sense a malignant aura.");
                curse_an_item(false);
                break;
            }
            // Intentional fall-through for monsters.
        case 3:
        case 4:
        case 5:
            _potion_effect(POT_SLOWING, 10);
            break;
        case 6:
            _potion_effect(POT_BERSERK_RAGE, 10);
            break;
        }
        break;

    case 3:         // potentially lethal
        // Only use first two cases for monsters.
        switch (random2(target->atype() == ACT_PLAYER ? 4 : 2))
        {
        case 0:
            _potion_effect(POT_PARALYSIS, 10);
            break;
        case 1:
            _potion_effect(POT_CONFUSION, 10);
            break;
        case 2:
            mpr("You feel saturated with unharnessed energies!");
            you.magic_contamination += random2avg(19, 3);
            break;
        case 3:
            do
                curse_an_item(false);
            while (!one_chance_in(3));
            mpr("You sense an overwhelmingly malignant aura!");
            break;
        }
        break;
    }
}

void MiscastEffect::_translocation(int severity)
{
    switch (severity)
    {
    case 0:         // harmless messages only
        switch (random2(10))
        {
        case 0:
            you_msg      = "Space warps around you.";
            mon_msg_seen = "Space warps around @the_monster@.";
            break;
        case 1:
            you_msg      = "The air around you crackles with energy!";
            mon_msg_seen = "The air around @the_monster@ crackles with "
                           "energy!";
            break;
        case 2:
            you_msg      = "You feel a wrenching sensation.";
            mon_msg_seen = "@The monster@ jerks violently for a moment.";
            break;
        case 3:
            you_msg = "You feel a strange surge of energy!";
            // Monster messages needed.
            break;
        case 4:
            you_msg      = "You spin around.";
            mon_msg_seen = "@The_monster@ spins around.";
            break;
        case 5:
            you_msg      = "Strange energies run through your body.";
            mon_msg_seen = "@The_monster@ glows " + weird_glowing_colour() +
                           " for a moment.";
            break;
        case 6:
            you_msg      = "Your skin tingles.";
            mon_msg_seen = "@The_monster@ twitches.";
            break;
        case 7:
            you_msg      = "The world appears momentarily distorted!";
            mon_msg_seen = "@The_monster@ appears momentarily distorted!";
            break;
        case 8:
            // Set nothing; canned_msg(MSG_NOTHING_HAPPENS) will be taken
            // care of elsewhere.
            break;
        case 9:
            you_msg = "You feel uncomfortable.";
            mon_msg_seen = "@The_monster@ grimaces.";
            break;
        }
        do_msg();
        break;

    case 1:         // mostly harmless
        switch (random2(6))
        {
        case 0:
        case 1:
        case 2:
            you_msg        = "You are caught in a localised field of spatial "
                             "distortion.";
            mon_msg_seen   = "@The_monster@ is caught in a localised field of "
                             "spatial distortion.";
            mon_msg_unseen = "A piece of empty space twists and distorts.";
            _ouch(4 + random2avg(9, 2));
            break;
        case 3:
        case 4:
            you_msg        = "Space bends around you!";
            mon_msg_seen   = "Space bends around @the_monster@!";
            mon_msg_unseen = "A piece of empty space twists and distorts.";
            if (_ouch(4 + random2avg(7, 2)) && target->alive())
                target->blink(false);
            break;
        case 5:
            if (_create_monster(MONS_SPATIAL_VORTEX, 3))
                all_msg = "Space twists in upon itself!";
            do_msg();
            break;
        }
        break;

    case 2:         // less harmless
        switch (random2(7))
        {
        case 0:
        case 1:
        case 2:
            you_msg        = "You are caught in a strong localised spatial "
                             "distortion.";
            mon_msg_seen   = "@The_monster@ is caught in a strong localised "
                             "spatial distortion.";
            mon_msg_unseen = "A piece of empty space twists and writhes.";
            _ouch(9 + random2avg(23, 2));
            break;
        case 3:
        case 4:
            you_msg        = "Space warps around you!";
            mon_msg_seen   = "Space warps around @the_monster!";
            mon_msg_unseen = "A piece of empty space twists and writhes.";
            if (_ouch(5 + random2avg(9, 2)) && target->alive())
            {
                if (one_chance_in(3))
                    target->teleport(true);
                else
                    target->blink(false);
                _potion_effect(POT_CONFUSION, 40);
            }
            break;
        case 5:
        {
            bool success = false;

            for (int i = 1 + random2(3); i >= 0; --i)
            {
                if (_create_monster(MONS_SPATIAL_VORTEX, 3))
                    success = true;
            }

            if (success)
                all_msg = "Space twists in upon itself!";
            break;
        }
        case 6:
            send_abyss();
            break;
        }
        break;

    case 3:         // much less harmless
        // Don't use the last case for monsters.
        switch (random2(target->atype() == ACT_PLAYER ? 4 : 3))
        {
        case 0:
            you_msg        = "You are caught in an extremely strong localised "
                             "spatial distortion!";
            mon_msg_seen   = "@The_monster@ is caught in an extremely strong "
                             "localised spatial distortion!";
            mon_msg_unseen = "A rift temporarily opens in the fabric of space!";
            _ouch(15 + random2avg(29, 2));
            break;
        case 1:
            you_msg        = "Space warps crazily around you!";
            mon_msg_seen   = "Space warps crazily around @the_monster@!";
            mon_msg_unseen = "A rift temporarily opens in the fabric of space!";
            if (_ouch(9 + random2avg(17, 2)) && target->alive())
            {
                target->teleport(true);
                _potion_effect(POT_CONFUSION, 60);
            }
            break;
        case 2:
            send_abyss();
            break;
        case 3:
            mpr("You feel saturated with unharnessed energies!");
            you.magic_contamination += random2avg(19, 3);
            break;
        }
        break;
    }
}

void MiscastEffect::_summoning(int severity)
{
    switch (severity)
    {
    case 0:         // harmless messages only
        switch (random2(10))
        {
        case 0:
            you_msg      = "Shadowy shapes form in the air around you, "
                           "then vanish.";
            mon_msg_seen = "Shadowy shapes form in the air around "
                           "@the_monster@, then vanish.";
            break;
        case 1:
            if (neither_end_silenced())
            {
                all_msg = "You hear strange voices.";
                msg_ch  = MSGCH_SOUND;
            }
            else if (target->atype() == ACT_PLAYER)
                you_msg = "You feel momentarily dizzy.";
            break;
        case 2:
            you_msg = "Your head hurts.";
            // Monster messages needed.
            break;
        case 3:
            you_msg = "You feel a strange surge of energy!";
            // Monster messages needed.
            break;
        case 4:
            you_msg = "Your brain hurts!";
            // Monster messages needed.
            break;
        case 5:
            you_msg      = "Strange energies run through your body.";
            mon_msg_seen = "@The_monster@ glows " + weird_glowing_colour() +
                           " for a moment.";
            break;
        case 6:
            you_msg      = "The world appears momentarily distorted.";
            mon_msg_seen = "@The_monster@ appears momentarily distorted.";
            break;
        case 7:
            you_msg      = "Space warps around you.";
            mon_msg_seen = "Space warps around @the_monster@.";
            break;
        case 8:
            // Set nothing; canned_msg(MSG_NOTHING_HAPPENS) will be taken
            // care of elsewhere.
            break;
        case 9:
            if (neither_end_silenced())
            {
                you_msg      = "Distant voices call out to you!";
                mon_msg_seen = "Distant voices call out to @the_monster@!";
                msg_ch       = MSGCH_SOUND;
            }
            else if (target->atype() == ACT_PLAYER)
                you_msg = "You feel watched.";
            break;
        }
        do_msg();
        break;

    case 1:         // a little bad
        switch (random2(6))
        {
        case 0:             // identical to translocation
        case 1:
        case 2:
            you_msg        = "You are caught in a localised spatial "
                             "distortion.";
            mon_msg_seen   = "@The_monster@ is caught in a localised spatial "
                             "distortion.";
            mon_msg_unseen = "An empty piece of space distorts and twists.";
            _ouch(5 + random2avg(9, 2));
            break;
        case 3:
            if (_create_monster(MONS_SPATIAL_VORTEX, 3))
                all_msg = "Space twists in upon itself!";
            do_msg();
            break;
        case 4:
        case 5:
            if (_create_monster(summon_any_demon(DEMON_LESSER), 5, true))
                all_msg = "Something appears in a flash of light!";
            do_msg();
            break;
        }
        break;

    case 2:         // more bad
        switch (random2(6))
        {
        case 0:
        {
            bool success = false;

            for (int i = 1 + random2(3); i >= 0; --i)
            {
                if (_create_monster(MONS_SPATIAL_VORTEX, 3))
                    success = true;
            }

            if (success)
                all_msg = "Space twists in upon itself!";
            do_msg();
            break;
        }

        case 1:
        case 2:
            if (_create_monster(summon_any_demon(DEMON_COMMON), 5, true))
                all_msg = "Something forms from out of thin air!";
            do_msg();
            break;

        case 3:
        case 4:
        case 5:
        {
            bool success = false;

            for (int i = 1 + random2(2); i >= 0; --i)
            {
                if (_create_monster(summon_any_demon(DEMON_LESSER), 5, true))
                    success = true;
            }

            if (success && neither_end_silenced())
            {
                you_msg = "A chorus of chattering voices calls out to you!";
                mon_msg = "A chorus of chattering voices calls out!";
                msg_ch  = MSGCH_SOUND;
            }
            do_msg();
            break;
        }
        }
        break;

    case 3:         // more bad
        switch (random2(4))
        {
        case 0:
            if (_create_monster(MONS_ABOMINATION_SMALL, 0, true))
                all_msg = "Something forms from out of thin air.";
            do_msg();
            break;

        case 1:
            if (_create_monster(summon_any_demon(DEMON_GREATER), 0, true))
                all_msg = "You sense a hostile presence.";
            do_msg();
            break;

        case 2:
        {
            bool success = false;

            for (int i = 1 + random2(2); i >= 0; --i)
            {
                if (_create_monster(summon_any_demon(DEMON_COMMON), 3, true))
                    success = true;
            }

            if (success)
            {
                you_msg = "Something turns its malign attention towards "
                          "you...";
                mon_msg = "You sense a malign presence.";
            }
            do_msg();
            break;
        }

        case 3:
            send_abyss();
            break;
        }
        break;
    }
}

void MiscastEffect::_divination_you(int severity)
{
    switch (severity)
    {
    case 0:         // just a harmless message
        switch (random2(10))
        {
        case 0:
            mpr("Weird images run through your mind.");
            break;
        case 1:
            if (!silenced(you.pos()))
                mpr("You hear strange voices.", MSGCH_SOUND);
            else
                mpr("Your nose twitches.");
            break;
        case 2:
            mpr("Your head hurts.");
            break;
        case 3:
            mpr("You feel a strange surge of energy!");
            break;
        case 4:
            mpr("Your brain hurts!");
            break;
        case 5:
            mpr("Strange energies run through your body.");
            break;
        case 6:
            mpr("Everything looks hazy for a moment.");
            break;
        case 7:
            mpr("You seem to have forgotten something, but you can't remember what it was!");
            break;
        case 8:
            canned_msg(MSG_NOTHING_HAPPENS);
            break;
        case 9:
            mpr("You feel uncomfortable.");
            break;
        }
        break;

    case 1:         // more annoying things
        switch (random2(2))
        {
        case 0:
            mpr("You feel slightly disoriented.");
            forget_map(10 + random2(10));
            break;
        case 1:
            potion_effect(POT_CONFUSION, 10);
            break;
        }
        break;

    case 2:         // even more annoying things
        switch (random2(2))
        {
        case 0:
            if (you.is_undead)
                mpr("You suddenly recall your previous life!");
            else if (_lose_stat(STAT_INTELLIGENCE, 1 + random2(3)))
            {
                mpr("You have damaged your brain!");
            }
            else if (!did_msg)
                mpr("You have a terrible headache.");
            break;
        case 1:
            mpr("You feel lost.");
            forget_map(40 + random2(40));
            break;
        }

        potion_effect(POT_CONFUSION, 1);  // common to all cases here {dlb}
        break;

    case 3:         // nasty
        switch (random2(3))
        {
        case 0:
            mpr(forget_spell() ? "You have forgotten a spell!"
                               : "You get a splitting headache.");
            break;
        case 1:
            mpr("You feel completely lost.");
            forget_map(100);
            break;
        case 2:
            if (you.is_undead)
                mpr("You suddenly recall your previous life.");
            else if (_lose_stat(STAT_INTELLIGENCE, 3 + random2(3)))
            {
                mpr("You have damaged your brain!");
            }
            else if (!did_msg)
                mpr("You have a terrible headache.");
            break;
        }

        potion_effect(POT_CONFUSION, 1);  // common to all cases here {dlb}
        break;
    }
}

// XXX: Monster divination miscasts.
void MiscastEffect::_divination_mon(int severity)
{
    switch (severity)
    {
    case 0:         // just a harmless message
        mon_msg_seen = "@The_monster@ looks momentarily confused.";
        break;

    case 1:         // more annoying things
        switch (random2(2))
        {
        case 0:
            mon_msg_seen = "@The_monster@ looks slightly disoriented.";
            break;
        case 1:
            target->confuse(
                act_source,
                1 + random2(3 + act_source->get_experience_level()));
            break;
        }
        break;

    case 2:         // even more annoying things
        mon_msg_seen = "@The_monster@ shudders.";
        target->confuse(
            act_source,
            5 + random2(3 + act_source->get_experience_level()));
        break;

    case 3:         // nasty
        mon_msg_seen = "@The_monster@ reels.";
        if (one_chance_in(7))
            target_as_monster()->forget_random_spell();
        target->confuse(
            act_source,
            8 + random2(3 + act_source->get_experience_level()));
        break;
    }
}

void MiscastEffect::_necromancy(int severity)
{
    if (target->atype() == ACT_PLAYER && you.religion == GOD_KIKUBAAQUDGHA
        && !player_under_penance() && you.piety >= piety_breakpoint(1)
        && x_chance_in_y(you.piety, 150))
    {
        canned_msg(MSG_NOTHING_HAPPENS);
        return;
    }

    switch (severity)
    {
    case 0:
        switch (random2(10))
        {
        case 0:
            if (player_can_smell())
                all_msg = "You smell decay.";
            else if (you.species == SP_MUMMY)
                you_msg = "Your bandages flutter.";
            break;
        case 1:
            if (neither_end_silenced())
            {
                all_msg = "You hear strange and distant voices.";
                msg_ch  =  MSGCH_SOUND;
            }
            else if (target->atype() == ACT_PLAYER)
                you_msg = "You feel homesick.";
            break;
        case 2:
            you_msg = "Pain shoots through your body.";
            // Monster messages needed.
            break;
        case 3:
            you_msg = "Your bones ache.";
            // Monster messages needed.
            break;
        case 4:
            you_msg      = "The world around you seems to dim momentarily.";
            mon_msg_seen = "@The_monster@ seems to dim momentarily.";
            break;
        case 5:
            you_msg      = "Strange energies run through your body.";
            mon_msg_seen = "@The_monster@ glows " + weird_glowing_colour() +
                           " for a moment.";
            break;
        case 6:
            you_msg      = "You shiver with cold.";
            mon_msg_seen = "@The_monster@ shivers with cold.";
            break;
        case 7:
            you_msg = "You sense a malignant aura.";
            // Monster messages needed.
            break;
        case 8:
            // Set nothing; canned_msg(MSG_NOTHING_HAPPENS) will be taken
            // care of elsewhere.
            break;
        case 9:
            you_msg      = "You feel very uncomfortable.";
            mon_msg_seen = "@The_monster@ grimaces horribly.";
            break;
        }
        do_msg();
        break;

    case 1:         // a bit nasty
        switch (random2(3))
        {
        case 0:
            if (target->res_torment())
            {
                you_msg      = "You feel weird for a moment.";
                mon_msg_seen = "@The_monster@ has an odd expression for a "
                               "moment.";
            }
            else
            {
                you_msg      = "Pain shoots through your body!";
                mon_msg_seen = "@The_monster@ convulses with pain!";
                _ouch(5 + random2avg(15, 2));
            }
            break;
        case 1:
            you_msg      = "You feel horribly lethargic.";
            mon_msg_seen = "@The_monster@ looks incredibly listless.";
            _potion_effect(POT_SLOWING, 15);
            break;
        case 2:
            if (target->res_rotting() == 0)
            {
                if (player_can_smell())
                {
                    // identical to a harmless message
                    all_msg = "You smell decay.";
                }

                if (target->atype() == ACT_PLAYER)
                    you.rotting++;
                else
                    target_as_monster()->add_ench(mon_enchant(ENCH_ROT, 1,
                                                              kc));
            }
            else if (you.species == SP_MUMMY)
            {
                // Monster messages needed.
                you_msg = "Your bandages flutter.";
            }
            break;
        }
        if (!did_msg)
            do_msg();
        break;

    case 2:         // much nastier
        switch (random2(3))
        {
        case 0:
        {
            bool success = false;

            for (int i = random2(3); i >= 0; --i)
            {
                if (_create_monster(MONS_SHADOW, 2, true))
                    success = true;
            }

            if (success)
            {
                you_msg        = "Flickering shadows surround you.";
                mon_msg_seen   = "Flickering shadows surround @the_monster@.";
                mon_msg_unseen = "Shadows flicker in the thin air.";
            }
            do_msg();
            break;
        }

        case 1:
            you_msg      = "You are engulfed in negative energy!";
            mon_msg_seen = "@The_monster@ is engulfed in negative energy!";

            if (lethality_margin == 0 || you.experience > 0
                || !avoid_lethal(you.hp))
            {
                if (one_chance_in(3))
                {
                    do_msg();
                    target->drain_exp(act_source);
                    break;
                }
            }

            // If draining failed, just flow through...

        case 2:
            if (target->res_torment())
            {
                you_msg      = "You feel weird for a moment.";
                mon_msg_seen = "@The_monster@ has an odd expression for a "
                               "moment.";
            }
            else
            {
                you_msg      = "You convulse helplessly as pain tears through "
                               "your body!";
                mon_msg_seen = "@The_monster@ convulses helplessly with pain!";
                _ouch(15 + random2avg(23, 2));
            }
            if (!did_msg)
                do_msg();
            break;
        }
        break;

    case 3:         // even nastier
        // Don't use last case for monsters.
        switch (random2(target->atype() == ACT_PLAYER ? 6 : 5))
        {
        case 0:
            if (target->holiness() == MH_UNDEAD)
            {
                you_msg      = "Something just walked over your grave. No, "
                               "really!";
                mon_msg_seen = "@The_monster@ seems frightened for a moment.";
                do_msg();
            }
            else
                torment_monsters(target->pos(), 0, TORMENT_GENERIC);
            break;

        case 1:
            target->rot(act_source, random2avg(7, 2) + 1);
            break;

        case 2:
            if (_create_monster(MONS_SOUL_EATER, 4, true))
            {
                you_msg        = "Something reaches out for you...";
                mon_msg_seen   = "Something reaches out for @the_monster@...";
                mon_msg_unseen = "Something reaches out from thin air...";
            }
            do_msg();
            break;

        case 3:
            if (_create_monster(MONS_REAPER, 4, true))
            {
                you_msg        = "Death has come for you...";
                mon_msg_seen   = "Death has come for @the_monster@...";
                mon_msg_unseen = "Death appears from thin air...";
            }
            do_msg();
            break;

        case 4:
            you_msg      = "You are engulfed in negative energy!";
            mon_msg_seen = "@The_monster@ is engulfed in negative energy!";

            if (lethality_margin == 0 || you.experience > 0
                || !avoid_lethal(you.hp))
            {
                do_msg();
                target->drain_exp(act_source);
                break;
            }

            // If draining failed, just flow through if it's the player...
            if (target->atype() == ACT_MONSTER)
                break;

        case 5:
            _lose_stat(STAT_RANDOM, 1 + random2avg(7, 2));
            break;
        }
        break;
    }
}

void MiscastEffect::_transmutation(int severity)
{
    int num;
    switch (severity)
    {
    case 0:         // just a harmless message
        num = 10 + (_has_hair(target) ? 1 : 0);
        switch (random2(num))
        {
        case 0:
            _your_hands_glow(target, you_msg, mon_msg_seen, can_plural_hand);
            break;
        case 1:
            you_msg        = "The air around you crackles with energy!";
            mon_msg_seen   = "The air around @the_monster@ crackles with "
                             "energy!";
            mon_msg_unseen = "The thin air crackles with energy!";
            break;
        case 2:
            you_msg        = "Multicoloured lights dance before your eyes!";
            mon_msg_seen   = "Multicoloured lights dance around @the_monster@!";
            mon_msg_unseen = "Multicoloured lights dance in the air!";
            break;
        case 3:
            you_msg = "You feel a strange surge of energy!";
            // Monster messages needed.
            break;
        case 4:
            you_msg        = "Waves of light ripple over your body.";
            mon_msg_seen   = "Waves of light ripple over @the_monster@'s body.";
            mon_msg_unseen = "Waves of light ripple in the air.";
            break;
        case 5:
            you_msg      = "Strange energies run through your body.";
            mon_msg_seen = "@The_monster@ glows " + weird_glowing_colour() +
                           " for a moment.";
            break;
        case 6:
            you_msg      = "Your skin tingles.";
            mon_msg_seen = "@The_monster@ twitches.";
            break;
        case 7:
            you_msg      = "Your skin glows momentarily.";
            mon_msg_seen = "@The_monster@'s body glows momentarily.";
            break;
        case 8:
            // Set nothing; canned_msg(MSG_NOTHING_HAPPENS) will be taken
            // care of elsewhere.
            break;
        case 9:
            if (player_can_smell())
                all_msg = "You smell something strange.";
            else if (you.species == SP_MUMMY)
                you_msg = "Your bandages flutter.";
            break;
        case 10:
        {
            // Player only (for now).
            bool plural;
            std::string hair = _hair_str(target, plural);
            you_msg = make_stringf("Your %s momentarily turn%s into snakes!",
                                   hair.c_str(), plural ? "" : "s");
        }
        }
        do_msg();
        break;

    case 1:         // slightly annoying
        switch (random2(crawl_state.arena ? 1 : 2))
        {
        case 0:
            you_msg      = "Your body is twisted painfully.";
            mon_msg_seen = "@The_monster@'s body twists unnaturally.";
            _ouch(1 + random2avg(11, 2));
            break;
        case 1:
            random_uselessness();
            break;
        }
        break;

    case 2:         // much more annoying
        // Last case for players only.
        switch (random2(target->atype() == ACT_PLAYER ? 4 : 3))
        {
        case 0:
            you_msg      = "Your body is twisted very painfully!";
            mon_msg_seen = "@The_monster@'s body twists and writhes.";
            _ouch(3 + random2avg(23, 2));
            break;
        case 1:
            _potion_effect(POT_PARALYSIS, 10);
            break;
        case 2:
            _potion_effect(POT_CONFUSION, 10);
            break;
        case 3:
            mpr("You feel saturated with unharnessed energies!");
            you.magic_contamination += random2avg(19, 3);
            break;
        }
        break;

    case 3:         // even nastier
        if (target->atype() == ACT_MONSTER)
            target->mutate(); // Polymorph the monster, if possible.

        switch (random2(3))
        {
        case 0:
            you_msg = "Your body is flooded with distortional energies!";
            mon_msg = "@The_monster@'s body is flooded with distortional "
                      "energies!";
            if (_ouch(3 + random2avg(18, 2)) && target->alive())
            {
                if (target->atype() == ACT_PLAYER)
                    you.magic_contamination += random2avg(35, 3);
            }
            break;

        case 1:
            // HACK: Avoid lethality before deleting mutation, since
            // afterwards a message would already have been given.
            if (lethality_margin > 0
                && (you.hp - lethality_margin) <= 27
                && avoid_lethal(you.hp))
            {
                return;
            }

            if (target->atype() == ACT_PLAYER)
            {
                you_msg = "You feel very strange.";
                delete_mutation(RANDOM_MUTATION, true, false,
                                lethality_margin > 0);
            }
            _ouch(5 + random2avg(23, 2));
            break;

        case 2:
            // HACK: Avoid lethality before giving mutation, since
            // afterwards a message would already have been given.
            if (lethality_margin > 0
                && (you.hp - lethality_margin) <= 27
                && avoid_lethal(you.hp))
            {
                return;
            }

            if (target->atype() == ACT_PLAYER)
            {
                you_msg = "Your body is distorted in a weirdly horrible way!";
                const bool failMsg = !give_bad_mutation(true, false,
                                                        lethality_margin > 0);
                if (coinflip())
                    give_bad_mutation(failMsg, false, lethality_margin > 0);
            }
            _ouch(5 + random2avg(23, 2));
            break;
        }
        break;
    }
}

void MiscastEffect::_fire(int severity)
{
    switch (severity)
    {
    case 0:         // just a harmless message
        switch (random2(10))
        {
        case 0:
            you_msg      = "Sparks fly from your @hands@!";
            mon_msg_seen = "Sparks fly from @the_monster@'s @hands@!";
            break;
        case 1:
            you_msg      = "The air around you burns with energy!";
            mon_msg_seen = "The air around @the_monster@ burns with energy!";
            break;
        case 2:
            you_msg      = "Wisps of smoke drift from your @hands@.";
            mon_msg_seen = "Wisps of smoke drift from @the_monster@'s @hands@.";
            break;
        case 3:
            you_msg = "You feel a strange surge of energy!";
            // Monster messages needed.
            break;
        case 4:
            if (player_can_smell())
                all_msg = "You smell smoke.";
            else if (you.species == SP_MUMMY)
                you_msg = "Your bandages flutter.";
            break;
        case 5:
            you_msg = "Heat runs through your body.";
            // Monster messages needed.
            break;
        case 6:
            you_msg = "You feel uncomfortably hot.";
            // Monster messages needed.
            break;
        case 7:
            you_msg      = "Lukewarm flames ripple over your body.";
            mon_msg_seen = "Dim flames ripple over @the_monster@'s body.";
            break;
        case 8:
            // Set nothing; canned_msg(MSG_NOTHING_HAPPENS) will be taken
            // care of elsewhere.
            break;
        case 9:
            if (neither_end_silenced())
            {
                all_msg = "You hear a sizzling sound.";
                msg_ch  = MSGCH_SOUND;
            }
            else if (target->atype() == ACT_PLAYER)
                you_msg = "You feel like you have heartburn.";
            break;
        }
        do_msg();
        break;

    case 1:         // a bit less harmless stuff
        switch (random2(2))
        {
        case 0:
            you_msg        = "Smoke pours from your @hands@!";
            mon_msg_seen   = "Smoke pours from @the_monster@'s @hands@!";
            mon_msg_unseen = "Smoke appears out of nowhere!";

            do_msg();
            big_cloud(random_smoke_type(), kc, kt, target->pos(), 20,
                      7 + random2(7));
            break;

        case 1:
            you_msg      = "Flames sear your flesh.";
            mon_msg_seen = "Flames sear @the_monster@.";
            if (target->res_fire() < 0)
            {
                if (!_ouch(2 + random2avg(13, 2)))
                    return;
            }
            else
                do_msg();
            if (target->alive())
                target->expose_to_element(BEAM_FIRE, 3);
            break;
        }
        break;

    case 2:         // rather less harmless stuff
        switch (random2(2))
        {
        case 0:
            you_msg        = "You are blasted with fire.";
            mon_msg_seen   = "@The_monster@ is blasted with fire.";
            mon_msg_unseen = "A flame briefly burns in thin air.";
            if (_ouch(5 + random2avg(29, 2), BEAM_FIRE) && target->alive())
                target->expose_to_element(BEAM_FIRE, 5);
            break;

        case 1:
            you_msg        = "You are caught in a fiery explosion!";
            mon_msg_seen   = "@The_monster@ is caught in a fiery explosion!";
            mon_msg_unseen = "Fire explodes from out of thin air!";

            beam.flavour = BEAM_FIRE;
            beam.type    = dchar_glyph(DCHAR_FIRED_BURST);
            beam.damage  = dice_def(3, 14);
            beam.name    = "explosion";
            beam.colour  = RED;

            _explosion();
            break;
        }
        break;

    case 3:         // considerably less harmless stuff
        switch (random2(3))
        {
        case 0:
            you_msg        = "You are blasted with searing flames!";
            mon_msg_seen   = "@The_monster@ is blasted with searing flames!";
            mon_msg_unseen = "A large flame burns hotly for a moment in the "
                             "thin air.";
            if (_ouch(9 + random2avg(33, 2), BEAM_FIRE) && target->alive())
                target->expose_to_element(BEAM_FIRE, 10);
            break;
        case 1:
            all_msg = "There is a sudden and violent explosion of flames!";

            beam.flavour = BEAM_FIRE;
            beam.type    = dchar_glyph(DCHAR_FIRED_BURST);
            beam.damage  = dice_def(3, 20);
            beam.name    = "fireball";
            beam.colour  = RED;
            beam.ex_size = coinflip() ? 1 : 2;

            _explosion();
            break;

        case 2:
        {
            you_msg      = "You are covered in liquid flames!";
            mon_msg_seen = "@The_monster@ is covered in liquid flames!";
            do_msg();

            if (target->atype() == ACT_PLAYER)
                you.duration[DUR_LIQUID_FLAMES] += random2avg(7, 3) + 1;
            else
            {
                monsters* mon_target = target_as_monster();
                mon_target->add_ench(mon_enchant(ENCH_STICKY_FLAME,
                    std::min(4, 1 + random2(mon_target->hit_dice) / 2), kc));
            }
            break;
        }
        }
        break;
    }
}

void MiscastEffect::_ice(int severity)
{
    const dungeon_feature_type feat = grd(target->pos());

    const bool frostable_feat =
        (feat == DNGN_FLOOR || grid_altar_god(feat) != GOD_NO_GOD
         || grid_is_staircase(feat) || grid_is_water(feat));

    const std::string feat_name = (feat == DNGN_FLOOR ? "the " : "") +
        feature_description(target->pos(), false, DESC_NOCAP_THE);

    int num;
    switch (severity)
    {
    case 0:         // just a harmless message
        num = 10 + (frostable_feat ? 1 : 0);
        switch (random2(num))
        {
        case 0:
            you_msg      = "You shiver with cold.";
            mon_msg_seen = "@The_monster@ shivers with cold.";
            break;
        case 1:
            you_msg = "A chill runs through your body.";
            // Monster messages needed.
            break;
        case 2:
            you_msg        = "Wisps of condensation drift from your @hands@.";
            mon_msg_seen   = "Wisps of condensation drift from @the_monster@'s "
                             "@hands@.";
            mon_msg_unseen = "Wisps of condensation drift in the air.";
            break;
        case 3:
            you_msg = "You feel a strange surge of energy!";
            // Monster messages needed.
            break;
        case 4:
            you_msg      = "Your @hands@ feel@hand_conj@ numb with cold.";
            // Monster messages needed.
            break;
        case 5:
            you_msg = "A chill runs through your body.";
            // Monster messages needed.
            break;
        case 6:
            you_msg = "You feel uncomfortably cold.";
            // Monster messages needed.
            break;
        case 7:
            you_msg      = "Frost covers your body.";
            mon_msg_seen = "Frost covers @the_monster@'s body.";
            break;
        case 8:
            // Set nothing; canned_msg(MSG_NOTHING_HAPPENS) will be taken
            // care of elsewhere.
            break;
        case 9:
            if (neither_end_silenced())
            {
                all_msg = "You hear a crackling sound.";
                msg_ch  = MSGCH_SOUND;
            }
            else if (target->atype() == ACT_PLAYER)
                you_msg = "A snowflake lands on your nose.";
            break;
        case 10:
            if (grid_is_water(feat))
                all_msg  = "A thin layer of ice forms on " + feat_name;
            else
                all_msg  = "Frost spreads across " + feat_name;
            break;
        }
        do_msg();
        break;

    case 1:         // a bit less harmless stuff
        switch (random2(2))
        {
        case 0:
            mpr("You feel extremely cold.");
            // Monster messages needed.
            break;
        case 1:
            you_msg      = "You are covered in a thin layer of ice.";
            mon_msg_seen = "@The_monster@ is covered in a thin layer of ice.";
            if (target->res_cold() < 0)
            {
                if (!_ouch(4 + random2avg(5, 2)))
                    return;
            }
            else
                do_msg();
            if (target->alive())
                target->expose_to_element(BEAM_COLD, 2);
            break;
        }
        break;

    case 2:         // rather less harmless stuff
        switch (random2(2))
        {
        case 0:
            you_msg = "Heat is drained from your body.";
            // Monster messages needed.
            if (_ouch(5 + random2(6) + random2(7), BEAM_COLD) && target->alive())
                target->expose_to_element(BEAM_COLD, 4);
            break;

        case 1:
            you_msg        = "You are caught in an explosion of ice and frost!";
            mon_msg_seen   = "@The_monster@ is caught in an explosion of "
                             "ice and frost!";
            mon_msg_unseen = "Ice and frost explode from out of thin air!";

            beam.flavour = BEAM_COLD;
            beam.type    = dchar_glyph(DCHAR_FIRED_BURST);
            beam.damage  = dice_def(3, 11);
            beam.name    = "explosion";
            beam.colour  = WHITE;

            _explosion();
            break;
        }
        break;

    case 3:         // less harmless stuff
        switch (random2(2))
        {
        case 0:
            you_msg      = "You are blasted with ice!";
            mon_msg_seen = "@The_monster@ is blasted with ice!";
            if (_ouch(9 + random2avg(23, 2), BEAM_ICE) && target->alive())
                target->expose_to_element(BEAM_COLD, 9);
            break;
        case 1:
            you_msg        = "Freezing gasses pour from your @hands@!";
            mon_msg_seen   = "Freezing gasses pour from @the_monster@'s "
                             "@hands@!";

            do_msg();
            big_cloud(CLOUD_COLD, kc, kt, target->pos(), 20, 8 + random2(4));
            break;
        }
        break;
    }
}

static bool _on_floor(actor* target)
{
    return (!target->airborne() && grd(target->pos()) == DNGN_FLOOR);
}

void MiscastEffect::_earth(int severity)
{
    int num;
    switch (severity)
    {
    case 0:         // just a harmless message
    case 1:
        num = 11 + (_on_floor(target) ? 2 : 0);
        switch (random2(num))
        {
        case 0:
            you_msg = "You feel earthy.";
            // Monster messages needed.
            break;
        case 1:
            you_msg        = "You are showered with tiny particles of grit.";
            mon_msg_seen   = "@The_monster@ is showered with tiny particles "
                             "of grit.";
            mon_msg_unseen = "Tiny particles of grit hang in the air for a "
                             "moment.";
            break;
        case 2:
            you_msg        = "Sand pours from your @hands@.";
            mon_msg_seen   = "Sand pours from @the_monster@'s @hands@.";
            mon_msg_unseen = "Sand pours from out of thin air.";
            break;
        case 3:
            you_msg = "You feel a surge of energy from the ground.";
            // Monster messages needed.
            break;
        case 4:
            if (neither_end_silenced())
            {
                all_msg = "You hear a distant rumble.";
                msg_ch  = MSGCH_SOUND;
            }
            else if (target->atype() == ACT_PLAYER)
                you_msg = "You sympathise with the stones.";
            break;
        case 5:
            you_msg = "You feel gritty.";
            // Monster messages needed.
            break;
        case 6:
            you_msg      = "You feel momentarily lethargic.";
            mon_msg_seen = "@The_monster@ looks momentarily listless.";
            break;
        case 7:
            you_msg        = "Motes of dust swirl before your eyes.";
            mon_msg_seen   = "Motes of dust swirl around @the_monster@.";
            mon_msg_unseen = "Motes of dust swirl around in the air.";
            break;
        case 8:
            // Set nothing; canned_msg(MSG_NOTHING_HAPPENS) will be taken
            // care of elsewhere.
            break;
        case 9:
        {
            bool               pluralized = true;
            std::string        feet       = you.foot_name(true, &pluralized);
            std::ostringstream str;

            str << "Your " << feet << (pluralized ? " feel" : " feels")
                << " warm.";

            you_msg = str.str();
            // Monster messages needed.
            break;
        }
        case 10:
            if (target->cannot_move())
            {
                you_msg      = "You briefly vibrate.";
                mon_msg_seen = "@The_monster@ briefly vibrates.";
            }
            else
            {
                you_msg      = "You momentarily stiffen.";
                mon_msg_seen = "@The_monster@ momentarily stiffens.";
            }
            break;
        case 11:
            all_msg = "The floor vibrates.";
            break;
        case 12:
            all_msg = "The floor shifts beneath you alarmingly!";
            break;
        }
        do_msg();
        break;

    case 2:         // slightly less harmless stuff
        switch (random2(1))
        {
        case 0:
            switch (random2(3))
            {
            case 0:
                you_msg        = "You are hit by flying rocks!";
                mon_msg_seen   = "@The_monster@ is hit by flying rocks!";
                mon_msg_unseen = "Flying rocks appear out of thin air!";
                break;
            case 1:
                you_msg        = "You are blasted with sand!";
                mon_msg_seen   = "@The_monster@ is blasted with sand!";
                mon_msg_unseen = "A miniature sandstorm briefly appears!";
                break;
            case 2:
                you_msg        = "Rocks fall onto you out of nowhere!";
                mon_msg_seen   = "Rocks fall onto @the_monster@ out of "
                                 "nowhere!";
                mon_msg_unseen = "Rocks fall out of nowhere!";
                break;
            }
            _ouch(random2avg(13, 2) + 10 - random2(1 + target->armour_class()));
            break;
        }
        break;

    case 3:         // less harmless stuff
        switch (random2(1))
        {
        case 0:
            you_msg        = "You are caught in an explosion of flying "
                             "shrapnel!";
            mon_msg_seen   = "@The_monster@ is caught in an explosion of "
                             "flying shrapnel!";
            mon_msg_unseen = "Flying shrapnel explodes from thin air!";

            beam.flavour = BEAM_FRAG;
            beam.type    = dchar_glyph(DCHAR_FIRED_BURST);
            beam.damage  = dice_def(3, 15);
            beam.name    = "explosion";
            beam.colour  = CYAN;

            if (one_chance_in(5))
                beam.colour = BROWN;
            if (one_chance_in(5))
                beam.colour = LIGHTCYAN;

            _explosion();
            break;
        }
        break;
    }
}

void MiscastEffect::_air(int severity)
{
    int num;
    switch (severity)
    {
    case 0:         // just a harmless message
        num = 10 + (_has_hair(target) ? 1 : 0);
        switch (random2(num))
        {
        case 0:
            you_msg = "Ouch! You gave yourself an electric shock.";
            // Monster messages needed.
            break;
        case 1:
            you_msg      = "You feel momentarily weightless.";
            mon_msg_seen = "@The_monster@ bobs in the air for a moment.";
            break;
        case 2:
            you_msg      = "Wisps of vapour drift from your @hands@.";
            mon_msg_seen = "Wisps of vapour drift from @the_monster@'s "
                           "@hands@.";
            break;
        case 3:
            you_msg = "You feel a strange surge of energy!";
            // Monster messages needed.
            break;
        case 4:
            you_msg = "You feel electric!";
            // Monster messages needed.
            break;
        case 5:
        {
            bool pluralized = true;
            if (!hand_str.empty())
                pluralized = can_plural_hand;
            else
                target->hand_name(true, &pluralized);

            if (pluralized)
            {
                you_msg      = "Sparks of electricity dance between your "
                               "@hands@.";
                mon_msg_seen = "Sparks of electricity dance between "
                               "@the_monster@'s @hands@.";
            }
            else
            {
                you_msg      = "Sparks of electricity dance over your "
                               "@hand@.";
                mon_msg_seen = "Sparks of electricity dance over "
                               "@the_monster@'s @hand@.";
            }
            break;
        }
        case 6:
            you_msg      = "You are blasted with air!";
            mon_msg_seen = "@The_monster@ is blasted with air!";
            break;
        case 7:
            if (neither_end_silenced())
            {
                all_msg = "You hear a whooshing sound.";
                msg_ch  = MSGCH_SOUND;
            }
            else if (player_can_smell())
                all_msg = "You smell ozone.";
            else if (you.species == SP_MUMMY)
                you_msg = "Your bandages flutter.";
            break;
        case 8:
            // Set nothing; canned_msg(MSG_NOTHING_HAPPENS) will be taken
            // care of elsewhere.
            break;
        case 9:
            if (neither_end_silenced())
            {
                all_msg = "You hear a crackling sound.";
                msg_ch  = MSGCH_SOUND;
            }
            else if (player_can_smell())
                all_msg = "You smell something musty.";
            else if (you.species == SP_MUMMY)
                you_msg = "Your bandages flutter.";
            break;
        case 10:
        {
            // Player only (for now).
            bool plural;
            std::string hair = _hair_str(target, plural);
            you_msg = make_stringf("Your %s stand%s on end.", hair.c_str(),
                                   plural ? "" : "s");
            break;
        }
        }
        do_msg();
        break;
    case 1:         // a bit less harmless stuff
        switch (random2(2))
        {
        case 0:
            you_msg      = "There is a short, sharp shower of sparks.";
            mon_msg_seen = "@The_monster@ is briefly showered in sparks.";
            break;
        case 1:
            if (silenced(you.pos()))
            {
               you_msg        = "The wind whips around you!";
               mon_msg_seen   = "The wind whips around @the_monster@!";
               mon_msg_unseen = "The wind whips!";
            }
            else
            {
               you_msg        = "The wind howls around you!";
               mon_msg_seen   = "The wind howls around @the_monster@!";
               mon_msg_unseen = "The wind howls!";
            }
            break;
        }
        do_msg();
        break;

    case 2:         // rather less harmless stuff
        switch (random2(2))
        {
        case 0:
            you_msg = "Electricity courses through your body.";
            // Monster messages needed.
            _ouch(4 + random2avg(9, 2), BEAM_ELECTRICITY);
            break;
        case 1:
            you_msg        = "Noxious gasses pour from your @hands@!";
            mon_msg_seen   = "Noxious gasses pour from @the_monster@'s "
                             "@hands@!";
            mon_msg_unseen = "Noxious gasses appear from out of thin air!";

            do_msg();
            big_cloud(CLOUD_STINK, kc, kt, target->pos(), 20, 9 + random2(4));
            break;
        }
        break;

    case 3:         // musch less harmless stuff
        switch (random2(2))
        {
        case 0:
            you_msg        = "You are caught in an explosion of electrical "
                             "discharges!";
            mon_msg_seen   = "@The_monster@ is caught in an explosion of "
                             "electrical discharges!";
            mon_msg_unseen = "Electrical discharges explode from out of "
                             "thin air!";

            beam.flavour = BEAM_ELECTRICITY;
            beam.type    = dchar_glyph(DCHAR_FIRED_BURST);
            beam.damage  = dice_def(3, 8);
            beam.name    = "explosion";
            beam.colour  = LIGHTBLUE;
            beam.ex_size = one_chance_in(4) ? 1 : 2;

            _explosion();
            break;
        case 1:
            you_msg        = "Venomous gasses pour from your @hands@!";
            mon_msg_seen   = "Venomous gasses pour from @the_monster@'s "
                             "@hands@!";
            mon_msg_unseen = "Venomous gasses pour forth from the thin air!";

            do_msg();
            big_cloud(CLOUD_POISON, kc, kt, target->pos(), 20, 8 + random2(5));
            break;
        }
    }
}

// XXX MATT
void MiscastEffect::_poison(int severity)
{
    switch (severity)
    {
    case 0:         // just a harmless message
        switch (random2(10))
        {
        case 0:
            you_msg = "You feel mildly nauseous.";
            // Monster messages needed.
            break;
        case 1:
            you_msg = "You feel slightly ill.";
            // Monster messages needed.
            break;
        case 2:
            you_msg      = "Wisps of poison gas drift from your @hands@.";
            mon_msg_seen = "Wisps of poison gas drift from @the_monster@'s "
                           "@hands@.";
            break;
        case 3:
            you_msg = "You feel a strange surge of energy!";
            // Monster messages needed.
            break;
        case 4:
            you_msg = "You feel faint for a moment.";
            // Monster messages needed.
            break;
        case 5:
            you_msg = "You feel sick.";
            // Monster messages needed.
            break;
        case 6:
            you_msg = "You feel odd.";
            // Monster messages needed.
            break;
        case 7:
            you_msg = "You feel weak for a moment.";
            // Monster messages needed.
            break;
        case 8:
            // Set nothing; canned_msg(MSG_NOTHING_HAPPENS) will be taken
            // care of elsewhere.
            break;
        case 9:
            if (neither_end_silenced())
            {
                all_msg = "You hear a slurping sound.";
                msg_ch  = MSGCH_SOUND;
            }
            else if (you.species != SP_MUMMY)
                you_msg = "You taste almonds.";
            break;
        }
        do_msg();
        break;

    case 1:         // a bit less harmless stuff
        switch (random2(2))
        {
        case 0:
            if (target->res_poison() <= 0)
            {
                you_msg = "You feel sick.";
                // Monster messages needed.
                target->poison(act_source, 2 + random2(3));
            }
            do_msg();
            break;

        case 1:
            you_msg        = "Noxious gasses pour from your @hands@!";
            mon_msg_seen   = "Noxious gasses pour from @the_monster@'s "
                             "@hands@!";
            mon_msg_unseen = "Noxious gasses pour forth from the thin air!";
            place_cloud(CLOUD_STINK, target->pos(), 2 + random2(4), kc, kt );
            break;
        }
        break;

    case 2:         // rather less harmless stuff
        // Don't use last case for monsters.
        switch (random2(target->atype() == ACT_PLAYER ? 3 : 2))
        {
        case 0:
            if (target->res_poison() <= 0)
            {
                you_msg = "You feel very sick.";
                // Monster messages needed.
                target->poison(act_source, 3 + random2avg(9, 2));
            }
            do_msg();
            break;

        case 1:
            you_msg        = "Noxious gasses pour from your @hands@!";
            mon_msg_seen   = "Noxious gasses pour from @the_monster@'s "
                             "@hands@!";
            mon_msg_unseen = "Noxious gasses pour forth from the thin air!";

            do_msg();
            big_cloud(CLOUD_STINK, kc, kt, target->pos(), 20, 8 + random2(5));
            break;

        case 2:
            if (player_res_poison())
                canned_msg(MSG_NOTHING_HAPPENS);
            else
                _lose_stat(STAT_RANDOM, 1);
            break;
        }
        break;

    case 3:         // less harmless stuff
        // Don't use last case for monsters.
        switch (random2(target->atype() == ACT_PLAYER ? 3 : 2))
        {
        case 0:
            if (target->res_poison() <= 0)
            {
                you_msg = "You feel incredibly sick.";
                // Monster messages needed.
                target->poison(act_source, 10 + random2avg(19, 2));
            }
            do_msg();
            break;
        case 1:
            you_msg        = "Venomous gasses pour from your @hands@!";
            mon_msg_seen   = "Venomous gasses pour from @the_monster@'s "
                             "@hands@!";
            mon_msg_unseen = "Venomous gasses pour forth from the thin air!";

            do_msg();
            big_cloud(CLOUD_POISON, kc, kt, target->pos(), 20, 7 + random2(7));
            break;
        case 2:
            if (player_res_poison())
                canned_msg(MSG_NOTHING_HAPPENS);
            else
                _lose_stat(STAT_RANDOM, 1 + random2avg(5, 2));
            break;
        }
        break;
    }
}