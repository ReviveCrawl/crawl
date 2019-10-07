/**
 * @file
 * @brief Functions used to print information about various game objects.
**/

#include "AppHdr.h"

#include "describe.h"

#include <algorithm>
#include <cstdio>
#include <cmath>
#include <iomanip>
#include <numeric>
#include <set>
#include <sstream>
#include <string>

#include "ability.h"
#include "adjust.h"
#include "areas.h"
#include "art-enum.h"
#include "artefact.h"
#include "branch.h"
#include "butcher.h"
#include "cloud.h" // cloud_type_name
#include "clua.h"
#include "colour.h"
#include "database.h"
#include "dbg-util.h"
#include "decks.h"
#include "delay.h"
#include "describe-spells.h"
#include "directn.h"
#include "english.h"
#include "env.h"
#include "evoke.h"
#include "fight.h"
#include "food.h"
#include "ghost.h"
#include "god-abil.h"
#include "god-item.h"
#include "hints.h"
#include "invent.h"
#include "item-prop.h"
#include "item-status-flag-type.h"
#include "items.h"
#include "item-use.h"
#include "jobs.h"
#include "lang-fake.h"
#include "libutil.h"
#include "macro.h"
#include "message.h"
#include "mon-book.h"
#include "mon-cast.h" // mons_spell_range
#include "mon-death.h"
#include "mon-tentacle.h"
#include "options.h"
#include "output.h"
#include "prompt.h"
#include "religion.h"
#include "scroller.h"
#include "skills.h"
#include "species.h"
#include "spl-book.h"
#include "spl-summoning.h"
#include "spl-util.h"
#include "spl-wpnench.h"
#include "stash.h"
#include "state.h"
#include "stringutil.h" // to_string on Cygwin
#include "terrain.h"
#ifdef USE_TILE_LOCAL
 #include "tilereg-crt.h"
 #include "tiledef-dngn.h"
#endif
#ifdef USE_TILE
 #include "tiledef-feat.h"
 #include "tilepick.h"
 #include "tileview.h"
 #include "tile-flags.h"
#endif
#include "unicode.h"

#include "i18n-format.h"

using namespace ui;

int count_desc_lines(const string &_desc, const int width)
{
    string desc = get_linebreak_string(_desc, width);
    return count(begin(desc), end(desc), '\n');
}

int show_description(const string &body, const tile_def *tile)
{
    describe_info inf;
    inf.body << body;
    return show_description(inf);
}

/// A message explaining how the player can toggle between quote &
static const formatted_string _toggle_message = formatted_string::parse_string(
    "Press '<w>!</w>'"
#ifdef USE_TILE_LOCAL
    " or <w>Right-click</w>"
#endif
    " to toggle between the description and quote.");

int show_description(const describe_info &inf, const tile_def *tile)
{
    auto vbox = make_shared<Box>(Widget::VERT);

    if (!inf.title.empty())
    {
        auto title_hbox = make_shared<Box>(Widget::HORZ);

#ifdef USE_TILE
        if (tile)
        {
            auto icon = make_shared<Image>();
            icon->set_tile(*tile);
            icon->set_margin_for_sdl({0, 10, 0, 0});
            title_hbox->add_child(move(icon));
        }
#endif

        auto title = make_shared<Text>(inf.title);
        title_hbox->add_child(move(title));

        title_hbox->align_items = Widget::CENTER;
        title_hbox->set_margin_for_sdl({0, 0, 20, 0});
        title_hbox->set_margin_for_crt({0, 0, 1, 0});
        vbox->add_child(move(title_hbox));
    }

    auto switcher = make_shared<Switcher>();

    const string descs[2] =  {
        trimmed_string(process_description(inf, false)),
        trimmed_string(inf.quote),
    };

    for (int i = 0; i < (inf.quote.empty() ? 1 : 2); i++)
    {
        const auto &desc = descs[static_cast<int>(i)];
        auto scroller = make_shared<Scroller>();
        auto fs = formatted_string::parse_string(trimmed_string(desc));
        auto text = make_shared<Text>(fs);
        text->wrap_text = true;
        scroller->set_child(text);
        switcher->add_child(move(scroller));
    }

    switcher->current() = 0;
    switcher->expand_h = false;
#ifdef USE_TILE_LOCAL
    switcher->max_size()[0] = tiles.get_crt_font()->char_width()*80;
#endif
    vbox->add_child(switcher);

    if (!inf.quote.empty())
    {
        auto footer = make_shared<Text>(_toggle_message);
        footer->set_margin_for_sdl({20, 0, 0, 0});
        footer->set_margin_for_crt({1, 0, 0, 0});
        vbox->add_child(move(footer));
    }

    auto popup = make_shared<ui::Popup>(vbox);

    bool done = false;
    int lastch;
    popup->on(Widget::slots.event, [&](wm_event ev) {
        if (ev.type != WME_KEYDOWN)
            return false;
        lastch = ev.key.keysym.sym;
        if (!inf.quote.empty() && (lastch == '!' || lastch == CK_MOUSE_CMD || lastch == '^'))
            switcher->current() = 1 - switcher->current();
        else
            done = !vbox->on_event(ev);
        return true;
    });

#ifdef USE_TILE_WEB
    tiles.json_open_object();
    if (tile)
    {
        tiles.json_open_object("tile");
        tiles.json_write_int("t", tile->tile);
        tiles.json_write_int("tex", tile->tex);
        if (tile->ymax != TILE_Y)
            tiles.json_write_int("ymax", tile->ymax);
        tiles.json_close_object();
    }
    tiles.json_write_string("title", inf.title);
    tiles.json_write_string("prefix", inf.prefix);
    tiles.json_write_string("suffix", inf.suffix);
    tiles.json_write_string("footer", inf.footer);
    tiles.json_write_string("quote", inf.quote);
    tiles.json_write_string("body", inf.body.str());
    tiles.push_ui_layout("describe-generic", 0);
#endif

    ui::run_layout(move(popup), done);

#ifdef USE_TILE_WEB
    tiles.pop_ui_layout();
#endif
    return lastch;
}

string process_description(const describe_info &inf, bool include_title)
{
    string desc;
    if (!inf.prefix.empty())
        desc += "\n\n" + trimmed_string(filtered_lang(inf.prefix));
    if (!inf.title.empty() && include_title)
        desc += "\n\n" + trimmed_string(filtered_lang(inf.title));
    desc += "\n\n" + trimmed_string(filtered_lang(inf.body.str()));
    if (!inf.suffix.empty())
        desc += "\n\n" + trimmed_string(filtered_lang(inf.suffix));
    if (!inf.footer.empty())
        desc += "\n\n" + trimmed_string(filtered_lang(inf.footer));
    trim_string(desc);
    return desc;
}

const char* jewellery_base_ability_string(int subtype)
{
    switch (subtype)
    {
#if TAG_MAJOR_VERSION == 34
    case RING_SUSTAIN_ATTRIBUTES: return "SustAt";
#endif
    case RING_WIZARDRY:           return "Wiz";
    case RING_FIRE:               return TR7("Fire","화염 마술");
    case RING_ICE:                return TR7("Ice","얼음 마술");
    case RING_TELEPORTATION:      return "*Tele";
    case RING_RESIST_CORROSION:   return "rCorr";
#if TAG_MAJOR_VERSION == 34
    case RING_TELEPORT_CONTROL:   return "+cTele";
#endif
    case AMU_HARM:                return "Harm";
    case AMU_MANA_REGENERATION:   return "RegenMP";
    case AMU_THE_GOURMAND:        return "Gourm";
    case AMU_ACROBAT:             return TR7("Acrobat","경쾌한 묘기꾼");
#if TAG_MAJOR_VERSION == 34
    case AMU_CONSERVATION:        return "Cons";
    case AMU_CONTROLLED_FLIGHT:   return "cFly";
#endif
    case AMU_GUARDIAN_SPIRIT:     return "Spirit";
    case AMU_FAITH:               return "Faith";
    case AMU_REFLECTION:          return "Reflect";
    case AMU_INACCURACY:          return "Inacc";
    }
    return "";
}

#define known_proprt(prop) (proprt[(prop)] && known[(prop)])

/// How to display props of a given type?
enum class prop_note
{
    /// The raw numeral; e.g "Slay+3", "Int-1"
    numeral,
    /// Plusses and minuses; "rF-", "rC++"
    symbolic,
    /// Don't note the number; e.g. "rMut"
    plain,
};

struct property_annotators
{
    artefact_prop_type prop;
    prop_note spell_out;
};

static vector<string> _randart_propnames(const item_def& item,
                                         bool no_comma = false)
{
    artefact_properties_t  proprt;
    artefact_known_props_t known;
    artefact_desc_properties(item, proprt, known);

    vector<string> propnames;

    // list the following in rough order of importance
    const property_annotators propanns[] =
    {
        // (Generally) negative attributes
        // These come first, so they don't get chopped off!
        { ARTP_PREVENT_SPELLCASTING,  prop_note::plain },
        { ARTP_PREVENT_TELEPORTATION, prop_note::plain },
        { ARTP_CONTAM,                prop_note::plain },
        { ARTP_ANGRY,                 prop_note::plain },
        { ARTP_CAUSE_TELEPORTATION,   prop_note::plain },
        { ARTP_NOISE,                 prop_note::plain },
        { ARTP_CORRODE,               prop_note::plain },
        { ARTP_DRAIN,                 prop_note::plain },
        { ARTP_SLOW,                  prop_note::plain },
        { ARTP_FRAGILE,               prop_note::plain },

        // Evokable abilities come second
        { ARTP_BLINK,                 prop_note::plain },
        { ARTP_BERSERK,               prop_note::plain },
        { ARTP_INVISIBLE,             prop_note::plain },
        { ARTP_FLY,                   prop_note::plain },

        // Resists, also really important
        { ARTP_ELECTRICITY,           prop_note::plain },
        { ARTP_POISON,                prop_note::plain },
        { ARTP_FIRE,                  prop_note::symbolic },
        { ARTP_COLD,                  prop_note::symbolic },
        { ARTP_NEGATIVE_ENERGY,       prop_note::symbolic },
        { ARTP_MAGIC_RESISTANCE,      prop_note::symbolic },
        { ARTP_REGENERATION,          prop_note::symbolic },
        { ARTP_RMUT,                  prop_note::plain },
        { ARTP_RCORR,                 prop_note::plain },

        // Quantitative attributes
        { ARTP_HP,                    prop_note::numeral },
        { ARTP_MAGICAL_POWER,         prop_note::numeral },
        { ARTP_AC,                    prop_note::numeral },
        { ARTP_EVASION,               prop_note::numeral },
        { ARTP_STRENGTH,              prop_note::numeral },
        { ARTP_INTELLIGENCE,          prop_note::numeral },
        { ARTP_DEXTERITY,             prop_note::numeral },
        { ARTP_SLAYING,               prop_note::numeral },
        { ARTP_SHIELDING,             prop_note::numeral },

        // Qualitative attributes (and Stealth)
        { ARTP_SEE_INVISIBLE,         prop_note::plain },
        { ARTP_STEALTH,               prop_note::symbolic },
        { ARTP_CURSE,                 prop_note::plain },
        { ARTP_CLARITY,               prop_note::plain },
        { ARTP_RMSL,                  prop_note::plain },
    };

    const unrandart_entry *entry = nullptr;
    if (is_unrandom_artefact(item))
        entry = get_unrand_entry(item.unrand_idx);

    // For randart jewellery, note the base jewellery type if it's not
    // covered by artefact_desc_properties()
    if (item.base_type == OBJ_JEWELLERY
        && (item_ident(item, ISFLAG_KNOW_TYPE)))
    {
        const char* type = jewellery_base_ability_string(item.sub_type);
        if (*type)
            propnames.push_back(type);
    }
    else if (item_brand_known(item)
             && !(is_unrandom_artefact(item) && entry
                  && entry->flags & UNRAND_FLAG_SKIP_EGO))
    {
        string ego;
        if (item.base_type == OBJ_WEAPONS)
            ego = weapon_brand_name(item, true);
        else if (item.base_type == OBJ_ARMOUR)
            ego = armour_ego_name(item, true);
        if (!ego.empty())
        {
            // XXX: Ugly hack for adding a comma if needed.
            bool extra_props = false;
            for (const property_annotators &ann : propanns)
                if (known_proprt(ann.prop) && ann.prop != ARTP_BRAND)
                {
                    extra_props = true;
                    break;
                }

            if (!no_comma && extra_props
                || is_unrandom_artefact(item)
                   && entry && entry->inscrip != nullptr)
            {
                ego += ",";
            }

            propnames.push_back(ego);
        }
    }

    if (is_unrandom_artefact(item) && entry && entry->inscrip != nullptr)
        propnames.push_back(entry->inscrip);

    for (const property_annotators &ann : propanns)
    {
        if (known_proprt(ann.prop))
        {
            const int val = proprt[ann.prop];

            // Don't show rF+/rC- for =Fire, or vice versa for =Ice.
            if (item.base_type == OBJ_JEWELLERY)
            {
                if (item.sub_type == RING_FIRE
                    && (ann.prop == ARTP_FIRE && val == 1
                        || ann.prop == ARTP_COLD && val == -1))
                {
                    continue;
                }
                if (item.sub_type == RING_ICE
                    && (ann.prop == ARTP_COLD && val == 1
                        || ann.prop == ARTP_FIRE && val == -1))
                {
                    continue;
                }
            }

            ostringstream work;
            switch (ann.spell_out)
            {
            case prop_note::numeral: // e.g. AC+4
                work << showpos << artp_name(ann.prop) << val;
                break;
            case prop_note::symbolic: // e.g. F++
            {
                work << artp_name(ann.prop);

                char symbol = val > 0 ? '+' : '-';
                const int sval = abs(val);
                if (sval > 4)
                    work << symbol << sval;
                else
                    work << string(sval, symbol);

                break;
            }
            case prop_note::plain: // e.g. rPois or SInv
                work << artp_name(ann.prop);
                break;
            }
            propnames.push_back(work.str());
        }
    }

    return propnames;
}

string artefact_inscription(const item_def& item)
{
    if (item.base_type == OBJ_BOOKS)
        return "";

    const vector<string> propnames = _randart_propnames(item);

    string insc = comma_separated_line(propnames.begin(), propnames.end(),
                                       " ", " ");
    if (!insc.empty() && insc[insc.length() - 1] == ',')
        insc.erase(insc.length() - 1);
    return insc;
}

void add_inscription(item_def &item, string inscrip)
{
    if (!item.inscription.empty())
    {
        if (ends_with(item.inscription, ","))
            item.inscription += " ";
        else
            item.inscription += ", ";
    }

    item.inscription += inscrip;
}

static const char* _jewellery_base_ability_description(int subtype)
{
    switch (subtype)
    {
#if TAG_MAJOR_VERSION == 34
    case RING_SUSTAIN_ATTRIBUTES:
        return "It sustains your strength, intelligence and dexterity.";
#endif
    case RING_WIZARDRY:
        return "It improves your spell success rate.";
    case RING_FIRE:
        return "It enhances your fire magic.";
    case RING_ICE:
        return "It enhances your ice magic.";
    case RING_TELEPORTATION:
        return "It may teleport you next to monsters.";
#if TAG_MAJOR_VERSION == 34
    case RING_TELEPORT_CONTROL:
        return "It can be evoked for teleport control.";
#endif
    case AMU_HARM:
        return "It increases damage dealt and taken.";
    case AMU_MANA_REGENERATION:
        return "It increases your magic regeneration.";
    case AMU_THE_GOURMAND:
        return "It allows you to eat raw meat even when not hungry.";
    case AMU_ACROBAT:
        return "It helps you evade while moving and waiting.";
#if TAG_MAJOR_VERSION == 34
    case AMU_CONSERVATION:
        return "It protects your inventory from destruction.";
#endif
    case AMU_GUARDIAN_SPIRIT:
        return "It causes incoming damage to be split between your health and "
               "magic.";
    case AMU_FAITH:
        return "It allows you to gain divine favour quickly.";
    case AMU_REFLECTION:
        return "It shields you and reflects attacks.";
    case AMU_INACCURACY:
        return "It reduces the accuracy of all your attacks.";
    }
    return "";
}

struct property_descriptor
{
    artefact_prop_type property;
    const char* desc;           // If it contains %d, will be replaced by value.
    bool is_graded_resist;
};

static string _randart_descrip(const item_def &item)
{
    string description;

    artefact_properties_t  proprt;
    artefact_known_props_t known;
    artefact_desc_properties(item, proprt, known);

    const property_descriptor propdescs[] =
    {
        { ARTP_AC, TR7("It affects your AC (%d).","이것은 당신의 방어력에 영향을 준다 (%d)"), false },
        { ARTP_EVASION, TR7("It affects your evasion (%d).","이것은 당신의 회피력에 영향을 준다 (%d)"), false},
        { ARTP_STRENGTH, TR7("It affects your strength (%d).","이것은 당신의 힘에 영향을 준다 (%d)"), false},
        { ARTP_INTELLIGENCE, TR7("It affects your intelligence (%d).","이것은 당신의 지능에 영향을 준다 (%d)."), false},
        { ARTP_DEXTERITY, TR7("It affects your dexterity (%d).","이것은 당신의 민첩에 영향을 준다 (%d)."), false},
        { ARTP_SLAYING, "It affects your accuracy and damage with ranged "
                        "weapons and melee attacks (%d).", false},
        { ARTP_FIRE, "fire", true},
        { ARTP_COLD, "cold", true},
        { ARTP_ELECTRICITY, TR7("It insulates you from electricity.","이것은 당신을 전기 쇼크로부터 보호한다."), false},
        { ARTP_POISON, "poison", true},
        { ARTP_NEGATIVE_ENERGY, "negative energy", true},
        { ARTP_MAGIC_RESISTANCE, "It affects your resistance to hostile "
                                 "enchantments.", false},
        { ARTP_HP, TR7("It affects your health (%d).","이것은 당신의 생명력에 영향을 준다 (%d)."), false},
        { ARTP_MAGICAL_POWER, TR7("It affects your magic capacity (%d).","이것은 당신의 마력에 영향을 준다 (%d)."), false},
        { ARTP_SEE_INVISIBLE, "It lets you see invisible.", false},
        { ARTP_INVISIBLE, TR7("It lets you turn invisible.","이것은 당신이 투명화를 할 수 있게 해준다."), false},
        { ARTP_FLY, TR7("It lets you fly.","이것은 당신이 비행을 할 수 있게 해준다."), false},
        { ARTP_BLINK, TR7("It lets you blink.","이것은 당신이 순간이동을 할 수 있게 해준다."), false},
        { ARTP_BERSERK, TR7("It lets you go berserk.","이것은 당신이 광폭화를 할 수 있게 해준다."), false},
        { ARTP_NOISE, "It may make noises in combat.", false},
        { ARTP_PREVENT_SPELLCASTING, TR7("It prevents spellcasting.","이것은 당신의 주문시전을 방해한다."), false},
        { ARTP_CAUSE_TELEPORTATION, "It may teleport you next to monsters.", false},
        { ARTP_PREVENT_TELEPORTATION, TR7("It prevents most forms of teleportation.","이것은 대부분의 공간이동을 방해한다."),
          false},
        { ARTP_ANGRY,  "It may make you go berserk in combat.", false},
        { ARTP_CURSE, "It curses itself when equipped.", false},
        { ARTP_CLARITY, TR7("It protects you against confusion.","이것은 당신을 혼란으로부터 보호한다."), false},
        { ARTP_CONTAM, TR7("It causes magical contamination when unequipped.","이것을 해제할 경우, 마력 오염을 일으킬 것이다."), false},
        { ARTP_RMSL, TR7("It protects you from missiles.","이것은 당신을 발사체로부터 보호한다."), false},
        { ARTP_REGENERATION, TR7("It increases your rate of regeneration.","이것은 당신의 재생력을 향상시킨다."), false},
        { ARTP_RCORR, "It provides partial protection from all sources of acid and corrosion.",
          false},
        { ARTP_RMUT, "It protects you from mutation.", false},
        { ARTP_CORRODE, "It may corrode you when you take damage.", false},
        { ARTP_DRAIN, "It causes draining when unequipped.", false},
        { ARTP_SLOW, "It may slow you when you take damage.", false},
        { ARTP_FRAGILE, "It will be destroyed if unequipped.", false },
        { ARTP_SHIELDING, "It affects your SH (%d).", false},
    };

    // Give a short description of the base type, for base types with no
    // corresponding ARTP.
    if (item.base_type == OBJ_JEWELLERY
        && (item_ident(item, ISFLAG_KNOW_TYPE)))
    {
        const char* type = _jewellery_base_ability_description(item.sub_type);
        if (*type)
        {
            description += "\n";
            description += type;
        }
    }

    for (const property_descriptor &desc : propdescs)
    {
        if (known_proprt(desc.property))
        {
            string sdesc = desc.desc;

            // FIXME Not the nicest hack.
            char buf[80];
            snprintf(buf, sizeof buf, "%+d", proprt[desc.property]);
            sdesc = replace_all(sdesc, "%d", buf);

            if (desc.is_graded_resist)
            {
                int idx = proprt[desc.property] + 3;
                idx = min(idx, 6);
                idx = max(idx, 0);

                const char* prefixes[] =
                {
                    "It makes you extremely vulnerable to ",
                    "It makes you very vulnerable to ",
                    "It makes you vulnerable to ",
                    "Buggy descriptor!",
                    "It protects you from ",
                    "It greatly protects you from ",
                    "It renders you almost immune to "
                };
                sdesc = prefixes[idx] + sdesc + '.';
            }

            description += '\n';
            description += sdesc;
        }
    }

    if (known_proprt(ARTP_STEALTH))
    {
        const int stval = proprt[ARTP_STEALTH];
        char buf[80];
        snprintf(buf, sizeof buf, "\nIt makes you %s%s stealthy.",
                 (stval < -1 || stval > 1) ? "much " : "",
                 (stval < 0) ? "less" : "more");
        description += buf;
    }

    return description;
}
#undef known_proprt

static const char *trap_names[] =
{
#if TAG_MAJOR_VERSION == 34
    "dart",
#endif
    "arrow", "spear",
#if TAG_MAJOR_VERSION > 34
    "teleport",
#endif
    "permanent teleport",
    "alarm", "blade",
    "bolt", "net", "Zot", "needle",
    "shaft", "passage", "pressure plate", "web",
#if TAG_MAJOR_VERSION == 34
    "gas", "teleport",
    "shadow", "dormant shadow",
#endif
};

string trap_name(trap_type trap)
{
    COMPILE_CHECK(ARRAYSZ(trap_names) == NUM_TRAPS);

    if (trap >= 0 && trap < NUM_TRAPS)
        return trap_names[trap];
    return "";
}

string full_trap_name(trap_type trap)
{
    string basename = trap_name(trap);
    switch (trap)
    {
    case TRAP_GOLUBRIA:
        return basename + " of Golubria";
    case TRAP_PLATE:
    case TRAP_WEB:
    case TRAP_SHAFT:
        return basename;
    default:
        return basename + " trap";
    }
}

int str_to_trap(const string &s)
{
    // TR7("Zot trap","조트의 함정") is capitalised in trap_names[], but the other trap
    // names aren't.
    const string tspec = lowercase_string(s);

    // allow a couple of synonyms
    if (tspec == "random" || tspec == "any")
        return TRAP_RANDOM;

    for (int i = 0; i < NUM_TRAPS; ++i)
        if (tspec == lowercase_string(trap_names[i]))
            return i;

    return -1;
}

/**
 * How should this panlord be described?
 *
 * @param name   The panlord's name; used as a seed for its appearance.
 * @param flying Whether the panlord can fly.
 * @returns a string including a description of its head, its body, its flight
 *          mode (if any), and how it smells or looks.
 */
static string _describe_demon(const string& name, bool flying)
{
    const uint32_t seed = hash32(&name[0], name.size());
    #define HRANDOM_ELEMENT(arr, id) arr[hash_with_seed(ARRAYSZ(arr), seed, id)]

    static const char* body_types[] =
    {
        "armoured",
        "vast, spindly",
        "fat",
        "obese",
        "muscular",
        "spiked",
        "splotchy",
        "slender",
        "tentacled",
        "emaciated",
        "bug-like",
        "skeletal",
        "mantis",
    };

    static const char* wing_names[] =
    {
        "with small, bat-like wings",
        "with bony wings",
        "with sharp, metallic wings",
        "with the wings of a moth",
        "with thin, membranous wings",
        "with dragonfly wings",
        "with large, powerful wings",
        "with fluttering wings",
        "with great, sinister wings",
        "with hideous, tattered wings",
        "with sparrow-like wings",
        "with hooked wings",
        "with strange knobs attached",
        "which hovers in mid-air",
        "with sacs of gas hanging from its back",
    };

    const char* head_names[] =
    {
        "a cubic structure in place of a head",
        "a brain for a head",
        "a hideous tangle of tentacles for a mouth",
        "the head of an elephant",
        "an eyeball for a head",
        "wears a helmet over its head",
        "a horn in place of a head",
        "a thick, horned head",
        "the head of a horse",
        "a vicious glare",
        "snakes for hair",
        "the face of a baboon",
        "the head of a mouse",
        "a ram's head",
        "the head of a rhino",
        "eerily human features",
        "a gigantic mouth",
        "a mass of tentacles growing from its neck",
        "a thin, worm-like head",
        "huge, compound eyes",
        "the head of a frog",
        "an insectoid head",
        "a great mass of hair",
        "a skull for a head",
        "a cow's skull for a head",
        "the head of a bird",
        "a large fungus growing from its neck",
    };

    static const char* misc_descs[] =
    {
        TR7(" It seethes with hatred of the living."," 이 악마는 살아있는 존재들에 대한 강한 적개심을 표출하고 있다."),
        TR7(" Tiny orange flames dance around it."," 오렌지 색 빛의 작은 불덩어리들이 이 악마의 주변을 춤추듯 배회하고 있다."),
        TR7(" Tiny purple flames dance around it."," 보랏빛의 작은 불덩어리들이 이 악마의 주변을 춤추듯 배회하고 있다."),
        TR7(" It is surrounded by a weird haze."," 이 악마는 옅으면서도 괴상한 연기에 둘러싸여 있다."),
        TR7(" It glows with a malevolent light."," 이 악마는 불경스러운 빛을 내며 발광하고 있다."),
        TR7(" It looks incredibly angry."," 이 악마는 극도로 분노한 듯 보인다."),
        TR7(" It oozes with slime."," 이 악마는 점액을 흘리고 있다."),
        TR7(" It dribbles constantly."," 이 악마는 끊임없이 침을 흘리고 있다."),
        TR7(" Mould grows all over it."," 곰팡이들이 이 악마의 몸에 피어 있다."),
        " Its body is covered in fungus.",
        " It is covered with lank hair.",
        TR7(" It looks diseased."," 이 악마는 병에 걸린것 같다."),
        TR7(" It looks as frightened of you as you are of it."," 이 악마는 당신이 겁에 질린 만큼 당신에게 겁에 질려 있다."),
        TR7(" It moves in a series of hideous convulsions."," 이 악마는 흉측스럽게 경기를 일으키면서 움직이고 있다."),
        TR7(" It moves with an unearthly grace."," 이 악마는 믿을 수 없을 정도로 우아하게 움직인다."),
        TR7(" It leaves a glistening oily trail."," 이 악마는 윤기가 흐르는 기름의 궤적을 남기며 움직이고 있다."),
        TR7(" It shimmers before your eyes."," 이 악마는 당신의 눈 뒤에서 희미하게 빛난다."),
        TR7(" It is surrounded by a brilliant glow."," 이 악마는 화려한 광채를 내고 있다."),
        TR7(" It radiates an aura of extreme power."," 이 악마는 궁극의 힘의 오라를 방출하고 있다."),
        " It seems utterly heartbroken.",
        " It seems filled with irrepressible glee.",
        " It constantly shivers and twitches.",
        " Blue sparks crawl across its body.",
        " It seems uncertain.",
        " A cloud of flies swarms around it.",
        " The air around it ripples with heat.",
        " Crystalline structures grow on everything near it.",
        " It appears supremely confident.",
        " Its skin is covered in a network of cracks.",
        " Its skin has a disgusting oily sheen.",
        " It seems somehow familiar.",
        " It is somehow always in shadow.",
        " It is difficult to look away.",
        " It is constantly speaking in tongues.",
        " It babbles unendingly.",
        " Its body is scourged by damnation.",
        " Its body is extensively scarred.",
        " You find it difficult to look away.",
    };

    static const char* smell_descs[] =
    {
        " It smells of brimstone.",
        TR7(" It is surrounded by a sickening stench."," 이 악마에게서 고약한 악취가 진동하고 있다."),
        " It smells of rotting flesh.",
        " It stinks of death.",
        " It stinks of decay.",
        " It smells delicious!",
    };

    ostringstream description;
    description << "One of the many lords of Pandemonium, " << name << " has ";

    description << article_a(HRANDOM_ELEMENT(body_types, 2));
    description << " body ";

    if (flying)
    {
        description << HRANDOM_ELEMENT(wing_names, 3);
        description << " ";
    }

    description << "and ";
    description << HRANDOM_ELEMENT(head_names, 1) << ".";

    if (!hash_with_seed(5, seed, 4) && you.can_smell()) // 20%
        description << HRANDOM_ELEMENT(smell_descs, 5);

    if (hash_with_seed(2, seed, 6)) // 50%
        description << HRANDOM_ELEMENT(misc_descs, 6);

    return description.str();
}

/**
 * Describe a given mutant beast's tier.
 *
 * @param tier      The mutant_beast_tier of the beast in question.
 * @return          A string describing the tier; e.g.
 *              "It is a juvenile, out of the larval stage but still below its
 *              mature strength."
 */
static string _describe_mutant_beast_tier(int tier)
{
    static const string tier_descs[] = {
        "It is of an unusually buggy age.",
        "It is larval and weak, freshly emerged from its mother's pouch.",
        "It is a juvenile, no longer larval but below its mature strength.",
        "It is mature, stronger than a juvenile but weaker than its elders.",
        "It is an elder, stronger than mature beasts.",
        "It is a primal beast, the most powerful of its kind.",
    };
    COMPILE_CHECK(ARRAYSZ(tier_descs) == NUM_BEAST_TIERS);

    ASSERT_RANGE(tier, 0, NUM_BEAST_TIERS);
    return tier_descs[tier];
}


/**
 * Describe a given mutant beast's facets.
 *
 * @param facets    A vector of the mutant_beast_facets in question.
 * @return          A string describing the facets; e.g.
 *              "It flies and flits around unpredictably, and its breath
 *               smoulders ominously."
 */
static string _describe_mutant_beast_facets(const CrawlVector &facets)
{
    static const string facet_descs[] = {
        " seems unusually buggy.",
        " sports a set of venomous tails",
        " flies swiftly and unpredictably",
        "s breath smoulders ominously",
        " is covered with eyes and tentacles",
        " flickers and crackles with electricity",
        " is covered in dense fur and muscle",
    };
    COMPILE_CHECK(ARRAYSZ(facet_descs) == NUM_BEAST_FACETS);

    if (facets.size() == 0)
        return "";

    return "It" + comma_separated_fn(begin(facets), end(facets),
                      [] (const CrawlStoreValue &sv) -> string {
                          const int facet = sv.get_int();
                          ASSERT_RANGE(facet, 0, NUM_BEAST_FACETS);
                          return facet_descs[facet];
                      }, ", and it", ", it")
           + ".";

}

/**
 * Describe a given mutant beast's special characteristics: its tier & facets.
 *
 * @param mi    The player-visible information about the monster in question.
 * @return      A string describing the monster; e.g.
 *              "It is a juvenile, out of the larval stage but still below its
 *              mature strength. It flies and flits around unpredictably, and
 *              its breath has a tendency to ignite when angered."
 */
static string _describe_mutant_beast(const monster_info &mi)
{
    const int xl = mi.props[MUTANT_BEAST_TIER].get_short();
    const int tier = mutant_beast_tier(xl);
    const CrawlVector facets = mi.props[MUTANT_BEAST_FACETS].get_vector();
    return _describe_mutant_beast_facets(facets)
           + " " + _describe_mutant_beast_tier(tier);
}

/**
 * Is the item associated with some specific training goal?  (E.g. mindelay)
 *
 * @return the goal, or 0 if there is none, scaled by 10.
 */
static int _item_training_target(const item_def &item)
{
    const int throw_dam = property(item, PWPN_DAMAGE);
    if (item.base_type == OBJ_WEAPONS || item.base_type == OBJ_STAVES)
        return weapon_min_delay_skill(item) * 10;
    else if (is_shield(item))
        return round(you.get_shield_skill_to_offset_penalty(item) * 10);
    else if (item.base_type == OBJ_MISSILES && throw_dam)
        return (((10 + throw_dam / 2) - FASTEST_PLAYER_THROWING_SPEED) * 2) * 10;
    else
        return 0;
}

/**
 * Does an item improve with training some skill?
 *
 * @return the skill, or SK_NONE if there is none. Note: SK_NONE is *not* 0.
 */
static skill_type _item_training_skill(const item_def &item)
{
    const int throw_dam = property(item, PWPN_DAMAGE);
    if (item.base_type == OBJ_WEAPONS || item.base_type == OBJ_STAVES)
        return item_attack_skill(item);
    else if (is_shield(item))
        return SK_SHIELDS; // shields are armour, so do shields before armour
    else if (item.base_type == OBJ_ARMOUR)
        return SK_ARMOUR;
    else if (item.base_type == OBJ_MISSILES && throw_dam)
        return SK_THROWING;
    else if (item_is_evokable(item)) // not very accurate
        return SK_EVOCATIONS;
    else
        return SK_NONE;
}

/**
 * Whether it would make sense to set a training target for an item.
 *
 * @param item the item to check.
 * @param ignore_current whether to ignore any current training targets (e.g. if there is a higher target, it might not make sense to set a lower one).
 */
static bool _could_set_training_target(const item_def &item, bool ignore_current)
{
    if (!crawl_state.need_save || is_useless_item(item) || you.species == SP_GNOLL)
        return false;

    const skill_type skill = _item_training_skill(item);
    if (skill == SK_NONE)
        return false;

    const int target = min(_item_training_target(item), 270);

    return target && you.can_train[skill]
       && you.skill(skill, 10, false, false, false) < target
       && (ignore_current || you.get_training_target(skill) < target);
}

/**
 * Produce the "Your skill:" line for item descriptions where specific skill targets
 * are releveant (weapons, missiles, shields)
 *
 * @param skill the skill to look at.
 * @param show_target_button whether to show the button for setting a skill target.
 * @param scaled_target a target, scaled by 10, to use when describing the button.
 */
static string _your_skill_desc(skill_type skill, bool show_target_button, int scaled_target)
{
    if (!crawl_state.need_save || skill == SK_NONE)
        return "";
    string target_button_desc = "";
    int min_scaled_target = min(scaled_target, 270);
    if (show_target_button &&
            you.get_training_target(skill) < min_scaled_target)
    {
        target_button_desc = make_stringf(
            "; use <white>(s)</white> to set %d.%d as a target for %s.",
                                min_scaled_target / 10, min_scaled_target % 10,
                                skill_name(skill));
    }
    int you_skill_temp = you.skill(skill, 10, false, true, true);
    int you_skill = you.skill(skill, 10, false, false, false);

    return make_stringf("Your %sskill: %d.%d%s",
                            (you_skill_temp != you_skill ? "(base) " : ""),
                            you_skill / 10, you_skill % 10,
                            target_button_desc.c_str());
}

/**
 * Produce a description of a skill target for items where specific targets are
 * relevant.
 *
 * @param skill the skill to look at.
 * @param scaled_target a skill level target, scaled by 10.
 * @param training a training value, from 0 to 100. Need not be the actual training
 * value.
 */
static string _skill_target_desc(skill_type skill, int scaled_target,
                                        unsigned int training)
{
    string description = "";
    scaled_target = min(scaled_target, 270);

    const bool max_training = (training == 100);
    const bool hypothetical = !crawl_state.need_save ||
                                    (training != you.training[skill]);

    const skill_diff diffs = skill_level_to_diffs(skill,
                                (double) scaled_target / 10, training, false);
    const int level_diff = xp_to_level_diff(diffs.experience / 10, 10);

    if (max_training)
        description += "At 100% training ";
    else if (!hypothetical)
    {
        description += make_stringf("At current training (%d%%) ",
                                        you.training[skill]);
    }
    else
        description += make_stringf("At a training level of %d%% ", training);

    description += make_stringf(
        "you %s reach %d.%d in %s %d.%d XLs.",
            hypothetical ? "would" : "will",
            scaled_target / 10, scaled_target % 10,
            (you.experience_level + (level_diff + 9) / 10) > 27
                                ? "the equivalent of" : "about",
            level_diff / 10, level_diff % 10);
    if (you.wizard)
    {
        description += make_stringf("\n    (%d xp, %d skp)",
                                    diffs.experience, diffs.skill_points);
    }
    return description;
}

/**
 * Append two skill target descriptions: one for 100%, and one for the
 * current training rate.
 */
static void _append_skill_target_desc(string &description, skill_type skill,
                                        int scaled_target)
{
    if (you.species != SP_GNOLL)
        description += "\n    " + _skill_target_desc(skill, scaled_target, 100);
    if (you.training[skill] > 0 && you.training[skill] < 100)
    {
        description += "\n    " + _skill_target_desc(skill, scaled_target,
                                                    you.training[skill]);
    }
}

static void _append_weapon_stats(string &description, const item_def &item)
{
    const int base_dam = property(item, PWPN_DAMAGE);
    const int ammo_type = fires_ammo_type(item);
    const int ammo_dam = ammo_type == MI_NONE ? 0 :
                                                ammo_type_damage(ammo_type);
    const skill_type skill = _item_training_skill(item);
    const int mindelay_skill = _item_training_target(item);

    const bool could_set_target = _could_set_training_target(item, true);

    if (skill == SK_SLINGS)
    {
        description += make_stringf("\nFiring bullets:    Base damage: %d",
                                    base_dam +
                                    ammo_type_damage(MI_SLING_BULLET));
    }

    description += make_stringf(
    TR7("\nBase accuracy: %+d  Base damage: %d  Base attack delay: %.1f","\n기본 정확도: %+d  기본 데미지: %d  기본 공격 딜레이: %.1f")
    TR7("\nThis weapon's minimum attack delay (%.1f) is reached at skill level %d.","\n이 무기의 최소 공격 딜레이는 (%.1f)에 도달합니다. ( 스킬레벨이 %d 일때 )"),
        property(item, PWPN_HIT),
        base_dam + ammo_dam,
        (float) property(item, PWPN_SPEED) / 10,
        (float) weapon_min_delay(item, item_brand_known(item)) / 10,
        mindelay_skill / 10);

    if (!is_useless_item(item))
    {
        description += "\n    " + _your_skill_desc(skill,
                    could_set_target && in_inventory(item), mindelay_skill);
    }

    if (could_set_target)
        _append_skill_target_desc(description, skill, mindelay_skill);
}

static string _handedness_string(const item_def &item)
{
    string description;

    switch (you.hands_reqd(item))
    {
    case HANDS_ONE:
        if (you.species == SP_FORMICID)
            description += "It is a weapon for one hand-pair.";
        else
            description += TR7("It is a one handed weapon.", "한손무기다");
        break;
    case HANDS_TWO:
        description += TR7("It is a two handed weapon.", "양손무기다");
        break;
    }

    return description;
}

static string _describe_weapon(const item_def &item, bool verbose)
{
    string description;

    description.reserve(200);

    description = "";

    if (verbose)
    {
        description += "\n";
        _append_weapon_stats(description, item);
    }

    const int spec_ench = (is_artefact(item) || verbose)
                          ? get_weapon_brand(item) : SPWPN_NORMAL;
    const int damtype = get_vorpal_type(item);

    if (verbose)
    {
        switch (item_attack_skill(item))
        {
        case SK_POLEARMS:
            description += TR7("\n\nIt can be evoked to extend its reach.","\n\n이 무기는 발동을 통해 사거리를 늘려 공격하는 것이 가능하다.");
            break;
        case SK_AXES:
            description += "\n\nIt hits all enemies adjacent to the wielder, "
                           "dealing less damage to those not targeted.";
            break;
        case SK_LONG_BLADES:
            description += "\n\nIt can be used to riposte, swiftly "
                           "retaliating against a missed attack.";
            break;
        case SK_SHORT_BLADES:
            {
                string adj = (item.sub_type == WPN_DAGGER) ? "extremely"
                                                           : "particularly";
                description += "\n\nIt is " + adj + " good for stabbing"
                               " unaware enemies.";
            }
            break;
        default:
            break;
        }
    }

    // ident known & no brand but still glowing
    // TODO: deduplicate this with the code in item-name.cc
    const bool enchanted = get_equip_desc(item) && spec_ench == SPWPN_NORMAL
                           && !item_ident(item, ISFLAG_KNOW_PLUSES);

    // special weapon descrip
    if (item_type_known(item) && (spec_ench != SPWPN_NORMAL || enchanted))
    {
        description += "\n\n";

        switch (spec_ench)
        {
        case SPWPN_FLAMING:
            if (is_range_weapon(item))
            {
                description += "It causes projectiles fired from it to burn "
                    "those they strike,";
            }
            else
            {
                description += "It has been specially enchanted to burn "
                    "those struck by it,";
            }
            description += " causing extra injury to most foes and up to half "
                           "again as much damage against particularly "
                           "susceptible opponents.";
            if (!is_range_weapon(item) &&
                (damtype == DVORP_SLICING || damtype == DVORP_CHOPPING))
            {
                description += " Big, fiery blades are also staple "
                    "armaments of hydra-hunters.";
            }
            break;
        case SPWPN_FREEZING:
            if (is_range_weapon(item))
            {
                description += "It causes projectiles fired from it to freeze "
                    "those they strike,";
            }
            else
            {
                description += "It has been specially enchanted to freeze "
                    "those struck by it,";
            }
            description += " causing extra injury to most foes "
                    "and up to half again as much damage against particularly "
                    "susceptible opponents.";
            if (is_range_weapon(item))
                description += " They";
            else
                description += " It";
            description += " can also slow down cold-blooded creatures.";
            break;
        case SPWPN_HOLY_WRATH:
            description += "It has been blessed by the Shining One";
            if (is_range_weapon(item))
            {
                description += ", and any ";
                description += ammo_name(item);
                description += " fired from it will";
            }
            else
                description += " to";
            description += " cause great damage to the undead and demons.";
            break;
        case SPWPN_ELECTROCUTION:
            if (is_range_weapon(item))
            {
                description += "It charges the ammunition it shoots with "
                    "electricity; occasionally upon a hit, such missiles "
                    "may discharge and cause terrible harm.";
            }
            else
            {
                description += "Occasionally, upon striking a foe, it will "
                    "discharge some electrical energy and cause terrible "
                    "harm.";
            }
            break;
        case SPWPN_VENOM:
            if (is_range_weapon(item))
                description += TR7("It poisons the ammo it fires.","발사되는 화살이나 다트 등에 독을 부여한다.");
            else
                description += TR7("It poisons the flesh of those it strikes.","이 무기는 공격당한 상대를 독에 걸리게 한다.");
            break;
        case SPWPN_PROTECTION:
            description += "It protects the one who uses it against "
                "injury (+AC on strike).";
            break;
        case SPWPN_DRAINING:
            description += "A truly terrible weapon, it drains the "
                "life of those it strikes.";
            break;
        case SPWPN_SPEED:
            description += "Attacks with this weapon are significantly faster.";
            break;
        case SPWPN_VORPAL:
            if (is_range_weapon(item))
            {
                description += "Any ";
                description += ammo_name(item);
                description += " fired from it inflicts extra damage.";
            }
            else
            {
                description += "It inflicts extra damage upon your "
                    "enemies.";
            }
            break;
        case SPWPN_CHAOS:
            if (is_range_weapon(item))
            {
                description += "Each projectile launched from it has a "
                               "different, random effect.";
            }
            else
            {
                description += "Each time it hits an enemy it has a "
                    "different, random effect.";
            }
            break;
        case SPWPN_VAMPIRISM:
            description += "It inflicts no extra harm, but heals "
                "its wielder when it wounds a living foe.";
            break;
        case SPWPN_PAIN:
            description += "In the hands of one skilled in necromantic "
                "magic, it inflicts extra damage on living creatures.";
            break;
        case SPWPN_DISTORTION:
            description += "It warps and distorts space around it. "
                "Unwielding it can cause banishment or high damage.";
            break;
        case SPWPN_PENETRATION:
            description += "Ammo fired by it will pass through the "
                "targets it hits, potentially hitting all targets in "
                "its path until it reaches maximum range.";
            break;
        case SPWPN_REAPING:
            description += "If a monster killed with it leaves a "
                "corpse in good enough shape, the corpse will be "
                "animated as a zombie friendly to the killer.";
            break;
        case SPWPN_ANTIMAGIC:
            description += "It reduces the magical energy of the wielder, "
                    "and disrupts the spells and magical abilities of those "
                    "hit. Natural abilities and divine invocations are not "
                    "affected.";
            break;
        case SPWPN_NORMAL:
            ASSERT(enchanted);
            description += "It has no special brand (it is not flaming, "
                    "freezing, etc), but is still enchanted in some way - "
                    "positive or negative.";
            break;
        }
    }

    if (you.duration[DUR_EXCRUCIATING_WOUNDS] && &item == you.weapon())
    {
        description += "\nIt is temporarily rebranded; it is actually a";
        if ((int) you.props[ORIGINAL_BRAND_KEY] == SPWPN_NORMAL)
            description += "n unbranded weapon.";
        else
        {
            description += " weapon of "
                        + ego_type_string(item, false, you.props[ORIGINAL_BRAND_KEY])
                        + ".";
        }
    }

    if (is_artefact(item))
    {
        string rand_desc = _randart_descrip(item);
        if (!rand_desc.empty())
        {
            description += "\n";
            description += rand_desc;
        }

        // XXX: Can't happen, right?
        if (!item_ident(item, ISFLAG_KNOW_PROPERTIES)
            && item_type_known(item))
        {
            description += "\n";
            description += TR7("This weapon may have some hidden properties.", "이 무기는 숨겨진 능력이 있을 것 같다.");
        }
    }

    if (verbose)
    {
        const skill_type skill = item_attack_skill(item);
        description += "\n\n";
        description += make_stringf(TR7("This %s falls into the '%s' category. ","이 %s(은)는 '%s'류로 분류된다."),
            is_unrandom_artefact(item) ? get_artefact_base_name(item).c_str() : TR7("weapon", "무기"),
            skill == SK_FIGHTING ? "buggy" : skill_name(skill)
        );

        description += _handedness_string(item);

        if (!you.could_wield(item, true) && crawl_state.need_save)
        {
            description += "\n";
            description += TR7("It is too large for you to wield.","이 무기는 장비하기에 너무 커 보인다.");
        }
    }

    if (!is_artefact(item))
    {
        if (item_ident(item, ISFLAG_KNOW_PLUSES) && item.plus >= MAX_WPN_ENCHANT){
            description += "\n";
            description += TR7("It cannot be enchanted further.","이것은 더 이상 강화할 수 없다.");
        }
        else
        {
            description += "\n";
            description += make_stringf(TR7("It can be maximally enchanted to +%s.","이것은 최대 +%s까지 강화가 가능하다."), to_string(MAX_WPN_ENCHANT).c_str());
        }
    }

    return description;
}

static string _describe_ammo(const item_def &item)
{
    string description;

    description.reserve(64);

    const bool can_launch = has_launcher(item);
    const bool can_throw  = is_throwable(&you, item, true);

    if (item.brand && item_type_known(item))
    {
        description += "\n\n";

        string threw_or_fired;
        if (can_throw)
        {
            threw_or_fired += "threw";
            if (can_launch)
                threw_or_fired += TR7(" or "," 혹은 ");
        }
        if (can_launch)
            threw_or_fired += "fired";

        switch (item.brand)
        {
#if TAG_MAJOR_VERSION == 34
        case SPMSL_FLAME:
            description += "It burns those it strikes, causing extra injury "
                    "to most foes and up to half again as much damage against "
                    "particularly susceptible opponents. Compared to normal "
                    "ammo, it is twice as likely to be destroyed on impact.";
            break;
        case SPMSL_FROST:
            description += "It freezes those it strikes, causing extra injury "
                    "to most foes and up to half again as much damage against "
                    "particularly susceptible opponents. It can also slow down "
                    "cold-blooded creatures. Compared to normal ammo, it is "
                    "twice as likely to be destroyed on impact.";
            break;
#endif
        case SPMSL_CHAOS:
            description += "When ";

            if (can_throw)
            {
                description += "thrown, ";
                if (can_launch)
                    description += "or ";
            }

            if (can_launch)
                description += TR7("fired from an appropriate launcher, ","이것을 발사할 수 있는 무기로 발사하면, ");

            description += "it has a random effect.";
            break;
        case SPMSL_POISONED:
            description += TR7("It is coated with poison.","이것은 독이 발라져 있다.");
            break;
        case SPMSL_CURARE:
            description += "It is tipped with a substance that causes "
                           "asphyxiation, dealing direct damage as well as "
                           "poisoning and slowing those it strikes.\n"
                           "It is twice as likely to be destroyed on impact as "
                           "other needles.";
            break;
        case SPMSL_PARALYSIS:
            description += TR7("It is tipped with a paralysing substance.","끝에 마비를 일으키는 물질이 발라져 있다.");
            break;
        case SPMSL_SLEEP:
            description += TR7("It is coated with a fast-acting tranquilizer.","효과빠른 수면제가 발라져 있다.");
            break;
        case SPMSL_CONFUSION:
            description += TR7("It is tipped with a substance that causes confusion.","혼란을 일으키는 물질이 발라져 있다.");
            break;
#if TAG_MAJOR_VERSION == 34
        case SPMSL_SICKNESS:
            description += TR7("It has been contaminated by something likely to cause disease.","질병을 일으킬 것 같이 오염되어 있다.");
            break;
#endif
        case SPMSL_FRENZY:
            description += "It is tipped with a substance that sends those it "
                           "hits into a mindless rage, attacking friend and "
                           "foe alike.";
            break;
        case SPMSL_RETURNING:
            description += "A skilled user can throw it in such a way that it "
                           "will return to its owner.";
            break;
        case SPMSL_PENETRATION:
            description += "It will pass through any targets it hits, "
                           "potentially hitting all targets in its path until "
                           "it reaches its maximum range.";
            break;
        case SPMSL_DISPERSAL:
            description += "It will cause any target it hits to blink, with a "
                           "tendency towards blinking further away from the "
                           "one who " + threw_or_fired + " it.";
            break;
        case SPMSL_EXPLODING:
            description += "It will explode into fragments upon hitting a "
                           "target, hitting an obstruction, or reaching its "
                           "maximum range.";
            break;
        case SPMSL_STEEL:
            description += "It deals increased damage compared to normal ammo.";
            break;
        case SPMSL_SILVER:
            description += "It deals substantially increased damage to chaotic "
                           "and magically transformed beings. It also inflicts "
                           "extra damage against mutated beings, according to "
                           "how mutated they are.";
            break;
        }
    }

    const int dam = property(item, PWPN_DAMAGE);
    if (dam)
    {
        const int throw_delay = (10 + dam / 2);
        const int target_skill = _item_training_target(item);
        const bool could_set_target = _could_set_training_target(item, true);

        description += make_stringf(
            "\nBase damage: %d  Base attack delay: %.1f"
            "\nThis projectile's minimum attack delay (%.1f) "
                "is reached at skill level %d.",
            dam,
            (float) throw_delay / 10,
            (float) FASTEST_PLAYER_THROWING_SPEED / 10,
            target_skill / 10
        );

        if (!is_useless_item(item))
        {
            description += "\n    " +
                    _your_skill_desc(SK_THROWING,
                        could_set_target && in_inventory(item), target_skill);
        }
        if (could_set_target)
            _append_skill_target_desc(description, SK_THROWING, target_skill);
    }

    if (ammo_always_destroyed(item))
        description += "\n\nIt will always be destroyed on impact.";
    else if (!ammo_never_destroyed(item))
        description += "\n\nIt may be destroyed on impact.";

    return description;
}

static string _warlock_mirror_reflect_desc()
{
    const int SH = crawl_state.need_save ? player_shield_class() : 0;
    const int reflect_chance = 100 * SH / omnireflect_chance_denom(SH);
    return "\n\nWith your current SH, it has a " + to_string(reflect_chance) +
           "% chance to reflect enchantments and other normally unblockable "
           "effects.";
}

static string _describe_armour(const item_def &item, bool verbose)
{
    string description;

    description.reserve(200);

    if (verbose)
    {
        if (is_shield(item))
        {
            const int target_skill = _item_training_target(item);
            description += "\n";
            description += "\nBase shield rating: "
                        + to_string(property(item, PARM_AC));
            const bool could_set_target = _could_set_training_target(item, true);

            if (!is_useless_item(item))
            {
                description += "       Skill to remove penalty: "
                            + make_stringf("%d.%d", target_skill / 10,
                                                target_skill % 10);

                if (crawl_state.need_save)
                {
                    description += "\n                            "
                        + _your_skill_desc(SK_SHIELDS,
                          could_set_target && in_inventory(item), target_skill);
                }
                else
                    description += "\n";
                if (could_set_target)
                {
                    _append_skill_target_desc(description, SK_SHIELDS,
                                                                target_skill);
                }
            }

            if (is_unrandom_artefact(item, UNRAND_WARLOCK_MIRROR))
                description += _warlock_mirror_reflect_desc();
        }
        else
        {
            const int evp = property(item, PARM_EVASION);
            description += TR7("\n\nBase armour rating: ","\n\n방어력 등급       :")
                        + to_string(property(item, PARM_AC));
            if (get_armour_slot(item) == EQ_BODY_ARMOUR)
            {
                description += TR7("       Encumbrance rating: ","       움직임 방해 등급:   ")
                            + to_string(-evp / 10);
            }
            // Bardings reduce evasion by a fixed amount, and don't have any of
            // the other effects of encumbrance.
            else if (evp)
            {
                description += TR7("       Evasion: ", "       회피력 : ")
                            + to_string(evp / 30);
            }

            // only display player-relevant info if the player exists
            if (crawl_state.need_save && get_armour_slot(item) == EQ_BODY_ARMOUR)
            {
                description += make_stringf("\nWearing mundane armour of this type "
                                            "will give the following: %d AC",
                                             you.base_ac_from(item));
            }
        }
    }

    const int ego = get_armour_ego_type(item);
    const bool enchanted = get_equip_desc(item) && ego == SPARM_NORMAL
                           && !item_ident(item, ISFLAG_KNOW_PLUSES);

    if ((ego != SPARM_NORMAL || enchanted) && item_type_known(item) && verbose)
    {
        description += "\n\n";

        switch (ego)
        {
        case SPARM_RUNNING:
            if (item.sub_type == ARM_NAGA_BARDING)
                description += TR7("It allows its wearer to slither at a great speed.","착용자를 빠른 속도로 기어갈 수 있게 해준다.");
            else
                description += TR7("It allows its wearer to run at a great speed.","착용자를 빠른 속도로 달릴 수 있게 해준다.");
            break;
        case SPARM_FIRE_RESISTANCE:
            description += TR7("It protects its wearer from heat.","착용자를 열기로부터 보호한다.");
            break;
        case SPARM_COLD_RESISTANCE:
            description += TR7("It protects its wearer from cold.","착용자를 냉기로부터 보호한다.");
            break;
        case SPARM_POISON_RESISTANCE:
            description += TR7("It protects its wearer from poison.","착용자를 독으로부터 보호한다.");
            break;
        case SPARM_SEE_INVISIBLE:
            description += TR7("It allows its wearer to see invisible things.","착용자가 투명한 것들을 볼 수 있게 해준다.");
            break;
        case SPARM_INVISIBILITY:
            description += TR7("When activated it hides its wearer from ","발동시 이것은 착용자를 주위 시선으로부터 숨기지만, 동시에 신진대사의 속도도 큰 폭으로 증가하게 된다.")
                TR7("the sight of others, but also increases ","")
                TR7("their metabolic rate by a large amount.","");
            break;
        case SPARM_STRENGTH:
            description += TR7("It increases the physical power of its wearer (+3 to strength).","착용자의 물리적 힘을 증가시킨다. (+3 힘)");
            break;
        case SPARM_DEXTERITY:
            description += TR7("It increases the dexterity of its wearer (+3 to dexterity).","착용자의 민첩성을 증가시킨다. (+3 민첩성)");
            break;
        case SPARM_INTELLIGENCE:
            description += TR7("It makes you more clever (+3 to intelligence).","착용자를 더욱 똑똑하게 만들어준다. (+3 지능)");
            break;
        case SPARM_PONDEROUSNESS:
            description += TR7("It is very cumbersome, thus slowing your movement.","움직임에 매우 방해가 되어, 이동속도를 감소시킨다.");
            break;
        case SPARM_FLYING:
            description += TR7("It can be activated to allow its wearer to ","발동시켜 착용자를 공중으로 떠올라 비행하도록 할 수 있다. 공중에 떠 있는 상태는 어느정도 지속된다.")
                TR7("fly indefinitely.","");
            break;
        case SPARM_MAGIC_RESISTANCE:
            description += TR7("It increases its wearer's resistance ","착용자의 마법에 대한 저항력을 강화시킨다.")
                TR7("to enchantments.","");
            break;
        case SPARM_PROTECTION:
            description += TR7("It protects its wearer from harm (+3 to AC).","착용자를 물리적 피해로부터 보호한다. (+3 AC)");
            break;
        case SPARM_STEALTH:
            description += TR7("It enhances the stealth of its wearer.","착용자가 더 은밀하게 움직일 수 있도록 해준다.");
            break;
        case SPARM_RESISTANCE:
            description += TR7("It protects its wearer from the effects ","착용자를 냉기와 불로부터 보호한다.")
                TR7("of both cold and heat.","");
            break;
        case SPARM_POSITIVE_ENERGY:
            description += TR7("It protects its wearer from ","착용자를 음에너지로부터 보호한다.")
                TR7("the effects of negative energy.","");
            break;

        // This is only for robes.
        case SPARM_ARCHMAGI:
            description += TR7("It increases the power of its wearer's ","착용자가 시전하는 주문의 위력을 강화시킨다.")
                TR7("magical spells.","");
            break;
#if TAG_MAJOR_VERSION == 34
        case SPARM_PRESERVATION:
            description += TR7("It does nothing special.","특별한 것은 없습니다.");
            break;
#endif
        case SPARM_REFLECTION:
            description += TR7("It reflects blocked things back in the ","막은 것들을 그것들이 온 방향으로 되돌려보낸다.")
                TR7("direction they came from.","");
            break;

        case SPARM_SPIRIT_SHIELD:
            description += TR7("It shields its wearer from harm at the cost ","마나를 소모하여 착용자를 피해로부터 보호한다.")
                TR7("of magical power.","");
            break;

        case SPARM_NORMAL:
            ASSERT(enchanted);
            description += TR7("It has no special ego (it is not resistant to ","특별한 에고가 없습니다 ( 불에대한 저항, 등등 ) 그러나 어떤 방식으로 마법에 걸렸습니다. - 긍정적이거나 부정적입니다.")
                           TR7("fire, etc), but is still enchanted in some way - ","")
                           TR7("positive or negative.","");

            break;

        // This is only for gloves.
        case SPARM_ARCHERY:
            description += TR7("It improves your effectiveness with ranged ","활과 자벨린같은 원거리 무기의 공격 효과를 향상시킵니다. (Slay+4)")
                           TR7("weaponry, such as bows and javelins (Slay+4).","");
            break;

        // These are only for scarves.
        case SPARM_REPULSION:
            description += TR7("It protects its wearer by repelling missiles.","투사체를 밀어내 착용자를 보호합니다.");
            break;

        case SPARM_CLOUD_IMMUNE:
            description += TR7("It completely protects its wearer from the effects of clouds.","착용자를 구름의 영향으로부터 완전히 보호합니다.");
            break;
        }
    }

    if (is_artefact(item))
    {
        string rand_desc = _randart_descrip(item);
        if (!rand_desc.empty())
        {
            description += "\n";
            description += rand_desc;
        }

        // Can't happen, right? (XXX)
        if (!item_ident(item, ISFLAG_KNOW_PROPERTIES) && item_type_known(item))
            description += TR7("\nThis armour may have some hidden properties.","\n이 방어구는 왠지 숨겨진 능력이 있을 것 같다.");
    }
    else
    {
        const int max_ench = armour_max_enchant(item);
        if (item.plus < max_ench || !item_ident(item, ISFLAG_KNOW_PLUSES))
        {
            description += TR7("\n\nIt can be maximally enchanted to +","\n\n이것은 최대 다음까지 강화가 가능하다. (+")
                           + to_string(max_ench) + TR7(".",")");
        }
        else
            description += TR7("\n\nIt cannot be enchanted further.","\n\n이것은 더 이상 강화할 수 없다.");
    }

    return description;
}

static string _describe_jewellery(const item_def &item, bool verbose)
{
    string description;

    description.reserve(200);

    if (verbose && !is_artefact(item)
        && item_ident(item, ISFLAG_KNOW_PLUSES))
    {
        // Explicit description of ring power.
        if (item.plus != 0)
        {
            switch (item.sub_type)
            {
            case RING_PROTECTION:
                description += make_stringf(TR7("\nIt affects your AC (%+d).","\n이것은 당신의 방어력에 영향을 준다 (%+d)"),
                                            item.plus);
                break;

            case RING_EVASION:
                description += make_stringf(TR7("\nIt affects your evasion (%+d).","\n이것은 당신의 회피력에 영향을 준다 (%+d)"),
                                            item.plus);
                break;

            case RING_STRENGTH:
                description += make_stringf(TR7("\nIt affects your strength (%+d).","\n이것은 당신의 힘에 영향을 준다 (%+d)"),
                                            item.plus);
                break;

            case RING_INTELLIGENCE:
                description += make_stringf(TR7("\nIt affects your intelligence (%+d).","\n이것은 당신의 지능에 영향을 준다 (%+d)."),
                                            item.plus);
                break;

            case RING_DEXTERITY:
                description += make_stringf(TR7("\nIt affects your dexterity (%+d).","\n이것은 당신의 민첩에 영향을 준다 (%+d)."),
                                            item.plus);
                break;

            case RING_SLAYING:
                description += make_stringf(TR7("\nIt affects your accuracy and","\n 원거리 무기 및 근접 공격의 정확도와 데미지에 영향을 줍니다. (%+d)")
                      TR7(" damage with ranged weapons and melee attacks (%+d).",""),
                      item.plus);
                break;

            case AMU_REFLECTION:
                description += make_stringf(TR7("\nIt affects your shielding (%+d).","\n막는 능력에 영향을 미칩니다. (%+d)"),
                                            item.plus);
                break;

            default:
                break;
            }
        }
    }

    // Artefact properties.
    if (is_artefact(item))
    {
        string rand_desc = _randart_descrip(item);
        if (!rand_desc.empty())
        {
            description += "\n";
            description += rand_desc;
        }
        if (!item_ident(item, ISFLAG_KNOW_PROPERTIES) ||
            !item_ident(item, ISFLAG_KNOW_TYPE))
        {
            description += TR7("\nThis ","\n이");
            description += (jewellery_is_amulet(item) ? TR7("amulet","부적") : TR7("ring","링"));
            description += TR7(" may have hidden properties.","은 숨겨진 능력을 갖고 있는듯 하다.");
        }
    }

    return description;
}

static bool _compare_card_names(card_type a, card_type b)
{
    return string(card_name(a)) < string(card_name(b));
}

static bool _check_buggy_deck(const item_def &deck, string &desc)
{
    if (!is_deck(deck))
    {
        desc += TR7("This isn't a deck at all!\n","이것은 덱이 아닙니다!\n");
        return true;
    }

    const CrawlHashTable &props = deck.props;

    if (!props.exists(CARD_KEY)
        || props[CARD_KEY].get_type() != SV_VEC
        || props[CARD_KEY].get_vector().get_type() != SV_BYTE
        || cards_in_deck(deck) == 0)
    {
        return true;
    }

    return false;
}

static string _describe_deck(const item_def &item)
{
    string description;

    description.reserve(100);

    description += "\n";

    if (_check_buggy_deck(item, description))
        return "";

    if (item_type_known(item))
        description += deck_contents(item.sub_type) + "\n";

    description += make_stringf(TR7("\nMost decks begin with %d to %d cards.","대부분의 덱은 %d에서 %d카드로 시작합니다."),
                                MIN_STARTING_CARDS,
                                MAX_STARTING_CARDS);

    const vector<card_type> drawn_cards = get_drawn_cards(item);
    if (!drawn_cards.empty())
    {
        description += TR7("\n","");
        description += TR7("Drawn card(s): ","뽑은 카드 : ");
        description += comma_separated_fn(drawn_cards.begin(),
                                          drawn_cards.end(),
                                          card_name);
    }

    const int num_cards = cards_in_deck(item);
    // The list of known cards, ending at the first one not known to be at the
    // top.
    vector<card_type> seen_top_cards;
    // Seen cards in the deck not necessarily contiguous with the start. (If
    // Nemelex wrath shuffled a deck that you stacked, for example.)
    vector<card_type> other_seen_cards;
    bool still_contiguous = true;
    for (int i = 0; i < num_cards; ++i)
    {
        uint8_t flags;
        const card_type card = get_card_and_flags(item, -i-1, flags);
        if (flags & CFLAG_SEEN)
        {
            if (still_contiguous)
                seen_top_cards.push_back(card);
            else
                other_seen_cards.push_back(card);
        }
        else
            still_contiguous = false;
    }

    if (!seen_top_cards.empty())
    {
        description += "\n";
        description += TR7("Next card(s): ","다음 카드: ");
        description += comma_separated_fn(seen_top_cards.begin(),
                                          seen_top_cards.end(),
                                          card_name);
    }
    if (!other_seen_cards.empty())
    {
        description += "\n";
        sort(other_seen_cards.begin(), other_seen_cards.end(),
             _compare_card_names);

        description += TR7("Seen card(s): ","확인한 카드: ");
        description += comma_separated_fn(other_seen_cards.begin(),
                                          other_seen_cards.end(),
                                          card_name);
    }

    return description;
}

bool is_dumpable_artefact(const item_def &item)
{
    return is_known_artefact(item) && item_ident(item, ISFLAG_KNOW_PROPERTIES);
}

/**
 * Describe a specified item.
 *
 * @param item    The specified item.
 * @param verbose Controls various switches for the length of the description.
 * @param dump    This controls which style the name is shown in.
 * @param lookup  If true, the name is not shown at all.
 *   If either of those two are true, the DB description is not shown.
 * @return a string with the name, db desc, and some other data.
 */
string get_item_description(const item_def &item, bool verbose,
                            bool dump, bool lookup)
{
    ostringstream description;

#ifdef DEBUG_DIAGNOSTICS
    if (!dump && !you.suppress_wizard)
    {
        description << setfill('0');
        description << "\n\n"
                    << "base: " << static_cast<int>(item.base_type)
                    << " sub: " << static_cast<int>(item.sub_type)
                    << " plus: " << item.plus << " plus2: " << item.plus2
                    << " special: " << item.special
                    << "\n"
                    << "quant: " << item.quantity
                    << " rnd?: " << static_cast<int>(item.rnd)
                    << " flags: " << hex << setw(8) << item.flags
                    << dec << "\n"
                    << "x: " << item.pos.x << " y: " << item.pos.y
                    << " link: " << item.link
                    << " slot: " << item.slot
                    << " ident_type: "
                    << get_ident_type(item)
                    << "\nannotate: "
                    << stash_annotate_item(STASH_LUA_SEARCH_ANNOTATE, &item);
    }
#endif

    if (verbose || (item.base_type != OBJ_WEAPONS
                    && item.base_type != OBJ_ARMOUR
                    && item.base_type != OBJ_BOOKS))
    {
        description << "\n\n";

        bool need_base_desc = !lookup;

        if (dump)
        {
            description << "["
                        << item.name(DESC_DBNAME, true, false, false)
                        << "]";
            need_base_desc = false;
        }
        else if (is_unrandom_artefact(item) && item_type_known(item))
        {
            const string desc = getLongDescription(get_artefact_name(item));
            if (!desc.empty())
            {
                description << desc;
                need_base_desc = false;
                description.seekp((streamoff)-1, ios_base::cur);
                description << " ";
            }
        }
        // Randart jewellery properties will be listed later,
        // just describe artefact status here.
        else if (is_artefact(item) && item_type_known(item)
                 && item.base_type == OBJ_JEWELLERY)
        {
            description << TR7("It is an ancient artefact.","고대의 유물이다.");
            need_base_desc = false;
        }

        if (need_base_desc)
        {
            string db_name = item.name(DESC_DBNAME, true, false, false);
            string db_desc = getLongDescription(db_name);

            if (db_desc.empty())
            {
                if (item_type_known(item))
                {
                    description << TR7("[ERROR: no desc for item name '","[ERROR: 아이템의 설명을 찾을 수 없다 '") << db_name
                                << "']. Perhaps this item has been removed?\n";
                }
                else
                {
                    description << uppercase_first(item.name(DESC_A, true,
                                                             false, false));
                    description << ".\n";
                }
            }
            else
                description << db_desc;

            // Get rid of newline at end of description; in most cases we
            // will be adding "\n\n" immediately, and we want only one,
            // not two, blank lines. This allow allows the "unpleasant"
            // message for chunks to appear on the same line.
            description.seekp((streamoff)-1, ios_base::cur);
            description << " ";
        }
    }

    bool need_extra_line = true;
    string desc;
    switch (item.base_type)
    {
    // Weapons, armour, jewellery, books might be artefacts.
    case OBJ_WEAPONS:
        desc = _describe_weapon(item, verbose);
        if (desc.empty())
            need_extra_line = false;
        else
            description << desc;
        break;

    case OBJ_ARMOUR:
        desc = _describe_armour(item, verbose);
        if (desc.empty())
            need_extra_line = false;
        else
            description << desc;
        break;

    case OBJ_JEWELLERY:
        desc = _describe_jewellery(item, verbose);
        if (desc.empty())
            need_extra_line = false;
        else
            description << desc;
        break;

    case OBJ_BOOKS:
        if (!verbose
            && (Options.dump_book_spells || is_random_artefact(item)))
        {
            desc += describe_item_spells(item);
            if (desc.empty())
                need_extra_line = false;
            else
                description << desc;
        }
        break;

    case OBJ_MISSILES:
        description << _describe_ammo(item);
        break;

    case OBJ_CORPSES:
        if (item.sub_type == CORPSE_SKELETON)
            break;

        // intentional fall-through
    case OBJ_FOOD:
        if (item.base_type == OBJ_CORPSES || item.sub_type == FOOD_CHUNK)
        {
            switch (determine_chunk_effect(item))
            {
            case CE_NOXIOUS:
                description << TR7("\n\nThis meat is toxic.","\n\n이 고기는 유독합니다.");
                break;
            default:
                break;
            }
        }
        break;

    case OBJ_STAVES:
        {
            string stats = "\n";
            _append_weapon_stats(stats, item);
            description << stats;
        }
        description << TR7("\n\nIt falls into the 'Staves' category. ","\n\n이 이것은 '지팡이'류로 분류된다.");
        description << _handedness_string(item);
        break;

    case OBJ_MISCELLANY:
        if (is_deck(item))
            description << _describe_deck(item);
        if (item.sub_type == MISC_ZIGGURAT && you.zigs_completed)
        {
            const int zigs = you.zigs_completed;
            description << "\n\nIt is surrounded by a "
                        << (zigs >= 27 ? "blinding " : // just plain silly
                            zigs >=  9 ? "dazzling " :
                            zigs >=  3 ? "bright " :
                                         "gentle ")
                        << "glow.";
        }
        if (is_xp_evoker(item))
        {
            description << "\n\nOnce "
                        << (item.sub_type == MISC_LIGHTNING_ROD
                            ? "all charges have been used"
                            : "activated")
                        << ", this device "
                        << (!item_is_horn_of_geryon(item) ?
                           "and all other devices of its kind " : "")
                        << "will be rendered temporarily inert. However, "
                        << (!item_is_horn_of_geryon(item) ? "they " : "it ")
                        << "will recharge as you gain experience."
                        << (!evoker_charges(item.sub_type) ?
                           " The device is presently inert." : "");
        }
        break;

    case OBJ_POTIONS:
#ifdef DEBUG_BLOOD_POTIONS
        // List content of timer vector for blood potions.
        if (!dump && is_blood_potion(item))
        {
            item_def stack = static_cast<item_def>(item);
            CrawlHashTable &props = stack.props;
            if (!props.exists("timer"))
                description << "\nTimers not yet initialized.";
            else
            {
                CrawlVector &timer = props["timer"].get_vector();
                ASSERT(!timer.empty());

                description << "\nQuantity: " << stack.quantity
                            << "        Timer size: " << (int) timer.size();
                description << "\nTimers:\n";
                for (const CrawlStoreValue& store : timer)
                    description << store.get_int() << "  ";
            }
        }
#endif

    case OBJ_SCROLLS:
    case OBJ_ORBS:
    case OBJ_GOLD:
    case OBJ_RUNES:
    case OBJ_WANDS:
#if TAG_MAJOR_VERSION == 34
    case OBJ_RODS:
#endif
        // No extra processing needed for these item types.
        break;

    default:
        die("Bad item class");
    }

    if (!verbose && item_known_cursed(item))
        description << TR7("\nIt has a curse placed upon it.","\n이것에는 저주가 걸려있다.");
    else
    {
        if (verbose)
        {
            if (need_extra_line)
                description << "\n";
            if (item_known_cursed(item))
                description << TR7("\nIt has a curse placed upon it.","\n이것에는 저주가 걸려있다.");

            if (is_artefact(item))
            {
                if (item.base_type == OBJ_ARMOUR
                    || item.base_type == OBJ_WEAPONS)
                {
                    description << TR7("\nThis ancient artefact cannot be changed ","\n이것은 고대의 아티팩트로, 마법이나 일상의 수단으로는 변화시킬 수 없다.")
                        TR7("by magic or mundane means.","");
                }
                // Randart jewellery has already displayed this line.
                else if (item.base_type != OBJ_JEWELLERY
                         || (item_type_known(item) && is_unrandom_artefact(item)))
                {
                    description << TR7("\nIt is an ancient artefact.","\n이것은 고대의 아티팩트다.");
                }
            }
        }
    }

    if (god_hates_item(item))
    {
        description << "\n\n" << uppercase_first(god_name(you.religion))
                    << " disapproves of the use of such an item.";
    }

    if (verbose && origin_describable(item))
        description << "\n" << origin_desc(item) << ".";

    // This information is obscure and differs per-item, so looking it up in
    // a docs file you don't know to exist is tedious.
    if (verbose)
    {
        description << "\n\n" << TR7("Stash search prefixes: ","이 아이템에 대한 검색어: ")
                    << userdef_annotate_item(STASH_LUA_SEARCH_ANNOTATE, &item);
        string menu_prefix = item_prefix(item, false);
        if (!menu_prefix.empty())
            description << TR7("\nMenu/colouring prefixes: ","\n메뉴/컬러링 접두어: ") << menu_prefix;
    }

    return description.str();
}

string get_cloud_desc(cloud_type cloud, bool include_title)
{
    if (cloud == CLOUD_NONE)
        return "";
    const string cl_name = cloud_type_name(cloud);
    const string cl_desc = getLongDescription(cl_name + " cloud");

    string ret;
    if (include_title)
        ret = "A cloud of " + cl_name + (cl_desc.empty() ? "." : ".\n\n");
    ret += cl_desc + extra_cloud_info(cloud);
    return ret;
}

static vector<pair<string,string>> _get_feature_extra_descs(const coord_def &pos)
{
    vector<pair<string,string>> ret;
    dungeon_feature_type feat = env.map_knowledge(pos).feat();
    if (!feat_is_solid(feat))
    {
        if (haloed(pos) && !umbraed(pos))
        {
            ret.emplace_back(pair<string,string>(
                    "A halo.", getLongDescription("haloed")));
        }
        if (umbraed(pos) && !haloed(pos))
        {
            ret.emplace_back(pair<string,string>(
                    "An umbra.", getLongDescription("umbraed")));
        }
        if (liquefied(pos))
        {
            ret.emplace_back(pair<string,string>(
                    "Liquefied ground.", getLongDescription("liquefied")));
        }
        if (disjunction_haloed(pos))
        {
            ret.emplace_back(pair<string,string>(
                    "Translocational energy.", getLongDescription("disjunction haloed")));
        }
    }
    if (const cloud_type cloud = env.map_knowledge(pos).cloud())
    {
        ret.emplace_back(pair<string,string>(
                    "A cloud of "+cloud_type_name(cloud)+".", get_cloud_desc(cloud, false)));
    }
    return ret;
}

void get_feature_desc(const coord_def &pos, describe_info &inf, bool include_extra)
{
    dungeon_feature_type feat = env.map_knowledge(pos).feat();

    string desc      = feature_description_at(pos, false, DESC_A, false);
    string db_name   = feat == DNGN_ENTER_SHOP ? "a shop" : desc;
    string long_desc = getLongDescription(db_name);

    inf.title = uppercase_first(desc);
    if (!ends_with(desc, ".") && !ends_with(desc, "!")
        && !ends_with(desc, "?"))
    {
        inf.title += ".";
    }

    const string marker_desc =
        env.markers.property_at(pos, MAT_ANY, "feature_description_long");

    // suppress this if the feature changed out of view
    if (!marker_desc.empty() && grd(pos) == feat)
        long_desc += marker_desc;

    // Display branch descriptions on the entries to those branches.
    if (feat_is_stair(feat))
    {
        for (branch_iterator it; it; ++it)
        {
            if (it->entry_stairs == feat)
            {
                long_desc += "\n";
                long_desc += getLongDescription(it->shortname);
                break;
            }
        }
    }

    // mention the ability to pray at altars
    if (feat_is_altar(feat))
    {
        long_desc +=
            make_stringf("\n(Pray here with '%s' to learn more.)\n",
                         command_to_string(CMD_GO_DOWNSTAIRS).c_str());
    }

    inf.body << long_desc;

    if (include_extra)
    {
        auto extra_descs = _get_feature_extra_descs(pos);
        for (const auto &d : extra_descs)
            inf.body << (d == extra_descs.back() ? "" : "\n") << d.second;
    }

    inf.quote = getQuoteString(db_name);
}

void describe_feature_wide(const coord_def& pos)
{
    typedef struct {
        string title, body;
        tile_def tile;
    } feat_info;

    vector<feat_info> feats;

    {
        describe_info inf;
        get_feature_desc(pos, inf, false);
        feat_info f = { "", "", tile_def(TILEG_TODO, TEX_GUI)};
        f.title = inf.title;
        f.body = trimmed_string(inf.body.str());
#ifdef USE_TILE
        tileidx_t tile = tileidx_feature(pos);
        apply_variations(env.tile_flv(pos), &tile, pos);
        f.tile = tile_def(tile, get_dngn_tex(tile));
#endif
        feats.emplace_back(f);
    }
    auto extra_descs = _get_feature_extra_descs(pos);
    for (const auto &desc : extra_descs)
    {
        feat_info f = { "", "", tile_def(TILEG_TODO, TEX_GUI)};
        f.title = desc.first;
        f.body = trimmed_string(desc.second);
#ifdef USE_TILE
        if (desc.first == "A halo.")
            f.tile = tile_def(TILE_HALO_RANGE, TEX_FEAT);
        else if (desc.first == "An umbra.")
            f.tile = tile_def(TILE_UMBRA, TEX_FEAT);
        else if  (desc.first == "Liquefied ground.")
            f.tile = tile_def(TILE_LIQUEFACTION, TEX_FLOOR);
        else
            f.tile = tile_def(env.tile_bk_cloud(pos) & ~TILE_FLAG_FLYING, TEX_DEFAULT);
#endif
        feats.emplace_back(f);
    }
    if (crawl_state.game_is_hints())
    {
        string hint_text = trimmed_string(hints_describe_pos(pos.x, pos.y));
        if (!hint_text.empty())
        {
            feat_info f = { "", "", tile_def(TILEG_TODO, TEX_GUI)};
            f.title = "Hints.";
            f.body = hint_text;
            f.tile = tile_def(TILEG_STARTUP_HINTS, TEX_GUI);
            feats.emplace_back(f);
        }
    }

    auto scroller = make_shared<Scroller>();
    auto vbox = make_shared<Box>(Widget::VERT);

    for (const auto &feat : feats)
    {
        auto title_hbox = make_shared<Box>(Widget::HORZ);
#ifdef USE_TILE
        auto icon = make_shared<Image>();
        icon->set_tile(feat.tile);
        title_hbox->add_child(move(icon));
#endif
        auto title = make_shared<Text>(feat.title);
        title->set_margin_for_crt({0, 0, 0, 0});
        title->set_margin_for_sdl({0, 0, 0, 10});
        title_hbox->add_child(move(title));
        title_hbox->align_items = Widget::CENTER;

        bool has_desc = feat.body != feat.title && feat.body != "";
        if (has_desc || &feat != &feats.back())
        {
            title_hbox->set_margin_for_crt({0, 0, 1, 0});
            title_hbox->set_margin_for_sdl({0, 0, 20, 0});
        }
        vbox->add_child(move(title_hbox));

        if (has_desc)
        {
            auto text = make_shared<Text>(formatted_string::parse_string(feat.body));
            if (&feat != &feats.back())
                text->set_margin_for_sdl({0, 0, 20, 0});
            text->wrap_text = true;
            vbox->add_child(text);
        }
    }
#ifdef USE_TILE_LOCAL
    vbox->max_size()[0] = tiles.get_crt_font()->char_width()*80;
#endif
    scroller->set_child(move(vbox));

    auto popup = make_shared<ui::Popup>(scroller);

    bool done = false;
    popup->on(Widget::slots.event, [&](wm_event ev) {
        if (ev.type != WME_KEYDOWN)
            return false;
        done = !scroller->on_event(ev);
        return true;
    });

#ifdef USE_TILE_WEB
    tiles_crt_control disable_crt(false);
    tiles.json_open_object();
    tiles.json_open_array("feats");
    for (const auto &feat : feats)
    {
        tiles.json_open_object();
        tiles.json_write_string("title", feat.title);
        tiles.json_write_string("body", feat.body);
        tiles.json_open_object("tile");
        tiles.json_write_int("t", feat.tile.tile);
        tiles.json_write_int("tex", feat.tile.tex);
        if (feat.tile.ymax != TILE_Y)
            tiles.json_write_int("ymax", feat.tile.ymax);
        tiles.json_close_object();
        tiles.json_close_object();
    }
    tiles.json_close_array();
    tiles.push_ui_layout("describe-feature-wide", 0);
#endif

    ui::run_layout(move(popup), done);

#ifdef USE_TILE_WEB
    tiles.pop_ui_layout();
#endif
}

void describe_feature_type(dungeon_feature_type feat)
{
    describe_info inf;
    string name = feature_description(feat, NUM_TRAPS, "", DESC_A, false);
    string title = uppercase_first(name);
    if (!ends_with(title, ".") && !ends_with(title, "!") && !ends_with(title, "?"))
        title += ".";
    inf.title = title;
    inf.body << getLongDescription(name);
#ifdef USE_TILE
    const tileidx_t idx = tileidx_feature_base(feat);
    tile_def tile = tile_def(idx, get_dngn_tex(idx));
    show_description(inf, &tile);
#else
    show_description(inf);
#endif
}

void get_item_desc(const item_def &item, describe_info &inf)
{
    // Don't use verbose descriptions if the item contains spells,
    // so we can actually output these spells if space is scarce.
    const bool verbose = !item.has_spells();
    string name = item.name(DESC_INVENTORY_EQUIP) + ".";
    if (!in_inventory(item))
        name = uppercase_first(name);
    inf.body << name << get_item_description(item, verbose);
}

static vector<command_type> _allowed_actions(const item_def& item)
{
    vector<command_type> actions;
    actions.push_back(CMD_ADJUST_INVENTORY);
    if (item_equip_slot(item) == EQ_WEAPON)
        actions.push_back(CMD_UNWIELD_WEAPON);
    switch (item.base_type)
    {
    case OBJ_WEAPONS:
    case OBJ_STAVES:
        if (_could_set_training_target(item, false))
            actions.push_back(CMD_SET_SKILL_TARGET);
        // intentional fallthrough
    case OBJ_MISCELLANY:
        if (!item_is_equipped(item))
        {
            if (item_is_wieldable(item))
                actions.push_back(CMD_WIELD_WEAPON);
            if (is_throwable(&you, item))
                actions.push_back(CMD_QUIVER_ITEM);
        }
        break;
    case OBJ_MISSILES:
        if (_could_set_training_target(item, false))
            actions.push_back(CMD_SET_SKILL_TARGET);
        if (you.species != SP_FELID)
            actions.push_back(CMD_QUIVER_ITEM);
        break;
    case OBJ_ARMOUR:
        if (_could_set_training_target(item, false))
            actions.push_back(CMD_SET_SKILL_TARGET);
        if (item_is_equipped(item))
            actions.push_back(CMD_REMOVE_ARMOUR);
        else
            actions.push_back(CMD_WEAR_ARMOUR);
        break;
    case OBJ_FOOD:
        if (can_eat(item, true, false))
            actions.push_back(CMD_EAT);
        break;
    case OBJ_SCROLLS:
    //case OBJ_BOOKS: these are handled differently
        actions.push_back(CMD_READ);
        break;
    case OBJ_JEWELLERY:
        if (item_is_equipped(item))
            actions.push_back(CMD_REMOVE_JEWELLERY);
        else
            actions.push_back(CMD_WEAR_JEWELLERY);
        break;
    case OBJ_POTIONS:
        if (!you_foodless()) // mummies and lich form forbidden
            actions.push_back(CMD_QUAFF);
        break;
    default:
        ;
    }
#if defined(CLUA_BINDINGS)
    if (clua.callbooleanfn(false, "ch_item_wieldable", "i", &item))
        actions.push_back(CMD_WIELD_WEAPON);
#endif

    if (item_is_evokable(item))
        actions.push_back(CMD_EVOKE);

    actions.push_back(CMD_DROP);

    if (!crawl_state.game_is_tutorial())
        actions.push_back(CMD_INSCRIBE_ITEM);

    return actions;
}

static string _actions_desc(const vector<command_type>& actions, const item_def& item)
{
    static const map<command_type, string> act_str =
    {
        { CMD_WIELD_WEAPON, TR7("(w)ield","(w)장비") },
        { CMD_UNWIELD_WEAPON, TR7("(u)nwield","(u)해제") },
        { CMD_QUIVER_ITEM, TR7("(q)uiver","(q)장착") },
        { CMD_WEAR_ARMOUR, TR7("(w)ear","(q)착용") },
        { CMD_REMOVE_ARMOUR, TR7("(t)ake off","(t)벗기") },
        { CMD_EVOKE, TR7("e(v)oke","(v)발동") },
        { CMD_EAT, TR7("(e)at","(e)식사") },
        { CMD_READ, TR7("(r)ead","(r)읽기") },
        { CMD_WEAR_JEWELLERY, TR7("(p)ut on","(p)장착") },
        { CMD_REMOVE_JEWELLERY, TR7("(r)emove","(r)해제") },
        { CMD_QUAFF, TR7("(q)uaff","(q)마시기") },
        { CMD_DROP, TR7("(d)rop","(d)버림") },
        { CMD_INSCRIBE_ITEM, TR7("(i)nscribe","(i)문장 새기기") },
        { CMD_ADJUST_INVENTORY, TR7("(=)adjust","(=)단축키 변경") },
        { CMD_SET_SKILL_TARGET, TR7("(s)kill","(s)스킬 목표지정") },
    };
    return comma_separated_fn(begin(actions), end(actions),
                                [] (command_type cmd)
                                {
                                    return act_str.at(cmd);
                                },
                                TR7(", or "," 혹은 "))
           + TR7(" the "," - 대상 : ") + item.name(DESC_BASENAME) + TR7(".", "");
}

// Take a key and a list of commands and return the command from the list
// that corresponds to the key. Note that some keys are overloaded (but with
// mutually-exclusive actions), so it's not just a simple lookup.
static command_type _get_action(int key, vector<command_type> actions)
{
    static const map<command_type, int> act_key =
    {
        { CMD_WIELD_WEAPON,     'w' },
        { CMD_UNWIELD_WEAPON,   'u' },
        { CMD_QUIVER_ITEM,      'q' },
        { CMD_WEAR_ARMOUR,      'w' },
        { CMD_REMOVE_ARMOUR,    't' },
        { CMD_EVOKE,            'v' },
        { CMD_EAT,              'e' },
        { CMD_READ,             'r' },
        { CMD_WEAR_JEWELLERY,   'p' },
        { CMD_REMOVE_JEWELLERY, 'r' },
        { CMD_QUAFF,            'q' },
        { CMD_DROP,             'd' },
        { CMD_INSCRIBE_ITEM,    'i' },
        { CMD_ADJUST_INVENTORY, '=' },
        { CMD_SET_SKILL_TARGET, 's' },
    };

    key = tolower(key);

    for (auto cmd : actions)
        if (key == act_key.at(cmd))
            return cmd;

    return CMD_NO_CMD;
}

/**
 * Do the specified action on the specified item.
 *
 * @param item    the item to have actions done on
 * @param actions the list of actions to search in
 * @param keyin   the key that was pressed
 * @return whether to stay in the inventory menu afterwards
 */
static bool _do_action(item_def &item, const vector<command_type>& actions, int keyin)
{
    const command_type action = _get_action(keyin, actions);
    if (action == CMD_NO_CMD)
        return true;

    const int slot = item.link;
    ASSERT_RANGE(slot, 0, ENDOFPACK);

    switch (action)
    {
    case CMD_WIELD_WEAPON:     wield_weapon(true, slot);            break;
    case CMD_UNWIELD_WEAPON:   wield_weapon(true, SLOT_BARE_HANDS); break;
    case CMD_QUIVER_ITEM:      quiver_item(slot);                   break;
    case CMD_WEAR_ARMOUR:      wear_armour(slot);                   break;
    case CMD_REMOVE_ARMOUR:    takeoff_armour(slot);                break;
    case CMD_EVOKE:            evoke_item(slot);                    break;
    case CMD_EAT:              eat_food(slot);                      break;
    case CMD_READ:             read(&item);                         break;
    case CMD_WEAR_JEWELLERY:   puton_ring(slot);                    break;
    case CMD_REMOVE_JEWELLERY: remove_ring(slot, true);             break;
    case CMD_QUAFF:            drink(&item);                        break;
    case CMD_DROP:             drop_item(slot, item.quantity);      break;
    case CMD_INSCRIBE_ITEM:    inscribe_item(item);                 break;
    case CMD_ADJUST_INVENTORY: adjust_item(slot);                   break;
    case CMD_SET_SKILL_TARGET: target_item(item);                   break;
    default:
        die("illegal inventory cmd %d", action);
    }
    return false;
}

void target_item(item_def &item)
{
    const skill_type skill = _item_training_skill(item);
    if (skill == SK_NONE)
        return;

    const int target = _item_training_target(item);
    if (target == 0)
        return;

    you.set_training_target(skill, target, true);
    // ensure that the skill is at least enabled
    if (you.train[skill] == TRAINING_DISABLED)
        you.train[skill] = TRAINING_ENABLED;
    you.train_alt[skill] = you.train[skill];
    reset_training();
}

/**
 *  Describe any item in the game.
 *
 *  @param item       the item to be described.
 *  @param fixup_desc a function (possibly null) to modify the
 *                    description before it's displayed.
 *  @return whether to stay in the inventory menu afterwards.
 */
bool describe_item(item_def &item, function<void (string&)> fixup_desc)
{
    if (!item.defined())
        return true;

    string name = item.name(DESC_INVENTORY_EQUIP) + ".";
    if (!in_inventory(item))
        name = uppercase_first(name);

    string desc = get_item_description(item, true, false);

    string quote;
    if (is_unrandom_artefact(item) && item_type_known(item))
        quote = getQuoteString(get_artefact_name(item));
    else
        quote = getQuoteString(item.name(DESC_DBNAME, true, false, false));

    if (!(crawl_state.game_is_hints_tutorial()
          || quote.empty()))
    {
        desc += "\n\n" + quote;
    }

    if (crawl_state.game_is_hints())
        desc += "\n\n" + hints_describe_item(item);

    if (fixup_desc)
        fixup_desc(desc);

    formatted_string fs_desc = formatted_string::parse_string(desc);

    spellset spells = item_spellset(item);
    formatted_string spells_desc;
    describe_spellset(spells, &item, spells_desc, nullptr);
#ifdef USE_TILE_WEB
    string desc_without_spells = fs_desc.to_colour_string();
#endif
    fs_desc += spells_desc;

    const bool do_actions = in_inventory(item) // Dead men use no items.
            && !(you.pending_revival || crawl_state.updating_scores);

    vector<command_type> actions;
    if (do_actions)
        actions = _allowed_actions(item);

    auto vbox = make_shared<Box>(Widget::VERT);
    auto title_hbox = make_shared<Box>(Widget::HORZ);

#ifdef USE_TILE
    vector<tile_def> item_tiles;
    get_tiles_for_item(item, item_tiles, true);
    if (item_tiles.size() > 0)
    {
        auto tiles_stack = make_shared<Stack>();
        for (const auto &tile : item_tiles)
        {
            auto icon = make_shared<Image>();
            icon->set_tile(tile);
            tiles_stack->add_child(move(icon));
        }
        title_hbox->add_child(move(tiles_stack));
    }
#endif

    auto title = make_shared<Text>(name);
    title->set_margin_for_crt({0, 0, 0, 0});
    title->set_margin_for_sdl({0, 0, 0, 10});
    title_hbox->add_child(move(title));

    title_hbox->align_items = Widget::CENTER;
    title_hbox->set_margin_for_crt({0, 0, 1, 0});
    title_hbox->set_margin_for_sdl({0, 0, 20, 0});
    vbox->add_child(move(title_hbox));

    auto scroller = make_shared<Scroller>();
    auto text = make_shared<Text>(fs_desc.trim());
    text->wrap_text = true;
    scroller->set_child(text);
    vbox->add_child(scroller);

    formatted_string footer_text("", CYAN);
    if (!actions.empty())
    {
        if (!spells.empty())
            footer_text.cprintf("Select a spell, or ");
        footer_text += formatted_string(_actions_desc(actions, item));
        auto footer = make_shared<Text>();
        footer->set_text(footer_text);
        footer->set_margin_for_crt({1, 0, 0, 0});
        footer->set_margin_for_sdl({20, 0, 0, 0});
        vbox->add_child(move(footer));
    }

#ifdef USE_TILE_LOCAL
    vbox->max_size()[0] = tiles.get_crt_font()->char_width()*80;
#endif

    auto popup = make_shared<ui::Popup>(move(vbox));

    bool done = false;
    command_type action;
    int lastch;
    popup->on(Widget::slots.event, [&](wm_event ev) {
        if (ev.type != WME_KEYDOWN)
            return false;
        int key = ev.key.keysym.sym;
        key = key == '{' ? 'i' : key;
        lastch = key;
        action = _get_action(key, actions);
        if (action != CMD_NO_CMD)
            done = true;
        else if (key == ' ' || key == CK_ESCAPE)
            done = true;
        const vector<pair<spell_type,char>> spell_map = map_chars_to_spells(spells, &item);
        auto entry = find_if(spell_map.begin(), spell_map.end(),
                [key](const pair<spell_type,char>& e) { return e.second == key; });
        if (entry == spell_map.end())
            return false;
        describe_spell(entry->first, nullptr, &item);
        done = already_learning_spell();
        return true;
    });

#ifdef USE_TILE_WEB
    tiles_crt_control disable_crt(false);
    tiles.json_open_object();
    tiles.json_write_string("title", name);
    desc_without_spells += "SPELLSET_PLACEHOLDER";
    trim_string(desc_without_spells);
    tiles.json_write_string("body", desc_without_spells);
    write_spellset(spells, &item, nullptr);

    tiles.json_write_string("actions", footer_text.tostring());
    tiles.json_open_array("tiles");
    for (const auto &tile : item_tiles)
    {
        tiles.json_open_object();
        tiles.json_write_int("t", tile.tile);
        tiles.json_write_int("tex", tile.tex);
        if (tile.ymax != TILE_Y)
            tiles.json_write_int("ymax", tile.ymax);
        tiles.json_close_object();
    }
    tiles.json_close_array();
    tiles.push_ui_layout("describe-item", 0);
#endif

    ui::run_layout(move(popup), done);

#ifdef USE_TILE_WEB
    tiles.pop_ui_layout();
#endif

    if (action != CMD_NO_CMD)
        return _do_action(item, actions, lastch);
    else if (item.has_spells())
    {
        // only continue the inventory loop if we didn't start memorizing a
        // spell & didn't destroy the item for amnesia.
        return !already_learning_spell();
    }
    return true;
}

void inscribe_item(item_def &item)
{
    mprf_nocap(MSGCH_EQUIPMENT, "%s", item.name(DESC_INVENTORY).c_str());

    const bool is_inscribed = !item.inscription.empty();
    string prompt = is_inscribed ? TR7("Replace inscription with what? ","문장을 무엇으로 교체할 것인가? ")
                                 : TR7("Inscribe with what? ","무슨 문장을 새길 것인가? ");

    char buf[79];
    int ret = msgwin_get_line(prompt, buf, sizeof buf, nullptr,
                              item.inscription);
    if (ret)
    {
        canned_msg(MSG_OK);
        return;
    }

    string new_inscrip = buf;
    trim_string_right(new_inscrip);

    if (item.inscription == new_inscrip)
    {
        canned_msg(MSG_OK);
        return;
    }

    item.inscription = new_inscrip;

    mprf_nocap(MSGCH_EQUIPMENT, "%s", item.name(DESC_INVENTORY).c_str());
    you.wield_change  = true;
    you.redraw_quiver = true;
}

/**
 * List the simple calculated stats of a given spell, when cast by the player
 * in their current condition.
 *
 * @param spell     The spell in question.
 */
static string _player_spell_stats(const spell_type spell)
{
    string description;
    description += make_stringf("\nLevel: %d", spell_difficulty(spell));

    const string schools = spell_schools_string(spell);
    description +=
        make_stringf("        School%s: %s",
                     schools.find("/") != string::npos ? "s" : "",
                     schools.c_str());

    if (!crawl_state.need_save
        || (get_spell_flags(spell) & SPFLAG_MONSTER))
    {
        return description; // all other info is player-dependent
    }

    const string failure = failure_rate_to_string(raw_spell_fail(spell));
    description += make_stringf("        Fail: %s", failure.c_str());

    description += TR7("\n\nPower : ","\n\n위력   : ");
    description += spell_power_string(spell);
    description += TR7("\nRange : ","\n사정거리 : ");
    description += spell_range_string(spell);
    description += TR7("\nHunger: ","\n만복도  : ");
    description += spell_hunger_string(spell);
    description += TR7("\nNoise : ","\n소음   : ");
    description += spell_noise_string(spell);
    description += "\n";
    return description;
}

string get_skill_description(skill_type skill, bool need_title)
{
    string lookup = skill_name(skill);
    string result = "";

    if (need_title)
    {
        result = lookup;
        result += "\n\n";
    }

    result += getLongDescription(lookup);

    switch (skill)
    {
        case SK_INVOCATIONS:
            if (you.species == SP_DEMIGOD)
            {
                result += "\n";
                result += TR7("How on earth did you manage to pick this up?","어떻게 반신족이 기도술의 설명을 볼 수 있었는가?");
            }
            else if (you_worship(GOD_TROG))
            {
                result += "\n";
                result += TR7("Note that Trog doesn't use Invocations, due to its ","트로그의 권능은 기도술에 의지하지 않음을 기억하라.")
                          TR7("close connection to magic.","기도술은 마법과 밀접한 관련이 있다.");
            }
            break;

        case SK_SPELLCASTING:
            if (you_worship(GOD_TROG))
            {
                result += "\n";
                result += TR7("Keep in mind, though, that Trog will greatly ","명심해라.")
                          TR7("disapprove of this.","주문을 사용하는 것은 '트로그'의 큰 반감을 사는 행위이다.");
            }
            break;
        default:
            // No further information.
            break;
    }

    return result;
}

/// How much power do we think the given monster casts this spell with?
static int _hex_pow(const spell_type spell, const int hd)
{
    const int cap = 200;
    const int pow = mons_power_for_hd(spell, hd, false) / ENCH_POW_FACTOR;
    return min(cap, pow);
}

/**
 * What are the odds of the given spell, cast by a monster with the given
 * spell_hd, affecting the player?
 */
int hex_chance(const spell_type spell, const int hd)
{
    const int capped_pow = _hex_pow(spell, hd);
    const int chance = hex_success_chance(you.res_magic(), capped_pow,
                                          100, true);
    if (spell == SPELL_STRIP_RESISTANCE)
        return chance + (100 - chance) / 3; // ignores mr 1/3rd of the time
    return chance;
}

/**
 * Describe mostly non-numeric player-specific information about a spell.
 *
 * (E.g., your god's opinion of it, whether it's in a high-level book that
 * you can't memorise from, whether it's currently useless for whatever
 * reason...)
 *
 * @param spell     The spell in question.
 */
static string _player_spell_desc(spell_type spell)
{
    if (!crawl_state.need_save || (get_spell_flags(spell) & SPFLAG_MONSTER))
        return ""; // all info is player-dependent

    string description;

    // Report summon cap
    const int limit = summons_limit(spell);
    if (limit)
    {
        description += TR7("You can sustain at most ","당신은 이 주문을 통해, 최대 ") + number_in_words(limit)
                        + TR7(" creature","인(마리)") + (limit > 1 ? TR7("s","") : "")
                        + TR7(" summoned by this spell.\n","의 소환물을 유지시킬 수 있다.\n");
    }

    if (god_hates_spell(spell, you.religion))
    {
        description += uppercase_first(god_name(you.religion))
                       + TR7(" frowns upon the use of this spell.\n","은(는) 이 마법을 사용하시는 것을 내켜하지 않는다.\n");
        if (god_loathes_spell(spell, you.religion))
            description += TR7("You'd be excommunicated if you dared to cast it!\n","이 주문을 외우면 즉시 파문당할 것이다!\n");
    }
    else if (god_likes_spell(spell, you.religion))
    {
        description += uppercase_first(god_name(you.religion))
                       + TR7(" supports the use of this spell.\n","은(는) 이 마법의 사용을 돕는다.\n");
    }

    if (!you_can_memorise(spell))
    {
        description += TR7("\nYou cannot memorise this spell because ","\n_")
                       + desc_cannot_memorise_reason(spell)
                       + "\n";
    }
    else if (spell_is_useless(spell, true, false))
    {
        description += TR7("\nThis spell will have no effect right now because ","\n_")
                       + spell_uselessness_reason(spell, true, false)
                       + "\n";
    }

    return description;
}


/**
 * Describe a spell, as cast by the player.
 *
 * @param spell     The spell in question.
 * @return          Information about the spell; does not include the title or
 *                  db description, but does include level, range, etc.
 */
string player_spell_desc(spell_type spell)
{
    return _player_spell_stats(spell) + _player_spell_desc(spell);
}

/**
 * Examine a given spell. Set the given string to its description, stats, &c.
 * If it's a book in a spell that the player is holding, mention the option to
 * memorise it.
 *
 * @param spell         The spell in question.
 * @param mon_owner     If this spell is being examined from a monster's
 *                      description, 'spell' is that monster. Else, null.
 * @param description   Set to the description & details of the spell.
 * @param item          The item holding the spell, if any.
 * @return              Whether you can memorise the spell.
 */
static bool _get_spell_description(const spell_type spell,
                                  const monster_info *mon_owner,
                                  string &description,
                                  const item_def* item = nullptr)
{
    description.reserve(500);

    const string long_descrip = getLongDescription(string(spell_title(spell))
                                                   + " spell");

    if (!long_descrip.empty())
        description += long_descrip;
    else
    {
        description += TR7("This spell has no description. ","_")
                       TR7("Casting it may therefore be unwise. ","_")
#ifdef DEBUG
                       TR7("Instead, go fix it. ","_");
#else
                       TR7("Please file a bug report.","_");
#endif
    }

    if (mon_owner)
    {
        const int hd = mon_owner->spell_hd();
        const int range = mons_spell_range(spell, hd);
        description += TR7("\nRange : ", "\n사정거리 : ")
                       + range_string(range, range, mons_char(mon_owner->type))
                       + "\n";

        // only display this if the player exists (not in the main menu)
        if (crawl_state.need_save && (get_spell_flags(spell) & SPFLAG_MR_CHECK)
#ifndef DEBUG_DIAGNOSTICS
            && mon_owner->attitude != ATT_FRIENDLY
#endif
            )
        {
            string wiz_info;
#ifdef WIZARD
            if (you.wizard)
                wiz_info += make_stringf(" (pow %d)", _hex_pow(spell, hd));
#endif
            description += make_stringf("Chance to beat your MR: %d%%%s\n",
                                        hex_chance(spell, hd),
                                        wiz_info.c_str());
        }

    }
    else
        description += player_spell_desc(spell);

    // Don't allow memorization after death.
    // (In the post-game inventory screen.)
    if (crawl_state.player_is_dead())
        return false;

    const string quote = getQuoteString(string(spell_title(spell)) + " spell");
    if (!quote.empty())
        description += "\n" + quote;

    if (item && item->base_type == OBJ_BOOKS
        && (in_inventory(*item)
            || item->pos == you.pos() && !is_shop_item(*item))
        && !you.has_spell(spell) && you_can_memorise(spell))
    {
        return true;
    }

    return false;
}

/**
 * Make a list of all books that contain a given spell.
 *
 * @param spell_type spell      The spell in question.
 * @return                      A formatted list of books containing
 *                              the spell, e.g.:
 *    \n\nThis spell can be found in the following books: dreams, burglary.
 *    or
 *    \n\nThis spell is not found in any books.
 */
static string _spell_sources(const spell_type spell)
{
    item_def item;
    set_ident_flags(item, ISFLAG_IDENT_MASK);
    vector<string> books;

    item.base_type = OBJ_BOOKS;
    for (int i = 0; i < NUM_FIXED_BOOKS; i++)
    {
        if (item_type_removed(OBJ_BOOKS, i))
            continue;
        for (spell_type sp : spellbook_template(static_cast<book_type>(i)))
            if (sp == spell)
            {
                item.sub_type = i;
                books.push_back(item.name(DESC_PLAIN));
            }
    }

    if (books.empty())
        return TR7("\nThis spell is not found in any books.","\n이 주문은 마법서로부터는 찾을 수 없다.");

    string desc;

    desc += TR7("\nThis spell can be found in the following book","\n이 주문은 다음 마법서들에서 습득할 수 있다");
    if (books.size() > 1)
        desc += "s";
    desc += ":\n ";
    desc += comma_separated_line(books.begin(), books.end(), "\n ", "\n ");

    return desc;
}

/**
 * Provide the text description of a given spell.
 *
 * @param spell     The spell in question.
 * @param inf[out]  The spell's description is concatenated onto the end of
 *                  inf.body.
 */
void get_spell_desc(const spell_type spell, describe_info &inf)
{
    string desc;
    _get_spell_description(spell, nullptr, desc);
    inf.body << desc;
}

/**
 * Examine a given spell. List its description and details, and handle
 * memorizing the spell in question, if the player is able & chooses to do so.
 *
 * @param spelled   The spell in question.
 * @param mon_owner If this spell is being examined from a monster's
 *                  description, 'mon_owner' is that monster. Else, null.
 * @param item      The item holding the spell, if any.
 */
void describe_spell(spell_type spell, const monster_info *mon_owner,
                    const item_def* item, bool show_booklist)
{
    string desc;
    bool can_mem = _get_spell_description(spell, mon_owner, desc, item);
    if (show_booklist)
        desc += _spell_sources(spell);

    auto vbox = make_shared<Box>(Widget::VERT);
#ifdef USE_TILE_LOCAL
    vbox->max_size()[0] = tiles.get_crt_font()->char_width()*80;
#endif

    auto title_hbox = make_shared<Box>(Widget::HORZ);
#ifdef USE_TILE
    auto spell_icon = make_shared<Image>();
    spell_icon->set_tile(tile_def(tileidx_spell(spell), TEX_GUI));
    title_hbox->add_child(move(spell_icon));
#endif

    string spl_title = spell_title(spell);
    trim_string(desc);

    auto title = make_shared<Text>();
    title->set_text(formatted_string(spl_title));
    title->set_margin_for_crt({0, 0, 0, 0});
    title->set_margin_for_sdl({0, 0, 0, 10});
    title_hbox->add_child(move(title));

    title_hbox->align_items = Widget::CENTER;
    title_hbox->set_margin_for_crt({0, 0, 1, 0});
    title_hbox->set_margin_for_sdl({0, 0, 20, 0});
    vbox->add_child(move(title_hbox));

    auto scroller = make_shared<Scroller>();
    auto text = make_shared<Text>();
    text->set_text(formatted_string::parse_string(desc));
    text->wrap_text = true;
    scroller->set_child(move(text));
    vbox->add_child(scroller);

    if (can_mem)
    {
        auto more = make_shared<Text>();
        more->set_text(formatted_string(TR7("(M)emorise this spell.","(M)이 마법을 암기한다."), CYAN));
        more->set_margin_for_crt({1, 0, 0, 0});
        more->set_margin_for_sdl({20, 0, 0, 0});
        vbox->add_child(move(more));
    }

    auto popup = make_shared<ui::Popup>(move(vbox));

    bool done = false;
    int lastch;
    popup->on(Widget::slots.event, [&done, &lastch](wm_event ev) {
        if (ev.type != WME_KEYDOWN)
            return false;
        lastch = ev.key.keysym.sym;
        done = (toupper(lastch) == 'M' || lastch == CK_ESCAPE);
        return done;
    });

#ifdef USE_TILE_WEB
    tiles.json_open_object();
    auto tile = tile_def(tileidx_spell(spell), TEX_GUI);
    tiles.json_open_object("tile");
    tiles.json_write_int("t", tile.tile);
    tiles.json_write_int("tex", tile.tex);
    if (tile.ymax != TILE_Y)
        tiles.json_write_int("ymax", tile.ymax);
    tiles.json_close_object();
    tiles.json_write_string("title", spl_title);
    tiles.json_write_string("desc", desc);
    tiles.json_write_bool("can_mem", can_mem);
    tiles.push_ui_layout("describe-spell", 0);
#endif

    ui::run_layout(move(popup), done);

#ifdef USE_TILE_WEB
    tiles.pop_ui_layout();
#endif

    if (toupper(lastch) == 'M')
    {
        redraw_screen(); // necessary to ensure stats is redrawn (!?)
        if (!learn_spell(spell) || !you.turn_is_over)
            more();
    }
}

/**
 * Examine a given ability. List its description and details.
 *
 * @param ability   The ability in question.
 */
void describe_ability(ability_type ability)
{
    describe_info inf;
    inf.title = ability_name(ability);
    inf.body << get_ability_desc(ability, false);
#ifdef USE_TILE
    tile_def tile = tile_def(tileidx_ability(ability), TEX_GUI);
    show_description(inf, &tile);
#else
    show_description(inf);
#endif
}


static string _describe_draconian(const monster_info& mi)
{
    string description;
    const int subsp = mi.draco_or_demonspawn_subspecies();

    if (subsp != mi.type)
    {
        description += TR7("It has ","");

        switch (subsp)
        {
        case MONS_BLACK_DRACONIAN:      description += TR7("black ","검은 ");   break;
        case MONS_YELLOW_DRACONIAN:     description += TR7("yellow ","노란 ");  break;
        case MONS_GREEN_DRACONIAN:      description += TR7("green ","녹색 ");   break;
        case MONS_PURPLE_DRACONIAN:     description += TR7("purple ","보라색 ");  break;
        case MONS_RED_DRACONIAN:        description += TR7("red ","붉은색 ");     break;
        case MONS_WHITE_DRACONIAN:      description += TR7("white ","흰색 ");   break;
        case MONS_GREY_DRACONIAN:       description += TR7("grey ","회색 ");    break;
        case MONS_PALE_DRACONIAN:       description += TR7("pale ","옅은 색 ");    break;
        default:
            break;
        }

        description += TR7("scales. ","비늘이 있다.");
    }

    switch (subsp)
    {
    case MONS_BLACK_DRACONIAN:
        description += TR7("Sparks flare out of its mouth and nostrils.","입과 콧구멍에서 스파크가 튄다.");
        break;
    case MONS_YELLOW_DRACONIAN:
        description += TR7("Acidic fumes swirl around it.","산성 가스가 주위를 소용돌이 친다.");
        break;
    case MONS_GREEN_DRACONIAN:
        description += TR7("Venom drips from its jaws.","턱에서 독액이 뚝뚝 떨어진다.");
        break;
    case MONS_PURPLE_DRACONIAN:
        description += TR7("Its outline shimmers with magical energy.","외곽선이 마법적 에너지로 반짝인다.");
        break;
    case MONS_RED_DRACONIAN:
        description += TR7("Smoke pours from its nostrils.","연기가 콧구멍에서 흘러나온다.");
        break;
    case MONS_WHITE_DRACONIAN:
        description += TR7("Frost pours from its nostrils.","냉기가 콧구멍에서 흘러나온다.");
        break;
    case MONS_GREY_DRACONIAN:
        description += TR7("Its scales and tail are adapted to the water.","비늘과 꼬리가 물에 쉽게 적응한다.");
        break;
    case MONS_PALE_DRACONIAN:
        description += TR7("It is cloaked in a pall of superheated steam.","과열된 증기로 은폐하고 있다.");
        break;
    default:
        break;
    }

    return description;
}

static string _describe_demonspawn_role(monster_type type)
{
    switch (type)
    {
    case MONS_BLOOD_SAINT:
        return TR7("It weaves powerful and unpredictable spells of devastation.","강력하고 예측할 수 없는 황폐의 마법을 만듭니다.");
    case MONS_WARMONGER:
        return TR7("It is devoted to combat, disrupting the magic of its foes as ","끝없이 전투를 벌여 적들의 마법을 혼란시킵니다.")
               TR7("it battles endlessly.","");
    case MONS_CORRUPTER:
        return TR7("It corrupts space around itself, and can twist even the very ","주변의 공간을 오염시키고 상대방의 살점을 뒤틀어버릴 수 있습니다.")
               TR7("flesh of its opponents.","");
    case MONS_BLACK_SUN:
        return TR7("It shines with an unholy radiance, and wields powers of ","부정한 파장을 흩뿌리며, 죽음의 신에 대한 헌신을 위해 어둠의 군세를 휘두릅니다.")
               TR7("darkness from its devotion to the deities of death.","");
    default:
        return "";
    }
}

static string _describe_demonspawn_base(int species)
{
    switch (species)
    {
    case MONS_MONSTROUS_DEMONSPAWN:
        return TR7("It is more beast now than whatever species it is descended from.","어떤 종보다도 더욱 짐승같습니다.");
    case MONS_GELID_DEMONSPAWN:
        return TR7("It is covered in icy armour.","얼음 갑옷으로 감싸져 있습니다.");
    case MONS_INFERNAL_DEMONSPAWN:
        return TR7("It gives off an intense heat.","엄청난 열을 내뿜고 있습니다.");
    case MONS_TORTUROUS_DEMONSPAWN:
        return TR7("It menaces with bony spines.","뼈처럼 튼튼한 가시로 위협합니다.");
    }
    return "";
}

static string _describe_demonspawn(const monster_info& mi)
{
    string description;
    const int subsp = mi.draco_or_demonspawn_subspecies();

    description += _describe_demonspawn_base(subsp);

    if (subsp != mi.type)
    {
        const string demonspawn_role = _describe_demonspawn_role(mi.type);
        if (!demonspawn_role.empty())
            description += " " + demonspawn_role;
    }

    return description;
}

static const char* _get_resist_name(mon_resist_flags res_type)
{
    switch (res_type)
    {
    case MR_RES_ELEC:
        return TR7("electricity","전기적인");
    case MR_RES_POISON:
        return TR7("poison","독의");
    case MR_RES_FIRE:
        return TR7("fire","화염의");
    case MR_RES_STEAM:
        return TR7("steam","증기의");
    case MR_RES_COLD:
        return TR7("cold","냉기의");
    case MR_RES_ACID:
        return TR7("acid","산성의");
    case MR_RES_ROTTING:
        return TR7("rotting","부패의");
    case MR_RES_NEG:
        return TR7("negative energy","음에너지의");
    case MR_RES_DAMNATION:
        return TR7("damnation","파멸의 저주의");
    case MR_RES_TORNADO:
        return TR7("tornadoes","폭풍의");
    default:
        return TR7("buggy resistance","버그 저항의");
    }
}

static const char* _get_threat_desc(mon_threat_level_type threat)
{
    switch (threat)
    {
    case MTHRT_TRIVIAL: return TR7("harmless","무해한");
    case MTHRT_EASY:    return TR7("easy","쉬운");
    case MTHRT_TOUGH:   return TR7("dangerous","위험한");
    case MTHRT_NASTY:   return TR7("extremely dangerous","극히 위험한");
    case MTHRT_UNDEF:
    default:            return TR7("buggily threatening","버그 위협의");
    }
}

/**
 * Describe monster attack 'flavours' that trigger before the attack.
 *
 * @param flavour   The flavour in question; e.g. AF_SWOOP.
 * @return          A description of anything that happens 'before' an attack
 *                  with the given flavour;
 *                  e.g. "swoop behind its target and ".
 */
static const char* _special_flavour_prefix(attack_flavour flavour)
{
    switch (flavour)
    {
        case AF_KITE:
            return TR7("retreat from adjacent foes and ","인접한 적들로부터 물러나고 ");
        case AF_SWOOP:
            return TR7("swoop behind its foe and ","적의 뒤에서 기습하고 ");
        default:
            return "";
    }
}

/**
 * Describe monster attack 'flavours' that have extra range.
 *
 * @param flavour   The flavour in question; e.g. AF_REACH_STING.
 * @return          If the flavour has extra-long range, say so. E.g.,
 *                  " from a distance". (Else "").
 */
static const char* _flavour_range_desc(attack_flavour flavour)
{
    if (flavour == AF_REACH || flavour == AF_REACH_STING)
        return TR7(" from a distance"," 원거리에서");
    return "";
}

static string _flavour_base_desc(attack_flavour flavour)
{
    static const map<attack_flavour, string> base_descs = {
        { AF_ACID,              TR7("deal extra acid damage","추가적인 산성 데미지를")},
        { AF_BLINK,             TR7("blink itself","스스로 블링크") },
        { AF_COLD,              TR7("deal up to %d extra cold damage","_") },
        { AF_CONFUSE,           TR7("cause confusion","_") },
        { AF_DRAIN_STR,         TR7("drain strength","_") },
        { AF_DRAIN_INT,         TR7("drain intelligence","_") },
        { AF_DRAIN_DEX,         TR7("drain dexterity","_") },
        { AF_DRAIN_STAT,        TR7("drain strength, intelligence or dexterity","_") },
        { AF_DRAIN_XP,          TR7("drain skills","_") },
        { AF_ELEC,              TR7("deal up to %d extra electric damage","_") },
        { AF_FIRE,              TR7("deal up to %d extra fire damage","_") },
        { AF_HUNGER,            TR7("cause hunger","_") },
        { AF_MUTATE,            TR7("cause mutations","_") },
        { AF_POISON_PARALYSE,   TR7("poison and cause paralysis or slowing","_") },
        { AF_POISON,            TR7("cause poisoning","_") },
        { AF_POISON_STRONG,     TR7("cause strong poisoning","_") },
        { AF_ROT,               TR7("cause rotting","_") },
        { AF_VAMPIRIC,          TR7("drain health from the living","_") },
        { AF_KLOWN,             TR7("cause random powerful effects","_") },
        { AF_DISTORT,           TR7("cause wild translocation effects","_") },
        { AF_RAGE,              TR7("cause berserking","_") },
        { AF_STICKY_FLAME,      TR7("apply sticky flame","_") },
        { AF_CHAOTIC,           TR7("cause unpredictable effects","_") },
        { AF_STEAL,             TR7("steal items","_") },
        { AF_CRUSH,             TR7("begin ongoing constriction","_") },
        { AF_REACH,             "" },
        { AF_HOLY,              TR7("deal extra damage to undead and demons","_") },
        { AF_ANTIMAGIC,         TR7("drain magic","_") },
        { AF_PAIN,              TR7("cause pain to the living","_") },
        { AF_ENSNARE,           TR7("ensnare with webbing","_") },
        { AF_ENGULF,            TR7("engulf with water","_") },
        { AF_PURE_FIRE,         "" },
        { AF_DRAIN_SPEED,       TR7("drain speed","_") },
        { AF_VULN,              TR7("reduce resistance to hostile enchantments","_") },
        { AF_SHADOWSTAB,        TR7("deal increased damage when unseen","_") },
        { AF_DROWN,             TR7("deal drowning damage","_") },
        { AF_CORRODE,           TR7("cause corrosion","_") },
        { AF_SCARAB,            TR7("drain speed and drain health","_") },
        { AF_TRAMPLE,           TR7("knock back the defender","_") },
        { AF_REACH_STING,       TR7("cause poisoning","_") },
        { AF_WEAKNESS,          TR7("cause weakness","_") },
        { AF_KITE,              "" },
        { AF_SWOOP,             "" },
        { AF_PLAIN,             "" },
    };

    const string* desc = map_find(base_descs, flavour);
    ASSERT(desc);
    return *desc;
}

/**
 * Provide a short, and-prefixed flavour description of the given attack
 * flavour, if any.
 *
 * @param flavour  E.g. AF_COLD, AF_PLAIN.
 * @param HD       The hit dice of the monster using the flavour.
 * @return         "" if AF_PLAIN; else " <desc>", e.g.
 *                 " to deal up to 27 extra cold damage if any damage is dealt".
 */
static string _flavour_effect(attack_flavour flavour, int HD)
{
    const string base_desc = _flavour_base_desc(flavour);
    if (base_desc.empty())
        return base_desc;

    const int flavour_dam = flavour_damage(flavour, HD, false);
    const string flavour_desc = make_stringf(base_desc.c_str(), flavour_dam);

    if (!flavour_triggers_damageless(flavour)
        && flavour != AF_KITE && flavour != AF_SWOOP)
    {
        return " to " + flavour_desc + " if any damage is dealt";
    }

    return " to " + flavour_desc;
}

struct mon_attack_info
{
    mon_attack_def definition;
    const item_def* weapon;
    bool operator < (const mon_attack_info &other) const
    {
        return std::tie(definition.type, definition.flavour,
                        definition.damage, weapon)
             < std::tie(other.definition.type, other.definition.flavour,
                        other.definition.damage, other.weapon);
    }
};

/**
 * What weapon is the given monster using for the given attack, if any?
 *
 * @param mi        The monster in question.
 * @param atk       The attack number. (E.g. 0, 1, 2...)
 * @return          The melee weapon being used by the monster for the given
 *                  attack, if any.
 */
static const item_def* _weapon_for_attack(const monster_info& mi, int atk)
{
    const item_def* weapon
       = atk == 0 ? mi.inv[MSLOT_WEAPON].get() :
         atk == 1 && mi.wields_two_weapons() ? mi.inv[MSLOT_ALT_WEAPON].get() :
         nullptr;

    if (weapon && is_weapon(*weapon))
        return weapon;
    return nullptr;
}

static string _monster_attacks_description(const monster_info& mi)
{
    ostringstream result;
    map<mon_attack_info, int> attack_counts;

    for (int i = 0; i < MAX_NUM_ATTACKS; ++i)
    {
        const mon_attack_def &attack = mi.attack[i];
        if (attack.type == AT_NONE)
            break; // assumes there are no gaps in attack arrays

        const item_def* weapon = _weapon_for_attack(mi, i);
        mon_attack_info attack_info = { attack, weapon };

        ++attack_counts[attack_info];
    }

    // Hydrae have only one explicit attack, which is repeated for each head.
    if (mons_genus(mi.base_type) == MONS_HYDRA)
        for (auto &attack_count : attack_counts)
            attack_count.second = mi.num_heads;

    vector<string> attack_descs;
    for (const auto &attack_count : attack_counts)
    {
        const mon_attack_info &info = attack_count.first;
        const mon_attack_def &attack = info.definition;
        const string weapon_note
            = info.weapon ? make_stringf(" plus %s %s",
                                         mi.pronoun(PRONOUN_POSSESSIVE),
                                         info.weapon->name(DESC_PLAIN).c_str())
                          : "";
        const string count_desc =
              attack_count.second == 1 ? "" :
              attack_count.second == 2 ? " twice" :
              " " + number_in_words(attack_count.second) + " times";

        // XXX: hack alert
        if (attack.flavour == AF_PURE_FIRE)
        {
            attack_descs.push_back(
                make_stringf(TR7("%s for up to %d fire damage",""),
                             mon_attack_name(attack.type, false).c_str(),
                             flavour_damage(attack.flavour, mi.hd, false)));
            continue;
        }

        // Damage is listed in parentheses for attacks with a flavour
        // description, but not for plain attacks.
        bool has_flavour = !_flavour_base_desc(attack.flavour).empty();
        const string damage_desc =
            make_stringf("%sfor up to %d damage%s%s%s",
                         has_flavour ? "(" : "",
                         attack.damage,
                         attack_count.second > 1 ? " each" : "",
                         weapon_note.c_str(),
                         has_flavour ? ")" : "");

        attack_descs.push_back(
            make_stringf("%s%s%s%s %s%s",
                         _special_flavour_prefix(attack.flavour),
                         mon_attack_name(attack.type, false).c_str(),
                         _flavour_range_desc(attack.flavour),
                         count_desc.c_str(),
                         damage_desc.c_str(),
                         _flavour_effect(attack.flavour, mi.hd).c_str()));
    }


    if (!attack_descs.empty())
    {
        result << uppercase_first(mi.pronoun(PRONOUN_SUBJECTIVE));
        result << " can " << comma_separated_line(attack_descs.begin(),
                                                  attack_descs.end(),
                                                  "; and ", "; ");
        result << ".\n";
    }

    return result.str();
}

static string _monster_spells_description(const monster_info& mi)
{
    static const string panlord_desc =
        TR7("It may possess any of a vast number of diabolical powers.\n","그것은 엄청난 수의 악마적인 힘을 소유하고있을 수 있습니다.\n");

    // Show a generic message for pan lords, since they're secret.
    if (mi.type == MONS_PANDEMONIUM_LORD && !mi.props.exists(SEEN_SPELLS_KEY))
        return panlord_desc;

    // Show monster spells and spell-like abilities.
    if (!mi.has_spells())
        return "";

    formatted_string description;
    if (mi.type == MONS_PANDEMONIUM_LORD)
        description.cprintf("%s", panlord_desc.c_str());
    describe_spellset(monster_spellset(mi), nullptr, description, &mi);
    description.cprintf(TR7("To read a description, press the key listed above.\n","설명을 읽으려면 위에 나열된 키를 누릅니다.\n"));
    return description.tostring();
}

static const char *_speed_description(int speed)
{
    // These thresholds correspond to the player mutations for fast and slow.
    ASSERT(speed != 10);
    if (speed < 7)
        return TR7("extremely slowly","달팽이처럼 느리게");
    else if (speed < 8)
        return TR7("very slowly","매우 느리게");
    else if (speed < 10)
        return TR7("slowly","느리게");
    else if (speed > 15)
        return TR7("extremely quickly", "극히 빠르게");
    else if (speed > 13)
        return TR7("very quickly", "매우 빠르게");
    else if (speed > 10)
        return TR7("quickly", "빠르게");

    return "buggily";
}

static void _add_energy_to_string(int speed, int energy, string what,
                                  vector<string> &fast, vector<string> &slow)
{
    if (energy == 10)
        return;

    const int act_speed = (speed * 10) / energy;
    if (act_speed > 10)
        fast.push_back(what + " " + _speed_description(act_speed));
    if (act_speed < 10)
        slow.push_back(what + " " + _speed_description(act_speed));
}


/**
 * Print a bar of +s and .s representing a given stat to a provided stream.
 *
 * @param value[in]         The current value represented by the bar.
 * @param scale[in]         The value that each + and . represents.
 * @param name              The name of the bar.
 * @param result[in,out]    The stringstream to append to.
 * @param base_value[in]    The 'base' value represented by the bar. If
 *                          INT_MAX, is ignored.
 */
static void _print_bar(int value, int scale, string name,
                       ostringstream &result, int base_value = INT_MAX)
{
    if (base_value == INT_MAX)
        base_value = value;

    result << name << " ";

    const int display_max = value ? value : base_value;
    const bool currently_disabled = !value && base_value;

    if (currently_disabled)
      result << "(";

    for (int i = 0; i * scale < display_max; i++)
    {
        result << "+";
        if (i % 5 == 4)
            result << " ";
    }

    if (currently_disabled)
      result << ")";

#ifdef DEBUG_DIAGNOSTICS
    if (!you.suppress_wizard)
        result << " (" << value << ")";
#endif

    if (currently_disabled)
    {
        result << " (Normal " << name << ")";

#ifdef DEBUG_DIAGNOSTICS
        if (!you.suppress_wizard)
            result << " (" << base_value << ")";
#endif
    }
}

/**
 * Append information about a given monster's HP to the provided stream.
 *
 * @param mi[in]            Player-visible info about the monster in question.
 * @param result[in,out]    The stringstream to append to.
 */
static void _describe_monster_hp(const monster_info& mi, ostringstream &result)
{
    result << "Max HP: " << mi.get_max_hp_desc() << "\n";
}

/**
 * Append information about a given monster's AC to the provided stream.
 *
 * @param mi[in]            Player-visible info about the monster in question.
 * @param result[in,out]    The stringstream to append to.
 */
static void _describe_monster_ac(const monster_info& mi, ostringstream &result)
{
    // MAX_GHOST_EVASION + two pips (so with EV in parens it's the same)
    _print_bar(mi.ac, 5, "AC", result);
    result << "\n";
}

/**
 * Append information about a given monster's EV to the provided stream.
 *
 * @param mi[in]            Player-visible info about the monster in question.
 * @param result[in,out]    The stringstream to append to.
 */
static void _describe_monster_ev(const monster_info& mi, ostringstream &result)
{
    _print_bar(mi.ev, 5, "EV", result, mi.base_ev);
    result << "\n";
}

/**
 * Append information about a given monster's MR to the provided stream.
 *
 * @param mi[in]            Player-visible info about the monster in question.
 * @param result[in,out]    The stringstream to append to.
 */
static void _describe_monster_mr(const monster_info& mi, ostringstream &result)
{
    if (mi.res_magic() == MAG_IMMUNE)
    {
        result << "MR ∞\n";
        return;
    }

    const int bar_scale = MR_PIP;
    _print_bar(mi.res_magic(), bar_scale, "MR", result);
    result << "\n";
}

// Size adjectives
const char* const size_adj[] =
{
    TR7("tiny","조그마한"),
    TR7("very small","매우 작은"),
    TR7("small","작은"),
    TR7("medium","중간 크기의"),
    TR7("large","큰"),
    TR7("very large","매우 큰"),
    TR7("giant","거대한"),
};
COMPILE_CHECK(ARRAYSZ(size_adj) == NUM_SIZE_LEVELS);

// This is used in monster description and on '%' screen for player size
const char* get_size_adj(const size_type size, bool ignore_medium)
{
    if (ignore_medium && size == SIZE_MEDIUM)
        return nullptr; // don't mention medium size
    return size_adj[size];
}

// Describe a monster's (intrinsic) resistances, speed and a few other
// attributes.
static string _monster_stat_description(const monster_info& mi)
{
    if (mons_is_sensed(mi.type) || mons_is_projectile(mi.type))
        return "";

    ostringstream result;

    _describe_monster_hp(mi, result);
    _describe_monster_ac(mi, result);
    _describe_monster_ev(mi, result);
    _describe_monster_mr(mi, result);

    result << "\n";

    resists_t resist = mi.resists();

    const mon_resist_flags resists[] =
    {
        MR_RES_ELEC,    MR_RES_POISON, MR_RES_FIRE,
        MR_RES_STEAM,   MR_RES_COLD,   MR_RES_ACID,
        MR_RES_ROTTING, MR_RES_NEG,    MR_RES_DAMNATION,
        MR_RES_TORNADO,
    };

    vector<string> extreme_resists;
    vector<string> high_resists;
    vector<string> base_resists;
    vector<string> suscept;

    for (mon_resist_flags rflags : resists)
    {
        int level = get_resist(resist, rflags);

        if (level != 0)
        {
            const char* attackname = _get_resist_name(rflags);
            if (rflags == MR_RES_DAMNATION || rflags == MR_RES_TORNADO)
                level = 3; // one level is immunity
            level = max(level, -1);
            level = min(level,  3);
            switch (level)
            {
                case -1:
                    suscept.emplace_back(attackname);
                    break;
                case 1:
                    base_resists.emplace_back(attackname);
                    break;
                case 2:
                    high_resists.emplace_back(attackname);
                    break;
                case 3:
                    extreme_resists.emplace_back(attackname);
                    break;
            }
        }
    }

    if (mi.props.exists(CLOUD_IMMUNE_MB_KEY) && mi.props[CLOUD_IMMUNE_MB_KEY])
        extreme_resists.emplace_back("clouds of all kinds");

    vector<string> resist_descriptions;
    if (!extreme_resists.empty())
    {
        const string tmp = TR7("immune to ","면역이다")
            + comma_separated_line(extreme_resists.begin(),
                                   extreme_resists.end());
        resist_descriptions.push_back(tmp);
    }
    if (!high_resists.empty())
    {
        const string tmp = TR7("very resistant to ","매우 저항이 있다")
            + comma_separated_line(high_resists.begin(), high_resists.end());
        resist_descriptions.push_back(tmp);
    }
    if (!base_resists.empty())
    {
        const string tmp = TR7("resistant to ","저항이 있다")
            + comma_separated_line(base_resists.begin(), base_resists.end());
        resist_descriptions.push_back(tmp);
    }

    const char* pronoun = mi.pronoun(PRONOUN_SUBJECTIVE);

    if (mi.threat != MTHRT_UNDEF)
    {
        result << uppercase_first(pronoun) << " looks "
               << _get_threat_desc(mi.threat) << ".\n";
    }

    if (!resist_descriptions.empty())
    {
        result << uppercase_first(pronoun) << TR7(" is ","은(는) ")
               << comma_separated_line(resist_descriptions.begin(),
                                       resist_descriptions.end(),
                                       "; and ", "; ")
               << ".\n";
    }

    // Is monster susceptible to anything? (On a new line.)
    if (!suscept.empty())
    {
        result << uppercase_first(pronoun) << TR7(" is susceptible to "," 취약하다 ")
               << comma_separated_line(suscept.begin(), suscept.end())
               << ".\n";
    }

    if (mi.is(MB_CHAOTIC))
    {
        result << uppercase_first(pronoun) << TR7(" is vulnerable to silver and","은 은제 무기에 약하며, 특히 진이 싫어한다.\n")
                                              TR7(" hated by Zin.\n","");
    }

    if (mons_class_flag(mi.type, M_STATIONARY)
        && !mons_is_tentacle_or_tentacle_segment(mi.type))
    {
        result << uppercase_first(pronoun) << TR7(" cannot move.\n","은(는) 이 자리에 고정되어 있다.");
    }

    if (mons_class_flag(mi.type, M_COLD_BLOOD)
        && get_resist(resist, MR_RES_COLD) <= 0)
    {
        result << uppercase_first(pronoun) << TR7(" is cold-blooded and may be ","은(는) 냉혈동물이다. 그리고 냉기공격으로 느려질 수 있습니다.\n")
                                              TR7("slowed by cold attacks.\n","");
    }

    // Seeing invisible.
    if (mi.can_see_invisible())
        result << uppercase_first(pronoun) << TR7(" can see invisible.\n","은(는) 보이지 않는 물체를 감지할 수 있다.");

    // Echolocation, wolf noses, jellies, etc
    if (!mons_can_be_blinded(mi.type))
        result << uppercase_first(pronoun) << TR7(" is immune to blinding.\n","은(는) 눈멀기에 면역이다.");
    // XXX: could mention "immune to dazzling" here, but that's spammy, since
    // it's true of such a huge number of monsters. (undead, statues, plants).
    // Might be better to have some place where players can see holiness &
    // information about holiness.......?

    if (mi.intel() <= I_BRAINLESS)
    {
        // Matters for Ely.
        result << uppercase_first(pronoun) << TR7(" is mindless.\n","은(는) 감정이 없다.");
    }
    else if (mi.intel() >= I_HUMAN)
    {
        // Matters for Yred, Gozag, Zin, TSO, Alistair....
        result << uppercase_first(pronoun) << TR7(" is intelligent.\n","은(는) 지성을 가졌다.");
    }

    // Unusual monster speed.
    const int speed = mi.base_speed();
    bool did_speed = false;
    if (speed != 10 && speed != 0)
    {
        did_speed = true;
        result << uppercase_first(pronoun) << TR7(" is ","은(는) ") << mi.speed_description();
    }
    const mon_energy_usage def = DEFAULT_ENERGY;
    if (!(mi.menergy == def))
    {
        const mon_energy_usage me = mi.menergy;
        vector<string> fast, slow;
        if (!did_speed)
            result << uppercase_first(pronoun) << " ";
        _add_energy_to_string(speed, me.move, "covers ground", fast, slow);
        // since MOVE_ENERGY also sets me.swim
        if (me.swim != me.move)
            _add_energy_to_string(speed, me.swim, "swims", fast, slow);
        _add_energy_to_string(speed, me.attack, "attacks", fast, slow);
        if (mons_class_itemuse(mi.type) >= MONUSE_STARTING_EQUIPMENT)
            _add_energy_to_string(speed, me.missile, "shoots", fast, slow);
        _add_energy_to_string(
            speed, me.spell,
            mi.is_actual_spellcaster() ? TR7("casts spells","은(는) 주문을 외웠다.") :
            mi.is_priest()             ? TR7("uses invocations","은(는) 기도했다.")
                                       : TR7("uses natural abilities","은(는) 타고난 특수능력을 사용했다."), fast, slow);
        _add_energy_to_string(speed, me.special, "uses special abilities",
                              fast, slow);
        if (mons_class_itemuse(mi.type) >= MONUSE_STARTING_EQUIPMENT)
            _add_energy_to_string(speed, me.item, "uses items", fast, slow);

        if (speed >= 10)
        {
            if (did_speed && fast.size() == 1)
                result << TR7(" and "," 그리고 ") << fast[0];
            else if (!fast.empty())
            {
                if (did_speed)
                    result << ", ";
                result << comma_separated_line(fast.begin(), fast.end());
            }
            if (!slow.empty())
            {
                if (did_speed || !fast.empty())
                    result << ", but ";
                result << comma_separated_line(slow.begin(), slow.end());
            }
        }
        else if (speed < 10)
        {
            if (did_speed && slow.size() == 1)
                result << TR7(" and "," 그리고 ") << slow[0];
            else if (!slow.empty())
            {
                if (did_speed)
                    result << ", ";
                result << comma_separated_line(slow.begin(), slow.end());
            }
            if (!fast.empty())
            {
                if (did_speed || !slow.empty())
                    result << ", but ";
                result << comma_separated_line(fast.begin(), fast.end());
            }
        }
        result << ".\n";
    }
    else if (did_speed)
        result << ".\n";

    if (mi.type == MONS_SHADOW)
    {
        // Cf. monster::action_energy() in monster.cc.
        result << uppercase_first(pronoun) << " covers ground more"
               << " quickly when invisible.\n";
    }

    if (mi.airborne())
        result << uppercase_first(pronoun) << " can fly.\n";

    // Unusual regeneration rates.
    if (!mi.can_regenerate())
        result << uppercase_first(pronoun) << " cannot regenerate.\n";
    else if (mons_class_fast_regen(mi.type))
        result << uppercase_first(pronoun) << " regenerates quickly.\n";

    const char* mon_size = get_size_adj(mi.body_size(), true);
    if (mon_size)
        result << uppercase_first(pronoun) << " is " << mon_size << ".\n";

    if (in_good_standing(GOD_ZIN, 0) && !mi.pos.origin() && monster_at(mi.pos))
    {
        recite_counts retval;
        monster *m = monster_at(mi.pos);
        auto eligibility = zin_check_recite_to_single_monster(m, retval);
        if (eligibility == RE_INELIGIBLE)
        {
            result << uppercase_first(pronoun) <<
                    " cannot be affected by reciting Zin's laws.";
        }
        else if (eligibility == RE_TOO_STRONG)
        {
            result << uppercase_first(pronoun) <<
                    " is too strong to be affected by reciting Zin's laws.";
        }
        else // RE_ELIGIBLE || RE_RECITE_TIMER
        {
            result << uppercase_first(pronoun) <<
                            " can be affected by reciting Zin's laws.";
        }

        if (you.wizard)
        {
            result << " (Recite power:" << zin_recite_power()
                   << ", Hit dice:" << mi.hd << ")";
        }
        result << "\n";
    }

    result << _monster_attacks_description(mi);
    result << _monster_spells_description(mi);

    return result.str();
}

branch_type serpent_of_hell_branch(monster_type m)
{
    switch (m)
    {
    case MONS_SERPENT_OF_HELL_COCYTUS:
        return BRANCH_COCYTUS;
    case MONS_SERPENT_OF_HELL_DIS:
        return BRANCH_DIS;
    case MONS_SERPENT_OF_HELL_TARTARUS:
        return BRANCH_TARTARUS;
    case MONS_SERPENT_OF_HELL:
        return BRANCH_GEHENNA;
    default:
        die("bad serpent of hell monster_type");
    }
}

string serpent_of_hell_flavour(monster_type m)
{
    return lowercase_string(branches[serpent_of_hell_branch(m)].shortname);
}

// Fetches the monster's database description and reads it into inf.
void get_monster_db_desc(const monster_info& mi, describe_info &inf,
                         bool &has_stat_desc, bool force_seen)
{
    if (inf.title.empty())
        inf.title = getMiscString(mi.common_name(DESC_DBNAME) + " title");
    if (inf.title.empty())
        inf.title = uppercase_first(mi.full_name(DESC_A)) + ".";

    string db_name;

    if (mi.props.exists("dbname"))
        db_name = mi.props["dbname"].get_string();
    else if (mi.mname.empty())
        db_name = mi.db_name();
    else
        db_name = mi.full_name(DESC_PLAIN);

    if (mons_species(mi.type) == MONS_SERPENT_OF_HELL)
        db_name += " " + serpent_of_hell_flavour(mi.type);

    // This is somewhat hackish, but it's a good way of over-riding monsters'
    // descriptions in Lua vaults by using MonPropsMarker. This is also the
    // method used by set_feature_desc_long, etc. {due}
    if (!mi.description.empty())
        inf.body << mi.description;
    // Don't get description for player ghosts.
    else if (mi.type != MONS_PLAYER_GHOST
             && mi.type != MONS_PLAYER_ILLUSION)
    {
        inf.body << getLongDescription(db_name);
    }

    // And quotes {due}
    if (!mi.quote.empty())
        inf.quote = mi.quote;
    else
        inf.quote = getQuoteString(db_name);

    string symbol;
    symbol += get_monster_data(mi.type)->basechar;
    if (isaupper(symbol[0]))
        symbol = "cap-" + symbol;

    string quote2;
    if (!mons_is_unique(mi.type))
    {
        string symbol_prefix = "__" + symbol + "_prefix";
        inf.prefix = getLongDescription(symbol_prefix);

        string symbol_suffix = "__" + symbol + "_suffix";
        quote2 = getQuoteString(symbol_suffix);
    }

    if (!inf.quote.empty() && !quote2.empty())
        inf.quote += "\n";
    inf.quote += quote2;

    const string it = mi.pronoun(PRONOUN_SUBJECTIVE);
    const string it_o = mi.pronoun(PRONOUN_OBJECTIVE);
    const string It = uppercase_first(it);

    switch (mi.type)
    {
    case MONS_RED_DRACONIAN:
    case MONS_WHITE_DRACONIAN:
    case MONS_GREEN_DRACONIAN:
    case MONS_PALE_DRACONIAN:
    case MONS_BLACK_DRACONIAN:
    case MONS_YELLOW_DRACONIAN:
    case MONS_PURPLE_DRACONIAN:
    case MONS_GREY_DRACONIAN:
    case MONS_DRACONIAN_SHIFTER:
    case MONS_DRACONIAN_SCORCHER:
    case MONS_DRACONIAN_ANNIHILATOR:
    case MONS_DRACONIAN_STORMCALLER:
    case MONS_DRACONIAN_MONK:
    case MONS_DRACONIAN_KNIGHT:
    {
        inf.body << "\n" << _describe_draconian(mi) << "\n";
        break;
    }

    case MONS_MONSTROUS_DEMONSPAWN:
    case MONS_GELID_DEMONSPAWN:
    case MONS_INFERNAL_DEMONSPAWN:
    case MONS_TORTUROUS_DEMONSPAWN:
    case MONS_BLOOD_SAINT:
    case MONS_WARMONGER:
    case MONS_CORRUPTER:
    case MONS_BLACK_SUN:
    {
        inf.body << "\n" << _describe_demonspawn(mi) << "\n";
        break;
    }

    case MONS_PLAYER_GHOST:
        inf.body << "The apparition of " << get_ghost_description(mi) << ".\n";
        if (mi.props.exists(MIRRORED_GHOST_KEY))
            inf.body << "It looks just like you...spooky!\n";
        break;

    case MONS_PLAYER_ILLUSION:
        inf.body << "An illusion of " << get_ghost_description(mi) << ".\n";
        break;

    case MONS_PANDEMONIUM_LORD:
        inf.body << _describe_demon(mi.mname, mi.airborne()) << "\n";
        break;

    case MONS_MUTANT_BEAST:
        // vault renames get their own descriptions
        if (mi.mname.empty() || !mi.is(MB_NAME_REPLACE))
            inf.body << _describe_mutant_beast(mi) << "\n";
        break;

    case MONS_BLOCK_OF_ICE:
        if (mi.is(MB_SLOWLY_DYING))
            inf.body << "\nIt is quickly melting away.\n";
        break;

    case MONS_PILLAR_OF_SALT:
        if (mi.is(MB_SLOWLY_DYING))
            inf.body << "\nIt is quickly crumbling away.\n";
        break;

    case MONS_PROGRAM_BUG:
        inf.body << TR7("If this monster is a \"program bug\", then it's ","만일 이 몬스터가 \"프로그램 버그\" 라면, 일단 게임을 저장하고 다시 불러오는 것을 추천한다. 프로그램 버그 몬스터를 보고하든지 아니면 그냥 놔두고 던젼을 돌아다니게 하든지, 좋을대로 하라.\n")
                TR7("recommended that you save your game and reload. Please report ","")
                TR7("monsters who masquerade as program bugs or run around the ","")
                TR7("dungeon without a proper description to the authorities.\n","");
        break;

    default:
        break;
    }

    if (!mons_is_unique(mi.type))
    {
        string symbol_suffix = "__";
        symbol_suffix += symbol;
        symbol_suffix += "_suffix";

        string suffix = getLongDescription(symbol_suffix)
                      + getLongDescription(symbol_suffix + "_examine");

        if (!suffix.empty())
            inf.body << "\n" << suffix;
    }

    const int curse_power = mummy_curse_power(mi.type);
    if (curse_power && !mi.is(MB_SUMMONED))
    {
        inf.body << "\n" << It << " will inflict a ";
        if (curse_power > 10)
            inf.body << "powerful ";
        inf.body << "necromantic curse on "
                 << mi.pronoun(PRONOUN_POSSESSIVE) << " foe when destroyed.\n";
    }

    // Get information on resistances, speed, etc.
    string result = _monster_stat_description(mi);
    if (!result.empty())
    {
        inf.body << "\n" << result;
        has_stat_desc = true;
    }

    bool stair_use = false;
    if (!mons_class_can_use_stairs(mi.type))
    {
        inf.body << It << " is incapable of using stairs.\n";
        stair_use = true;
    }

    if (mi.is(MB_SUMMONED))
    {
        inf.body << "\nThis monster has been summoned, and is thus only "
                    "temporary. Killing " << it_o << " yields no experience, "
                    "nutrition or items";
        if (!stair_use)
            inf.body << ", and " << it << " is incapable of using stairs";
        inf.body << ".\n";
    }
    else if (mi.is(MB_PERM_SUMMON))
    {
        inf.body << "\nThis monster has been summoned in a durable way. "
                    "Killing " << it_o << " yields no experience, nutrition "
                    "or items, but " << it_o << " cannot be abjured.\n";
    }
    else if (mi.is(MB_NO_REWARD))
    {
        inf.body << "\nKilling this monster yields no experience, nutrition or"
                    " items.";
    }
    else if (mons_class_leaves_hide(mi.type))
    {
        inf.body << "\nIf " << it << " is slain, it may be possible to "
                    "recover " << mi.pronoun(PRONOUN_POSSESSIVE)
                 << " hide, which can be used as armour.\n";
    }

    if (mi.is(MB_SUMMONED_CAPPED))
    {
        inf.body << "\nYou have summoned too many monsters of this kind to "
                    "sustain them all, and thus this one will shortly "
                    "expire.\n";
    }

    if (!inf.quote.empty())
        inf.quote += "\n";

#ifdef DEBUG_DIAGNOSTICS
    if (you.suppress_wizard)
        return;
    if (mi.pos.origin() || !monster_at(mi.pos))
        return; // not a real monster
    monster& mons = *monster_at(mi.pos);

    if (mons.has_originating_map())
    {
        inf.body << make_stringf("\nPlaced by map: %s",
                                 mons.originating_map().c_str());
    }

    inf.body << "\nMonster health: "
             << mons.hit_points << "/" << mons.max_hit_points << "\n";

    const actor *mfoe = mons.get_foe();
    inf.body << "Monster foe: "
             << (mfoe? mfoe->name(DESC_PLAIN, true)
                 : "(none)");

    vector<string> attitude;
    if (mons.friendly())
        attitude.emplace_back("friendly");
    if (mons.neutral())
        attitude.emplace_back("neutral");
    if (mons.good_neutral())
        attitude.emplace_back("good_neutral");
    if (mons.strict_neutral())
        attitude.emplace_back("strict_neutral");
    if (mons.pacified())
        attitude.emplace_back("pacified");
    if (mons.wont_attack())
        attitude.emplace_back("wont_attack");
    if (!attitude.empty())
    {
        string att = comma_separated_line(attitude.begin(), attitude.end(),
                                          "; ", "; ");
        if (mons.has_ench(ENCH_INSANE))
            inf.body << "; frenzied and insane (otherwise " << att << ")";
        else
            inf.body << "; " << att;
    }
    else if (mons.has_ench(ENCH_INSANE))
        inf.body << "; frenzied and insane";

    inf.body << "\n\nHas holiness: ";
    inf.body << holiness_description(mi.holi);
    inf.body << ".";

    const monster_spells &hspell_pass = mons.spells;
    bool found_spell = false;

    for (unsigned int i = 0; i < hspell_pass.size(); ++i)
    {
        if (!found_spell)
        {
            inf.body << "\n\nMonster Spells:\n";
            found_spell = true;
        }

        inf.body << "    " << i << ": "
                 << spell_title(hspell_pass[i].spell)
                 << " (";
        if (hspell_pass[i].flags & MON_SPELL_EMERGENCY)
            inf.body << "emergency, ";
        if (hspell_pass[i].flags & MON_SPELL_NATURAL)
            inf.body << "natural, ";
        if (hspell_pass[i].flags & MON_SPELL_MAGICAL)
            inf.body << "magical, ";
        if (hspell_pass[i].flags & MON_SPELL_WIZARD)
            inf.body << "wizard, ";
        if (hspell_pass[i].flags & MON_SPELL_PRIEST)
            inf.body << "priest, ";
        if (hspell_pass[i].flags & MON_SPELL_BREATH)
            inf.body << "breath, ";
        inf.body << (int) hspell_pass[i].freq << ")";
    }

    bool has_item = false;
    for (mon_inv_iterator ii(mons); ii; ++ii)
    {
        if (!has_item)
        {
            inf.body << "\n\nMonster Inventory:\n";
            has_item = true;
        }
        inf.body << "    " << ii.slot() << ": "
                 << ii->name(DESC_A, false, true);
    }

    if (mons.props.exists("blame"))
    {
        inf.body << "\n\nMonster blame chain:\n";

        const CrawlVector& blame = mons.props["blame"].get_vector();

        for (const auto &entry : blame)
            inf.body << "    " << entry.get_string() << "\n";
    }
    inf.body << "\n\n" << debug_constriction_string(&mons);
#endif
}

int describe_monsters(const monster_info &mi, bool force_seen,
                      const string &footer)
{
    bool has_stat_desc = false;
    describe_info inf;
    formatted_string desc;

    get_monster_db_desc(mi, inf, has_stat_desc, force_seen);

    spellset spells = monster_spellset(mi);

    auto vbox = make_shared<Box>(Widget::VERT);
    auto title_hbox = make_shared<Box>(Widget::HORZ);

#ifdef USE_TILE_LOCAL
    auto dgn = make_shared<Dungeon>();
    dgn->width = dgn->height = 1;
    dgn->buf().add_monster(mi, 0, 0);
    title_hbox->add_child(move(dgn));
#endif

    auto title = make_shared<Text>();
    title->set_text(formatted_string(inf.title));
    title->set_margin_for_crt({0, 0, 0, 0});
    title->set_margin_for_sdl({0, 0, 0, 10});
    title_hbox->add_child(move(title));

    title_hbox->align_items = Widget::CENTER;
    title_hbox->set_margin_for_crt({0, 0, 1, 0});
    title_hbox->set_margin_for_sdl({0, 0, 20, 0});
    vbox->add_child(move(title_hbox));

    auto scroller = make_shared<Scroller>();
    auto text = make_shared<Text>();
    desc += formatted_string(inf.body.str());
    if (crawl_state.game_is_hints())
        desc += formatted_string(hints_describe_monster(mi, has_stat_desc));
    desc += formatted_string(inf.footer);
    desc = formatted_string::parse_string(trimmed_string(desc));

    text->set_text(desc);
    text->wrap_text = true;
    scroller->set_child(text);
    vbox->add_child(scroller);

    if (!inf.quote.empty())
    {
        auto more = make_shared<Text>(_toggle_message);
        more->set_margin_for_crt({1, 0, 0, 0});
        more->set_margin_for_sdl({20, 0, 0, 0});
        vbox->add_child(move(more));
    }

#ifdef USE_TILE_LOCAL
    vbox->max_size()[0] = tiles.get_crt_font()->char_width()*80;
#endif

    auto popup = make_shared<ui::Popup>(move(vbox));

    bool done = false;
    bool show_quote = false;
    int lastch;
    popup->on(Widget::slots.event, [&](wm_event ev) {
        if (ev.type != WME_KEYDOWN)
            return false;
        int key = ev.key.keysym.sym;
        lastch = key;
        done = key == CK_ESCAPE;
        if (!inf.quote.empty() && (key == '!' || key == CK_MOUSE_CMD))
        {
            show_quote = !show_quote;
            text->set_text(show_quote ? formatted_string(trimmed_string(inf.quote)) : desc);
        }
        const vector<pair<spell_type,char>> spell_map = map_chars_to_spells(spells, nullptr);
        auto entry = find_if(spell_map.begin(), spell_map.end(),
                [key](const pair<spell_type,char>& e) { return e.second == key; });
        if (entry == spell_map.end())
            return false;
        describe_spell(entry->first, &mi, nullptr);
        return true;
    });

#ifdef USE_TILE_WEB
    tiles_crt_control disable_crt(false);
    tiles.json_open_object();
    tiles.json_write_string("title", inf.title);
    formatted_string needle;
    describe_spellset(spells, nullptr, needle, &mi);
    string desc_without_spells = desc.to_colour_string();
    if (!needle.empty())
    {
        desc_without_spells = replace_all(desc_without_spells,
                needle, "SPELLSET_PLACEHOLDER");
    }
    tiles.json_write_string("body", desc_without_spells);
    write_spellset(spells, nullptr, &mi);

    {
        tileidx_t t    = tileidx_monster(mi);
        tileidx_t t0   = t & TILE_FLAG_MASK;
        tileidx_t flag = t & (~TILE_FLAG_MASK);

        if (!mons_class_is_stationary(mi.type) || mi.type == MONS_TRAINING_DUMMY)
        {
            tileidx_t mcache_idx = mcache.register_monster(mi);
            t = flag | (mcache_idx ? mcache_idx : t0);
            t0 = t & TILE_FLAG_MASK;
        }

        tiles.json_write_int("fg_idx", t0);
        tiles.json_write_name("flag");
        tiles.write_tileidx(flag);

        if (t0 >= TILEP_MCACHE_START)
        {
            mcache_entry *entry = mcache.get(t0);
            if (entry)
                tiles.send_mcache(entry, false);
            else
            {
                tiles.json_write_comma();
                tiles.write_message("\"doll\":[[%d,%d]]", TILEP_MONS_UNKNOWN, TILE_Y);
                tiles.json_write_null("mcache");
            }
        }
        else if (t0 >= TILE_MAIN_MAX)
        {
            tiles.json_write_comma();
            tiles.write_message("\"doll\":[[%u,%d]]", (unsigned int) t0, TILE_Y);
            tiles.json_write_null("mcache");
        }
    }
    tiles.push_ui_layout("describe-monster", 0);
#endif

    ui::run_layout(move(popup), done);

#ifdef USE_TILE_WEB
    tiles.pop_ui_layout();
#endif

    return lastch;
}

static const char* xl_rank_names[] =
{
    "weakling",
    "amateur",
    "novice",
    "journeyman",
    "adept",
    "veteran",
    "master",
    "legendary"
};

static string _xl_rank_name(const int xl_rank)
{
    const string rank = xl_rank_names[xl_rank];

    return article_a(rank);
}

string short_ghost_description(const monster *mon, bool abbrev)
{
    ASSERT(mons_is_pghost(mon->type));

    const ghost_demon &ghost = *(mon->ghost);
    const char* rank = xl_rank_names[ghost_level_to_rank(ghost.xl)];

    string desc = make_stringf("%s %s %s", rank,
                               species_name(ghost.species).c_str(),
                               get_job_name(ghost.job));

    if (abbrev || strwidth(desc) > 40)
    {
        desc = make_stringf("%s %s%s",
                            rank,
                            get_species_abbrev(ghost.species),
                            get_job_abbrev(ghost.job));
    }

    return desc;
}

// Describes the current ghost's previous owner. The caller must
// prepend "The apparition of" or whatever and append any trailing
// punctuation that's wanted.
string get_ghost_description(const monster_info &mi, bool concise)
{
    ostringstream gstr;

    const species_type gspecies = mi.i_ghost.species;

    gstr << mi.mname << " the "
         << skill_title_by_rank(mi.i_ghost.best_skill,
                        mi.i_ghost.best_skill_rank,
                        gspecies,
                        species_has_low_str(gspecies), mi.i_ghost.religion)
         << ", " << _xl_rank_name(mi.i_ghost.xl_rank) << " ";

    if (concise)
    {
        gstr << get_species_abbrev(gspecies)
             << get_job_abbrev(mi.i_ghost.job);
    }
    else
    {
        gstr << species_name(gspecies)
             << " "
             << get_job_name(mi.i_ghost.job);
    }

    if (mi.i_ghost.religion != GOD_NO_GOD)
    {
        gstr << " of "
             << god_name(mi.i_ghost.religion);
    }

    return gstr.str();
}

void describe_skill(skill_type skill)
{
    describe_info inf;
    inf.title = skill_name(skill);
    inf.body << get_skill_description(skill, false);
#ifdef USE_TILE
    tile_def tile = tile_def(tileidx_skill(skill, TRAINING_ENABLED), TEX_GUI);
    show_description(inf, &tile);
#else
    show_description(inf);
#endif
}

// only used in tiles
string get_command_description(const command_type cmd, bool terse)
{
    string lookup = command_to_name(cmd);

    if (!terse)
        lookup += " verbose";

    string result = getLongDescription(lookup);
    if (result.empty())
    {
        if (!terse)
        {
            // Try for the terse description.
            result = get_command_description(cmd, true);
            if (!result.empty())
                return result + ".";
        }
        return command_to_name(cmd);
    }

    return result.substr(0, result.length() - 1);
}

/**
 * Provide auto-generated information about the given cloud type. Describe
 * opacity & related factors.
 *
 * @param cloud_type        The cloud_type in question.
 * @return e.g. "\nThis cloud is opaque; one tile will not block vision, but
 *      multiple will. \nClouds of this kind the player makes will vanish very
 *      quickly once outside the player's sight."
 */
string extra_cloud_info(cloud_type cloud_type)
{
    const bool opaque = is_opaque_cloud(cloud_type);
    const string opacity_info = !opaque ? "" :
        "\nThis cloud is opaque; one tile will not block vision, but "
        "multiple will.";
    const string vanish_info
        = make_stringf("\nClouds of this kind an adventurer makes will vanish "
                       "%s once outside their sight.",
                       opaque ? "quickly" : "almost instantly");
    return opacity_info + vanish_info;
}