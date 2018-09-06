// Copyright (c) 1981-86 Robert A. Koeneke
// Copyright (c) 1987-94 James E. Wilson
//
// This work is free software released under the GNU General Public License
// version 2.0, and comes with ABSOLUTELY NO WARRANTY.
//
// See LICENSE and AUTHORS for more information.

// Player/creature spells, breaths, wands, scrolls, etc. code

#include "headers.h"

// Returns spell pointer -RAK-
static bool spellGetId(int *spell_ids, int number_of_choices, int &spell_id, int &spell_chance, const char *prompt, int first_spell) {
    spell_id = -1;

    vtype_t str = {'\0'};
    (void) sprintf(str, "(Spells %c-%c, *=List, <ESCAPE>=exit) %s", spell_ids[0] + 'a' - first_spell, spell_ids[number_of_choices - 1] + 'a' - first_spell, prompt);

    bool spell_found = false;
    bool redraw = false;

    int offset = (classes[py.misc.class_id].class_to_use_mage_spells == config::spells::SPELL_TYPE_MAGE ? config::spells::NAME_OFFSET_SPELLS : config::spells::NAME_OFFSET_PRAYERS);

    char choice;

    while (!spell_found && getCommand(str, choice)) {
        if (isupper((int) choice) != 0) {
            spell_id = choice - 'A' + first_spell;

            // verify that this is in spells[], at most 22 entries in class_to_use_mage_spells[]
            int test_spell_id;
            for (test_spell_id = 0; test_spell_id < number_of_choices; test_spell_id++) {
                if (spell_id == spell_ids[test_spell_id]) {
                    break;
                }
            }

            if (test_spell_id == number_of_choices) {
                spell_id = -2;
            } else {
                Spell_t const &spell = magic_spells[py.misc.class_id - 1][spell_id];

                vtype_t tmp_str = {'\0'};
                (void) sprintf(tmp_str, "Cast %s (%d mana, %d%% fail)?", spell_names[spell_id + offset], spell.mana_required, spellChanceOfSuccess(spell_id));
                if (getInputConfirmation(tmp_str)) {
                    spell_found = true;
                } else {
                    spell_id = -1;
                }
            }
        } else if (islower((int) choice) != 0) {
            spell_id = choice - 'a' + first_spell;

            // verify that this is in spells[], at most 22 entries in class_to_use_mage_spells[]
            int test_spell_id;
            for (test_spell_id = 0; test_spell_id < number_of_choices; test_spell_id++) {
                if (spell_id == spell_ids[test_spell_id]) {
                    break;
                }
            }

            if (test_spell_id == number_of_choices) {
                spell_id = -2;
            } else {
                spell_found = true;
            }
        } else if (choice == '*') {
            // only do this drawing once
            if (!redraw) {
                terminalSaveScreen();
                redraw = true;
                displaySpellsList(spell_ids, number_of_choices, false, first_spell);
            }
        } else if (isalpha((int) choice) != 0) {
            spell_id = -2;
        } else {
            spell_id = -1;
            terminalBellSound();
        }

        if (spell_id == -2) {
            vtype_t tmp_str = {'\0'};
            (void) sprintf(tmp_str, "You don't know that %s.", (offset == config::spells::NAME_OFFSET_SPELLS ? "spell" : "prayer"));
            printMessage(tmp_str);
        }
    }

    if (redraw) {
        terminalRestoreScreen();
    }

    messageLineClear();

    if (spell_found) {
        spell_chance = spellChanceOfSuccess(spell_id);
    }

    return spell_found;
}

// Return spell number and failure chance -RAK-
// returns -1 if no spells in book
// returns  1 if choose a spell in book to cast
// returns  0 if don't choose a spell, i.e. exit with an escape
// TODO: split into two functions; getting spell ID and casting an actual spell
int castSpellGetId(const char *prompt, int item_id, int &spell_id, int &spell_chance) {
    // NOTE: `flags` gets set again, since getAndClearFirstBit modified it
    uint32_t flags = inventory[item_id].flags;
    int first_spell = getAndClearFirstBit(flags);
    flags = inventory[item_id].flags & py.flags.spells_learnt;

    // TODO(cook) move access to `magic_spells[]` directly to the for loop it's used in, below?
    Spell_t *spells = magic_spells[py.misc.class_id - 1];

    int spell_count = 0;
    int spell_list[31];

    while (flags != 0u) {
        int pos = getAndClearFirstBit(flags);

        if (spells[pos].level_required <= py.misc.level) {
            spell_list[spell_count] = pos;
            spell_count++;
        }
    }

    if (spell_count == 0) {
        return -1;
    }

    int result = 0;
    if (spellGetId(spell_list, spell_count, spell_id, spell_chance, prompt, first_spell)) {
        result = 1;
    }

    if ((result != 0) && magic_spells[py.misc.class_id - 1][spell_id].mana_required > py.misc.current_mana) {
        if (classes[py.misc.class_id].class_to_use_mage_spells == config::spells::SPELL_TYPE_MAGE) {
            result = (int) getInputConfirmation("You summon your limited strength to cast this one! Confirm?");
        } else {
            result = (int) getInputConfirmation("The gods may think you presumptuous for this! Confirm?");
        }
    }

    return result;
}

// Following are spell procedure/functions -RAK-
// These routines are commonly used in the scroll, potion, wands, and
// staves routines, and are occasionally called from other areas.
// Now included are creature spells also.           -RAK

// Detect any treasure on the current panel -RAK-
bool spellDetectTreasureWithinVicinity() {
    bool detected = false;

    for (int y = dg.panel.top; y <= dg.panel.bottom; y++) {
        for (int x = dg.panel.left; x <= dg.panel.right; x++) {
            Tile_t &tile = dg.floor[y][x];

            if (tile.treasure_id != 0 && treasure_list[tile.treasure_id].category_id == TV_GOLD && !caveTileVisible(Coord_t{y, x})) {
                tile.field_mark = true;
                dungeonLiteSpot(Coord_t{y, x});
                detected = true;
            }
        }
    }

    return detected;
}

// Detect all objects on the current panel -RAK-
bool spellDetectObjectsWithinVicinity() {
    bool detected = false;

    for (int y = dg.panel.top; y <= dg.panel.bottom; y++) {
        for (int x = dg.panel.left; x <= dg.panel.right; x++) {
            Tile_t &tile = dg.floor[y][x];

            if (tile.treasure_id != 0 && treasure_list[tile.treasure_id].category_id < TV_MAX_OBJECT && !caveTileVisible(Coord_t{y, x})) {
                tile.field_mark = true;
                dungeonLiteSpot(Coord_t{y, x});
                detected = true;
            }
        }
    }

    return detected;
}

// Locates and displays traps on current panel -RAK-
bool spellDetectTrapsWithinVicinity() {
    bool detected = false;

    for (int y = dg.panel.top; y <= dg.panel.bottom; y++) {
        for (int x = dg.panel.left; x <= dg.panel.right; x++) {
            Tile_t &tile = dg.floor[y][x];

            if (tile.treasure_id == 0) {
                continue;
            }

            if (treasure_list[tile.treasure_id].category_id == TV_INVIS_TRAP) {
                tile.field_mark = true;
                trapChangeVisibility(Coord_t{y, x});
                detected = true;
            } else if (treasure_list[tile.treasure_id].category_id == TV_CHEST) {
                Inventory_t &item = treasure_list[tile.treasure_id];
                spellItemIdentifyAndRemoveRandomInscription(item);
            }
        }
    }

    return detected;
}

// Locates and displays all secret doors on current panel -RAK-
bool spellDetectSecretDoorssWithinVicinity() {
    bool detected = false;

    for (int y = dg.panel.top; y <= dg.panel.bottom; y++) {
        for (int x = dg.panel.left; x <= dg.panel.right; x++) {
            Tile_t &tile = dg.floor[y][x];

            if (tile.treasure_id == 0) {
                continue;
            }

            if (treasure_list[tile.treasure_id].category_id == TV_SECRET_DOOR) {
                // Secret doors

                tile.field_mark = true;
                trapChangeVisibility(Coord_t{y, x});
                detected = true;
            } else if ((treasure_list[tile.treasure_id].category_id == TV_UP_STAIR || treasure_list[tile.treasure_id].category_id == TV_DOWN_STAIR) && !tile.field_mark) {
                // Staircases

                tile.field_mark = true;
                dungeonLiteSpot(Coord_t{y, x});
                detected = true;
            }
        }
    }

    return detected;
}

// Locates and displays all invisible creatures on current panel -RAK-
bool spellDetectInvisibleCreaturesWithinVicinity() {
    bool detected = false;

    for (int id = next_free_monster_id - 1; id >= config::monsters::MON_MIN_INDEX_ID; id--) {
        Monster_t &monster = monsters[id];

        if (coordInsidePanel(Coord_t{monster.y, monster.x}) && ((creatures_list[monster.creature_id].movement & config::monsters::move::CM_INVISIBLE) != 0u)) {
            monster.lit = true;

            // works correctly even if hallucinating
            panelPutTile((char) creatures_list[monster.creature_id].sprite, Coord_t{monster.y, monster.x});

            detected = true;
        }
    }

    if (detected) {
        printMessage("You sense the presence of invisible creatures!");
        printMessage(CNIL);

        // must unlight every monster just lighted
        updateMonsters(false);
    }

    return detected;
}

// Light an area: -RAK-
//     1.  If corridor  light immediate area
//     2.  If room      light entire room plus immediate area.
bool spellLightArea(int y, int x) {
    if (py.flags.blind < 1) {
        printMessage("You are surrounded by a white light.");
    }

    // NOTE: this is not changed anywhere. A bug or correct? -MRC-
    bool lit = true;

    if (dg.floor[y][x].perma_lit_room && dg.current_level > 0) {
        dungeonLightRoom(Coord_t{y, x});
    }

    // Must always light immediate area, because one might be standing on
    // the edge of a room, or next to a destroyed area, etc.
    for (int i = y - 1; i <= y + 1; i++) {
        for (int j = x - 1; j <= x + 1; j++) {
            dg.floor[i][j].permanent_light = true;
            dungeonLiteSpot(Coord_t{i, j});
        }
    }

    return lit;
}

// Darken an area, opposite of light area -RAK-
bool spellDarkenArea(int y, int x) {
    bool darkened = false;

    if (dg.floor[y][x].perma_lit_room && dg.current_level > 0) {
        int half_height = (SCREEN_HEIGHT / 2);
        int half_width = (SCREEN_WIDTH / 2);
        int start_row = (y / half_height) * half_height + 1;
        int start_col = (x / half_width) * half_width + 1;
        int end_row = start_row + half_height - 1;
        int end_col = start_col + half_width - 1;

        for (int row = start_row; row <= end_row; row++) {
            for (int col = start_col; col <= end_col; col++) {
                Tile_t &tile = dg.floor[row][col];

                if (tile.perma_lit_room && tile.feature_id <= MAX_CAVE_FLOOR) {
                    tile.permanent_light = false;
                    tile.feature_id = TILE_DARK_FLOOR;

                    dungeonLiteSpot(Coord_t{row, col});

                    if (!caveTileVisible(Coord_t{row, col})) {
                        darkened = true;
                    }
                }
            }
        }
    } else {
        for (int row = y - 1; row <= y + 1; row++) {
            for (int col = x - 1; col <= x + 1; col++) {
                Tile_t &tile = dg.floor[row][col];

                if (tile.feature_id == TILE_CORR_FLOOR && tile.permanent_light) {
                    // permanent_light could have been set by star-lite wand, etc
                    tile.permanent_light = false;
                    darkened = true;
                }
            }
        }
    }

    if (darkened && py.flags.blind < 1) {
        printMessage("Darkness surrounds you.");
    }

    return darkened;
}

static void dungeonLightAreaAroundFloorTile(int y, int x) {
    for (int yy = y - 1; yy <= y + 1; yy++) {
        for (int xx = x - 1; xx <= x + 1; xx++) {
            Tile_t &tile = dg.floor[yy][xx];

            if (tile.feature_id >= MIN_CAVE_WALL) {
                tile.permanent_light = true;
            } else if (tile.treasure_id != 0 && treasure_list[tile.treasure_id].category_id >= TV_MIN_VISIBLE && treasure_list[tile.treasure_id].category_id <= TV_MAX_VISIBLE) {
                tile.field_mark = true;
            }
        }
    }
}

// Map the current area plus some -RAK-
void spellMapCurrentArea() {
    int row_min = dg.panel.top - randomNumber(10);
    int row_max = dg.panel.bottom + randomNumber(10);
    int col_min = dg.panel.left - randomNumber(20);
    int col_max = dg.panel.right + randomNumber(20);

    for (int y = row_min; y <= row_max; y++) {
        for (int x = col_min; x <= col_max; x++) {
            if (coordInBounds(Coord_t{y, x}) && dg.floor[y][x].feature_id <= MAX_CAVE_FLOOR) {
                dungeonLightAreaAroundFloorTile(y, x);
            }
        }
    }

    drawDungeonPanel();
}

// Identify an object -RAK-
bool spellIdentifyItem() {
    int item_id;
    if (!inventoryGetInputForItemId(item_id, "Item you wish identified?", 0, PLAYER_INVENTORY_SIZE, CNIL, CNIL)) {
        return false;
    }

    itemIdentify(inventory[item_id], item_id);

    Inventory_t &item = inventory[item_id];
    spellItemIdentifyAndRemoveRandomInscription(item);

    obj_desc_t description = {'\0'};
    itemDescription(description, item, true);

    obj_desc_t msg = {'\0'};
    if (item_id >= player_equipment::EQUIPMENT_WIELD) {
        playerRecalculateBonuses();
        (void) sprintf(msg, "%s: %s", playerItemWearingDescription(item_id), description);
    } else {
        (void) sprintf(msg, "%c %s", item_id + 97, description);
    }
    printMessage(msg);

    return true;
}

// Get all the monsters on the level pissed off. -RAK-
bool spellAggravateMonsters(int affect_distance) {
    bool aggravated = false;

    for (int id = next_free_monster_id - 1; id >= config::monsters::MON_MIN_INDEX_ID; id--) {
        Monster_t &monster = monsters[id];
        monster.sleep_count = 0;

        if (monster.distance_from_player <= affect_distance && monster.speed < 2) {
            monster.speed++;
            aggravated = true;
        }
    }

    if (aggravated) {
        printMessage("You hear a sudden stirring in the distance!");
    }

    return aggravated;
}

// Surround the fool with traps (chuckle) -RAK-
bool spellSurroundPlayerWithTraps() {
    for (int y = py.row - 1; y <= py.row + 1; y++) {
        for (int x = py.col - 1; x <= py.col + 1; x++) {
            // Don't put a trap under the player, since this can lead to
            // strange situations, e.g. falling through a trap door while
            // trying to rest, setting off a falling rock trap and ending
            // up under the rock.
            if (y == py.row && x == py.col) {
                continue;
            }

            Tile_t const &tile = dg.floor[y][x];

            if (tile.feature_id <= MAX_CAVE_FLOOR) {
                if (tile.treasure_id != 0) {
                    (void) dungeonDeleteObject(Coord_t{y, x});;
                }

                dungeonSetTrap(Coord_t{y, x}, randomNumber(config::dungeon::objects::MAX_TRAPS) - 1);

                // don't let player gain exp from the newly created traps
                treasure_list[tile.treasure_id].misc_use = 0;

                // open pits are immediately visible, so call dungeonLiteSpot
                dungeonLiteSpot(Coord_t{y, x});
            }
        }
    }

    // traps are always placed, so just return true
    return true;
}

// Surround the player with doors. -RAK-
bool spellSurroundPlayerWithDoors() {
    bool created = false;

    for (int y = py.row - 1; y <= py.row + 1; y++) {
        for (int x = py.col - 1; x <= py.col + 1; x++) {
            // Don't put a door under the player!
            if (y == py.row && x == py.col) {
                continue;
            }

            Tile_t &tile = dg.floor[y][x];

            if (tile.feature_id <= MAX_CAVE_FLOOR) {
                if (tile.treasure_id != 0) {
                    (void) dungeonDeleteObject(Coord_t{y, x});;
                }

                int free_id = popt();
                tile.feature_id = TILE_BLOCKED_FLOOR;
                tile.treasure_id = (uint8_t) free_id;

                inventoryItemCopyTo(config::dungeon::objects::OBJ_CLOSED_DOOR, treasure_list[free_id]);
                dungeonLiteSpot(Coord_t{y, x});

                created = true;
            }
        }
    }

    return created;
}

// Destroys any adjacent door(s)/trap(s) -RAK-
bool spellDestroyAdjacentDoorsTraps() {
    bool destroyed = false;

    for (int y = py.row - 1; y <= py.row + 1; y++) {
        for (int x = py.col - 1; x <= py.col + 1; x++) {
            Tile_t const &tile = dg.floor[y][x];

            if (tile.treasure_id == 0) {
                continue;
            }

            Inventory_t &item = treasure_list[tile.treasure_id];

            if ((item.category_id >= TV_INVIS_TRAP && item.category_id <= TV_CLOSED_DOOR && item.category_id != TV_RUBBLE) || item.category_id == TV_SECRET_DOOR) {
                if (dungeonDeleteObject(Coord_t{y, x})) {
                    destroyed = true;
                }
            } else if (item.category_id == TV_CHEST && item.flags != 0) {
                // destroy traps on chest and unlock
                item.flags &= ~(config::treasure::chests::CH_TRAPPED | config::treasure::chests::CH_LOCKED);
                item.special_name_id = special_name_ids::SN_UNLOCKED;

                destroyed = true;

                printMessage("You have disarmed the chest.");
                spellItemIdentifyAndRemoveRandomInscription(item);
            }
        }
    }

    return destroyed;
}

// Display all creatures on the current panel -RAK-
bool spellDetectMonsters() {
    bool detected = false;

    for (int id = next_free_monster_id - 1; id >= config::monsters::MON_MIN_INDEX_ID; id--) {
        Monster_t &monster = monsters[id];

        if (coordInsidePanel(Coord_t{monster.y, monster.x}) && (creatures_list[monster.creature_id].movement & config::monsters::move::CM_INVISIBLE) == 0) {
            monster.lit = true;
            detected = true;

            // works correctly even if hallucinating
            panelPutTile((char) creatures_list[monster.creature_id].sprite, Coord_t{monster.y, monster.x});
        }
    }

    if (detected) {
        printMessage("You sense the presence of monsters!");
        printMessage(CNIL);

        // must unlight every monster just lighted
        updateMonsters(false);
    }

    return detected;
}

// Update monster when light line spell touches it.
static void spellLightLineTouchesMonster(int monster_id) {
    Monster_t const &monster = monsters[monster_id];
    Creature_t const &creature = creatures_list[monster.creature_id];

    // light up and draw monster
    monsterUpdateVisibility(monster_id);

    auto name = monsterNameDescription(creature.name, monster.lit);

    if ((creature.defenses & config::monsters::defense::CD_LIGHT) != 0) {
        if (monster.lit) {
            creature_recall[monster.creature_id].defenses |= config::monsters::defense::CD_LIGHT;
        }

        if (monsterTakeHit(monster_id, diceRoll(Dice_t{2, 8})) >= 0) {
            printMonsterActionText(name, "shrivels away in the light!");
            displayCharacterExperience();
        } else {
            printMonsterActionText(name, "cringes from the light!");
        }
    }
}

// Leave a line of light in given dir, blue light can sometimes hurt creatures. -RAK-
void spellLightLine(int x, int y, int direction) {
    int distance = 0;
    bool finished = false;

    while (!finished) {
        Tile_t &tile = dg.floor[y][x];

        if (distance > config::treasure::OBJECT_BOLTS_MAX_RANGE || tile.feature_id >= MIN_CLOSED_SPACE) {
            (void) playerMovePosition(direction, y, x);
            finished = true;
            continue; // we're done here, break out of the loop
        }

        if (!tile.permanent_light && !tile.temporary_light) {
            // set permanent_light so that dungeonLiteSpot will work
            tile.permanent_light = true;

            if (tile.feature_id == TILE_LIGHT_FLOOR) {
                if (coordInsidePanel(Coord_t{y, x})) {
                    dungeonLightRoom(Coord_t{y, x});
                }
            } else {
                dungeonLiteSpot(Coord_t{y, x});
            }
        }

        // set permanent_light in case temporary_light was true above
        tile.permanent_light = true;

        if (tile.creature_id > 1) {
            spellLightLineTouchesMonster((int) tile.creature_id);
        }

        // move must be at end because want to light up current spot
        (void) playerMovePosition(direction, y, x);
        distance++;
    }
}

// Light line in all directions -RAK-
void spellStarlite(int y, int x) {
    if (py.flags.blind < 1) {
        printMessage("The end of the staff bursts into a blue shimmering light.");
    }

    for (int dir = 1; dir <= 9; dir++) {
        if (dir != 5) {
            spellLightLine(x, y, dir);
        }
    }
}

// Disarms all traps/chests in a given direction -RAK-
bool spellDisarmAllInDirection(int y, int x, int direction) {
    int distance = 0;
    bool disarmed = false;

    Tile_t *tile = nullptr;

    do {
        tile = &dg.floor[y][x];

        // note, must continue up to and including the first non open space,
        // because secret doors have feature_id greater than MAX_OPEN_SPACE
        if (tile->treasure_id != 0) {
            Inventory_t &item = treasure_list[tile->treasure_id];

            if (item.category_id == TV_INVIS_TRAP || item.category_id == TV_VIS_TRAP) {
                if (dungeonDeleteObject(Coord_t{y, x})) {
                    disarmed = true;
                }
            } else if (item.category_id == TV_CLOSED_DOOR) {
                // Locked or jammed doors become merely closed.
                item.misc_use = 0;
            } else if (item.category_id == TV_SECRET_DOOR) {
                tile->field_mark = true;
                trapChangeVisibility(Coord_t{y, x});
                disarmed = true;
            } else if (item.category_id == TV_CHEST && item.flags != 0) {
                disarmed = true;
                printMessage("Click!");

                item.flags &= ~(config::treasure::chests::CH_TRAPPED | config::treasure::chests::CH_LOCKED);
                item.special_name_id = special_name_ids::SN_UNLOCKED;

                spellItemIdentifyAndRemoveRandomInscription(item);
            }
        }

        // move must be at end because want to light up current spot
        (void) playerMovePosition(direction, y, x);

        distance++;
    } while (distance <= config::treasure::OBJECT_BOLTS_MAX_RANGE && tile->feature_id <= MAX_OPEN_SPACE);

    return disarmed;
}

// Return flags for given type area affect -RAK-
static void spellGetAreaAffectFlags(int spell_type, uint32_t &weapon_type, int &harm_type, bool (**destroy)(Inventory_t *)) {
    switch (spell_type) {
        case magic_spell_flags::GF_MAGIC_MISSILE:
            weapon_type = 0;
            harm_type = 0;
            *destroy = setNull;
            break;
        case magic_spell_flags::GF_LIGHTNING:
            weapon_type = config::monsters::spells::CS_BR_LIGHT;
            harm_type = config::monsters::defense::CD_LIGHT;
            *destroy = setLightningDestroyableItems;
            break;
        case magic_spell_flags::GF_POISON_GAS:
            weapon_type = config::monsters::spells::CS_BR_GAS;
            harm_type = config::monsters::defense::CD_POISON;
            *destroy = setNull;
            break;
        case magic_spell_flags::GF_ACID:
            weapon_type = config::monsters::spells::CS_BR_ACID;
            harm_type = config::monsters::defense::CD_ACID;
            *destroy = setAcidDestroyableItems;
            break;
        case magic_spell_flags::GF_FROST:
            weapon_type = config::monsters::spells::CS_BR_FROST;
            harm_type = config::monsters::defense::CD_FROST;
            *destroy = setFrostDestroyableItems;
            break;
        case magic_spell_flags::GF_FIRE:
            weapon_type = config::monsters::spells::CS_BR_FIRE;
            harm_type = config::monsters::defense::CD_FIRE;
            *destroy = setFireDestroyableItems;
            break;
        case magic_spell_flags::GF_HOLY_ORB:
            weapon_type = 0;
            harm_type = config::monsters::defense::CD_EVIL;
            *destroy = setNull;
            break;
        default:
            printMessage("ERROR in spellGetAreaAffectFlags()\n");
    }
}

static void printBoltStrikesMonsterMessage(Creature_t const &creature, const std::string &bolt_name, bool is_lit) {
    std::string monster_name;
    if (is_lit) {
        monster_name = "the " + std::string(creature.name);
    } else {
        monster_name = "it";
    }
    std::string msg = "The " + bolt_name + " strikes " + monster_name + ".";
    printMessage(msg.c_str());
}

// Light up, draw, and check for monster damage when Fire Bolt touches it.
static void spellFireBoltTouchesMonster(Tile_t &tile, int damage, int harm_type, uint32_t weapon_id, const std::string &bolt_name) {
    Monster_t const &monster = monsters[tile.creature_id];
    Creature_t const &creature = creatures_list[monster.creature_id];

    // light up monster and draw monster, temporarily set
    // permanent_light so that `monsterUpdateVisibility()` will work
    bool saved_lit_status = tile.permanent_light;
    tile.permanent_light = true;
    monsterUpdateVisibility((int) tile.creature_id);
    tile.permanent_light = saved_lit_status;

    // draw monster and clear previous bolt
    putQIO();

    printBoltStrikesMonsterMessage(creature, bolt_name, monster.lit);

    if ((harm_type & creature.defenses) != 0) {
        damage = damage * 2;
        if (monster.lit) {
            creature_recall[monster.creature_id].defenses |= harm_type;
        }
    } else if ((weapon_id & creature.spells) != 0u) {
        damage = damage / 4;
        if (monster.lit) {
            creature_recall[monster.creature_id].spells |= weapon_id;
        }
    }

    auto name = monsterNameDescription(creature.name, monster.lit);

    if (monsterTakeHit((int) tile.creature_id, damage) >= 0) {
        printMonsterActionText(name, "dies in a fit of agony.");
        displayCharacterExperience();
    } else if (damage > 0) {
        printMonsterActionText(name, "screams in agony.");
    }
}

// Shoot a bolt in a given direction -RAK-
void spellFireBolt(int y, int x, int direction, int damage_hp, int spell_type, const std::string &spell_name) {
    bool (*dummy)(Inventory_t *);
    int harm_type = 0;
    uint32_t weapon_type;
    spellGetAreaAffectFlags(spell_type, weapon_type, harm_type, &dummy);

    int distance = 0;
    bool finished = false;

    while (!finished) {
        int old_y = y;
        int old_x = x;

        (void) playerMovePosition(direction, y, x);
        distance++;

        Tile_t &tile = dg.floor[y][x];

        dungeonLiteSpot(Coord_t{old_y, old_x});

        if (distance > config::treasure::OBJECT_BOLTS_MAX_RANGE || tile.feature_id >= MIN_CLOSED_SPACE) {
            finished = true;
            continue; // we're done here, break out of the loop
        }

        if (tile.creature_id > 1) {
            finished = true;
            spellFireBoltTouchesMonster(tile, damage_hp, harm_type, weapon_type, spell_name);
        } else if (coordInsidePanel(Coord_t{y, x}) && py.flags.blind < 1) {
            panelPutTile('*', Coord_t{y, x});

            // show the bolt
            putQIO();
        }
    }
}

// Shoot a ball in a given direction.  Note that balls have an area affect. -RAK-
void spellFireBall(int y, int x, int direction, int damage_hp, int spell_type, const std::string &spell_name) {
    int total_hits = 0;
    int total_kills = 0;
    int max_distance = 2;

    bool (*destroy)(Inventory_t *);
    int harm_type;
    uint32_t weapon_type;
    spellGetAreaAffectFlags(spell_type, weapon_type, harm_type, &destroy);

    int distance = 0;

    bool finished = false;
    while (!finished) {
        int old_y = y;
        int old_x = x;

        (void) playerMovePosition(direction, y, x);
        distance++;

        dungeonLiteSpot(Coord_t{old_y, old_x});

        if (distance > config::treasure::OBJECT_BOLTS_MAX_RANGE) {
            finished = true;
            continue;
        }

        Tile_t *tile = &dg.floor[y][x];

        if (tile->feature_id >= MIN_CLOSED_SPACE || tile->creature_id > 1) {
            finished = true;

            if (tile->feature_id >= MIN_CLOSED_SPACE) {
                y = old_y;
                x = old_x;
            }

            // The ball hits and explodes.

            // The explosion.
            for (int row = y - max_distance; row <= y + max_distance; row++) {
                for (int col = x - max_distance; col <= x + max_distance; col++) {
                    if (coordInBounds(Coord_t{row, col}) && coordDistanceBetween(Coord_t{y, x}, Coord_t{row, col}) <= max_distance && los(y, x, row, col)) {
                        tile = &dg.floor[row][col];

                        if (tile->treasure_id != 0 && (*destroy)(&treasure_list[tile->treasure_id])) {
                            (void) dungeonDeleteObject(Coord_t{row, col});
                        }

                        if (tile->feature_id <= MAX_OPEN_SPACE) {
                            if (tile->creature_id > 1) {
                                Monster_t const &monster = monsters[tile->creature_id];
                                Creature_t const &creature = creatures_list[monster.creature_id];

                                // lite up creature if visible, temp set permanent_light so that monsterUpdateVisibility works
                                bool saved_lit_status = tile->permanent_light;
                                tile->permanent_light = true;
                                monsterUpdateVisibility((int) tile->creature_id);

                                total_hits++;
                                int damage = damage_hp;

                                if ((harm_type & creature.defenses) != 0) {
                                    damage = damage * 2;
                                    if (monster.lit) {
                                        creature_recall[monster.creature_id].defenses |= harm_type;
                                    }
                                } else if ((weapon_type & creature.spells) != 0u) {
                                    damage = damage / 4;
                                    if (monster.lit) {
                                        creature_recall[monster.creature_id].spells |= weapon_type;
                                    }
                                }

                                damage = (damage / (coordDistanceBetween(Coord_t{row, col}, Coord_t{y, x}) + 1));

                                if (monsterTakeHit((int) tile->creature_id, damage) >= 0) {
                                    total_kills++;
                                }
                                tile->permanent_light = saved_lit_status;
                            } else if (coordInsidePanel(Coord_t{row, col}) && py.flags.blind < 1) {
                                panelPutTile('*', Coord_t{row, col});
                            }
                        }
                    }
                }
            }

            // show ball of whatever
            putQIO();

            for (int row = (y - 2); row <= (y + 2); row++) {
                for (int col = (x - 2); col <= (x + 2); col++) {
                    if (coordInBounds(Coord_t{row, col}) && coordInsidePanel(Coord_t{row, col}) && coordDistanceBetween(Coord_t{y, x}, Coord_t{row, col}) <= max_distance) {
                        dungeonLiteSpot(Coord_t{row, col});
                    }
                }
            }
            // End  explosion.

            if (total_hits == 1) {
                printMessage(("The " + spell_name + " envelops a creature!").c_str());
            } else if (total_hits > 1) {
                printMessage(("The " + spell_name + " envelops several creatures!").c_str());
            }

            if (total_kills == 1) {
                printMessage("There is a scream of agony!");
            } else if (total_kills > 1) {
                printMessage("There are several screams of agony!");
            }

            if (total_kills >= 0) {
                displayCharacterExperience();
            }
            // End ball hitting.
        } else if (coordInsidePanel(Coord_t{y, x}) && py.flags.blind < 1) {
            panelPutTile('*', Coord_t{y, x});

            // show bolt
            putQIO();
        }
    }
}

// Breath weapon works like a spellFireBall(), but affects the player.
// Note the area affect. -RAK-
void spellBreath(int y, int x, int monster_id, int damage_hp, int spell_type, const std::string &spell_name) {
    int max_distance = 2;

    bool (*destroy)(Inventory_t *);
    int harm_type;
    uint32_t weapon_type;
    spellGetAreaAffectFlags(spell_type, weapon_type, harm_type, &destroy);

    for (int row = y - 2; row <= y + 2; row++) {
        for (int col = x - 2; col <= x + 2; col++) {
            if (coordInBounds(Coord_t{row, col}) && coordDistanceBetween(Coord_t{y, x}, Coord_t{row, col}) <= max_distance && los(y, x, row, col)) {
                Tile_t const &tile = dg.floor[row][col];

                if (tile.treasure_id != 0 && (*destroy)(&treasure_list[tile.treasure_id])) {
                    (void) dungeonDeleteObject(Coord_t{row, col});
                }

                if (tile.feature_id <= MAX_OPEN_SPACE) {
                    // must test status bit, not py.flags.blind here, flag could have
                    // been set by a previous monster, but the breath should still
                    // be visible until the blindness takes effect
                    if (coordInsidePanel(Coord_t{row, col}) && ((py.flags.status & config::player::status::PY_BLIND) == 0u)) {
                        panelPutTile('*', Coord_t{row, col});
                    }

                    if (tile.creature_id > 1) {
                        Monster_t &monster = monsters[tile.creature_id];
                        Creature_t const &creature = creatures_list[monster.creature_id];

                        int damage = damage_hp;

                        if ((harm_type & creature.defenses) != 0) {
                            damage = damage * 2;
                        } else if ((weapon_type & creature.spells) != 0u) {
                            damage = (damage / 4);
                        }

                        damage = (damage / (coordDistanceBetween(Coord_t{row, col}, Coord_t{y, x}) + 1));

                        // can not call monsterTakeHit here, since player does not
                        // get experience for kill
                        monster.hp = (int16_t) (monster.hp - damage);
                        monster.sleep_count = 0;

                        if (monster.hp < 0) {
                            uint32_t treasure_id = monsterDeath((int) monster.y, (int) monster.x, creature.movement);

                            if (monster.lit) {
                                auto tmp = (uint32_t) ((creature_recall[monster.creature_id].movement & config::monsters::move::CM_TREASURE) >> config::monsters::move::CM_TR_SHIFT);
                                if (tmp > ((treasure_id & config::monsters::move::CM_TREASURE) >> config::monsters::move::CM_TR_SHIFT)) {
                                    treasure_id = (uint32_t) ((treasure_id & ~config::monsters::move::CM_TREASURE) | (tmp << config::monsters::move::CM_TR_SHIFT));
                                }
                                creature_recall[monster.creature_id].movement = (uint32_t) (treasure_id | (creature_recall[monster.creature_id].movement & ~config::monsters::move::CM_TREASURE));
                            }

                            // It ate an already processed monster. Handle normally.
                            if (monster_id < tile.creature_id) {
                                dungeonDeleteMonster((int) tile.creature_id);
                            } else {
                                // If it eats this monster, an already processed monster
                                // will take its place, causing all kinds of havoc.
                                // Delay the kill a bit.
                                dungeonDeleteMonsterFix1((int) tile.creature_id);
                            }
                        }
                    } else if (tile.creature_id == 1) {
                        int damage = (damage_hp / (coordDistanceBetween(Coord_t{row, col}, Coord_t{y, x}) + 1));

                        // let's do at least one point of damage
                        // prevents randomNumber(0) problem with damagePoisonedGas, also
                        if (damage == 0) {
                            damage = 1;
                        }

                        switch (spell_type) {
                            case magic_spell_flags::GF_LIGHTNING:
                                damageLightningBolt(damage, spell_name.c_str());
                                break;
                            case magic_spell_flags::GF_POISON_GAS:
                                damagePoisonedGas(damage, spell_name.c_str());
                                break;
                            case magic_spell_flags::GF_ACID:
                                damageAcid(damage, spell_name.c_str());
                                break;
                            case magic_spell_flags::GF_FROST:
                                damageCold(damage, spell_name.c_str());
                                break;
                            case magic_spell_flags::GF_FIRE:
                                damageFire(damage, spell_name.c_str());
                                break;
                            default:
                                break;
                        }
                    }
                }
            }
        }
    }

    // show the ball of gas
    putQIO();

    for (int row = (y - 2); row <= (y + 2); row++) {
        for (int col = (x - 2); col <= (x + 2); col++) {
            if (coordInBounds(Coord_t{row, col}) && coordInsidePanel(Coord_t{row, col}) && coordDistanceBetween(Coord_t{y, x}, Coord_t{row, col}) <= max_distance) {
                dungeonLiteSpot(Coord_t{row, col});
            }
        }
    }
}

// Recharge a wand, staff, or rod.  Sometimes the item breaks. -RAK-
bool spellRechargeItem(int number_of_charges) {
    int item_pos_start, item_pos_end;
    if (!inventoryFindRange(TV_STAFF, TV_WAND, item_pos_start, item_pos_end)) {
        printMessage("You have nothing to recharge.");
        return false;
    }

    int item_id;
    if (!inventoryGetInputForItemId(item_id, "Recharge which item?", item_pos_start, item_pos_end, CNIL, CNIL)) {
        return false;
    }

    Inventory_t &item = inventory[item_id];

    // recharge  I = recharge(20) = 1/6  failure for empty 10th level wand
    // recharge II = recharge(60) = 1/10 failure for empty 10th level wand
    //
    // make it harder to recharge high level, and highly charged wands,
    // note that `fail_chance` can be negative, so check its value before
    // trying to call randomNumber().
    int fail_chance = number_of_charges + 50 - (int) item.depth_first_found - item.misc_use;

    // Automatic failure.
    if (fail_chance < 19) {
        fail_chance = 1;
    } else {
        fail_chance = randomNumber(fail_chance / 10);
    }

    if (fail_chance == 1) {
        printMessage("There is a bright flash of light.");
        inventoryDestroyItem(item_id);
    } else {
        number_of_charges = (number_of_charges / (item.depth_first_found + 2)) + 1;
        item.misc_use += 2 + randomNumber(number_of_charges);

        if (spellItemIdentified(item)) {
            spellItemRemoveIdentification(item);
        }

        itemIdentificationClearEmpty(item);
    }

    return true;
}

// Increase or decrease a creatures hit points -RAK-
bool spellChangeMonsterHitPoints(int y, int x, int direction, int damage_hp) {
    int distance = 0;
    bool changed = false;
    bool finished = false;

    while (!finished) {
        (void) playerMovePosition(direction, y, x);
        distance++;

        Tile_t const &tile = dg.floor[y][x];

        if (distance > config::treasure::OBJECT_BOLTS_MAX_RANGE || tile.feature_id >= MIN_CLOSED_SPACE) {
            finished = true;
            continue;
        }

        if (tile.creature_id > 1) {
            finished = true;

            Monster_t const &monster = monsters[tile.creature_id];
            Creature_t const &creature = creatures_list[monster.creature_id];

            auto name = monsterNameDescription(creature.name, monster.lit);

            if (monsterTakeHit((int) tile.creature_id, damage_hp) >= 0) {
                printMonsterActionText(name, "dies in a fit of agony.");
                displayCharacterExperience();
            } else if (damage_hp > 0) {
                printMonsterActionText(name, "screams in agony.");
            }

            changed = true;
        }
    }

    return changed;
}

// Drains life; note it must be living. -RAK-
bool spellDrainLifeFromMonster(int y, int x, int direction) {
    int distance = 0;
    bool drained = false;
    bool finished = false;

    while (!finished) {
        (void) playerMovePosition(direction, y, x);
        distance++;

        Tile_t const &tile = dg.floor[y][x];

        if (distance > config::treasure::OBJECT_BOLTS_MAX_RANGE || tile.feature_id >= MIN_CLOSED_SPACE) {
            finished = true;
            continue;
        }

        if (tile.creature_id > 1) {
            finished = true;

            Monster_t const &monster = monsters[tile.creature_id];
            Creature_t const &creature = creatures_list[monster.creature_id];

            if ((creature.defenses & config::monsters::defense::CD_UNDEAD) == 0) {
                auto name = monsterNameDescription(creature.name, monster.lit);

                if (monsterTakeHit((int) tile.creature_id, 75) >= 0) {
                    printMonsterActionText(name, "dies in a fit of agony.");
                    displayCharacterExperience();
                } else {
                    printMonsterActionText(name, "screams in agony.");
                }

                drained = true;
            } else {
                creature_recall[monster.creature_id].defenses |= config::monsters::defense::CD_UNDEAD;
            }
        }
    }

    return drained;
}

// Increase or decrease a creatures speed -RAK-
// NOTE: cannot slow a winning creature (BALROG)
bool spellSpeedMonster(int y, int x, int direction, int speed) {
    int distance = 0;
    bool changed = false;
    bool finished = false;

    while (!finished) {
        (void) playerMovePosition(direction, y, x);
        distance++;

        Tile_t const &tile = dg.floor[y][x];

        if (distance > config::treasure::OBJECT_BOLTS_MAX_RANGE || tile.feature_id >= MIN_CLOSED_SPACE) {
            finished = true;
            continue;
        }

        if (tile.creature_id > 1) {
            finished = true;

            Monster_t &monster = monsters[tile.creature_id];
            Creature_t const &creature = creatures_list[monster.creature_id];

            auto name = monsterNameDescription(creature.name, monster.lit);

            if (speed > 0) {
                monster.speed += speed;
                monster.sleep_count = 0;

                changed = true;

                printMonsterActionText(name, "starts moving faster.");
            } else if (randomNumber(MON_MAX_LEVELS) > creature.level) {
                monster.speed += speed;
                monster.sleep_count = 0;

                changed = true;

                printMonsterActionText(name, "starts moving slower.");
            } else {
                monster.sleep_count = 0;

                printMonsterActionText(name, "is unaffected.");
            }
        }
    }

    return changed;
}

// Confuse a creature -RAK-
bool spellConfuseMonster(int y, int x, int direction) {
    int distance = 0;
    bool confused = false;
    bool finished = false;

    while (!finished) {
        (void) playerMovePosition(direction, y, x);
        distance++;

        Tile_t const &tile = dg.floor[y][x];

        if (distance > config::treasure::OBJECT_BOLTS_MAX_RANGE || tile.feature_id >= MIN_CLOSED_SPACE) {
            finished = true;
            continue;
        }

        if (tile.creature_id > 1) {
            finished = true;

            Monster_t &monster = monsters[tile.creature_id];
            Creature_t const &creature = creatures_list[monster.creature_id];

            auto name = monsterNameDescription(creature.name, monster.lit);

            if (randomNumber(MON_MAX_LEVELS) < creature.level || ((creature.defenses & config::monsters::defense::CD_NO_SLEEP) != 0)) {
                if (monster.lit && ((creature.defenses & config::monsters::defense::CD_NO_SLEEP) != 0)) {
                    creature_recall[monster.creature_id].defenses |= config::monsters::defense::CD_NO_SLEEP;
                }

                // Monsters which resisted the attack should wake up.
                // Monsters with innate resistance ignore the attack.
                if ((creature.defenses & config::monsters::defense::CD_NO_SLEEP) == 0) {
                    monster.sleep_count = 0;
                }

                printMonsterActionText(name, "is unaffected.");
            } else {
                if (monster.confused_amount != 0u) {
                    monster.confused_amount += 3;
                } else {
                    monster.confused_amount = (uint8_t) (2 + randomNumber(16));
                }
                monster.sleep_count = 0;

                confused = true;

                printMonsterActionText(name, "appears confused.");
            }
        }
    }

    return confused;
}

// Sleep a creature. -RAK-
bool spellSleepMonster(int y, int x, int direction) {
    int distance = 0;
    bool asleep = false;
    bool finished = false;

    while (!finished) {
        (void) playerMovePosition(direction, y, x);
        distance++;

        Tile_t const &tile = dg.floor[y][x];

        if (distance > config::treasure::OBJECT_BOLTS_MAX_RANGE || tile.feature_id >= MIN_CLOSED_SPACE) {
            finished = true;
            continue;
        }

        if (tile.creature_id > 1) {
            finished = true;

            Monster_t &monster = monsters[tile.creature_id];
            Creature_t const &creature = creatures_list[monster.creature_id];

            auto name = monsterNameDescription(creature.name, monster.lit);

            if (randomNumber(MON_MAX_LEVELS) < creature.level || ((creature.defenses & config::monsters::defense::CD_NO_SLEEP) != 0)) {
                if (monster.lit && ((creature.defenses & config::monsters::defense::CD_NO_SLEEP) != 0)) {
                    creature_recall[monster.creature_id].defenses |= config::monsters::defense::CD_NO_SLEEP;
                }

                printMonsterActionText(name, "is unaffected.");
            } else {
                monster.sleep_count = 500;

                asleep = true;

                printMonsterActionText(name, "falls asleep.");
            }
        }
    }

    return asleep;
}

// Turn stone to mud, delete wall. -RAK-
bool spellWallToMud(int y, int x, int direction) {
    int distance = 0;
    bool turned = false;
    bool finished = false;

    while (!finished) {
        (void) playerMovePosition(direction, y, x);
        distance++;

        Tile_t const &tile = dg.floor[y][x];

        // note, this ray can move through walls as it turns them to mud
        if (distance == config::treasure::OBJECT_BOLTS_MAX_RANGE) {
            finished = true;
        }

        if (tile.feature_id >= MIN_CAVE_WALL && tile.feature_id != TILE_BOUNDARY_WALL) {
            finished = true;

            (void) playerTunnelWall(y, x, 1, 0);

            if (caveTileVisible(Coord_t{y, x})) {
                turned = true;
                printMessage("The wall turns into mud.");
            }
        } else if (tile.treasure_id != 0 && tile.feature_id >= MIN_CLOSED_SPACE) {
            finished = true;

            if (coordInsidePanel(Coord_t{y, x}) && caveTileVisible(Coord_t{y, x})) {
                turned = true;

                obj_desc_t description = {'\0'};
                itemDescription(description, treasure_list[tile.treasure_id], false);

                obj_desc_t out_val = {'\0'};
                (void) sprintf(out_val, "The %s turns into mud.", description);
                printMessage(out_val);
            }

            if (treasure_list[tile.treasure_id].category_id == TV_RUBBLE) {
                (void) dungeonDeleteObject(Coord_t{y, x});;
                if (randomNumber(10) == 1) {
                    dungeonPlaceRandomObjectAt(Coord_t{y, x}, false);
                    if (caveTileVisible(Coord_t{y, x})) {
                        printMessage("You have found something!");
                    }
                }
                dungeonLiteSpot(Coord_t{y, x});
            } else {
                (void) dungeonDeleteObject(Coord_t{y, x});;
            }
        }

        if (tile.creature_id > 1) {
            Monster_t const &monster = monsters[tile.creature_id];
            Creature_t const &creature = creatures_list[monster.creature_id];

            if ((creature.defenses & config::monsters::defense::CD_STONE) != 0) {
                auto name = monsterNameDescription(creature.name, monster.lit);

                // Should get these messages even if the monster is not visible.
                int creature_id = monsterTakeHit((int) tile.creature_id, 100);
                if (creature_id >= 0) {
                    creature_recall[creature_id].defenses |= config::monsters::defense::CD_STONE;
                    printMonsterActionText(name, "dissolves!");
                    displayCharacterExperience(); // print msg before calling prt_exp
                } else {
                    creature_recall[monster.creature_id].defenses |= config::monsters::defense::CD_STONE;
                    printMonsterActionText(name, "grunts in pain!");
                }
                finished = true;
            }
        }
    }

    return turned;
}

// Destroy all traps and doors in a given direction -RAK-
bool spellDestroyDoorsTrapsInDirection(int y, int x, int direction) {
    bool destroyed = false;

    int distance = 0;

    Tile_t *tile = nullptr;

    do {
        (void) playerMovePosition(direction, y, x);
        distance++;

        tile = &dg.floor[y][x];

        // must move into first closed spot, as it might be a secret door
        if (tile->treasure_id != 0) {
            Inventory_t &item = treasure_list[tile->treasure_id];

            if (item.category_id == TV_INVIS_TRAP || item.category_id == TV_CLOSED_DOOR || item.category_id == TV_VIS_TRAP || item.category_id == TV_OPEN_DOOR || item.category_id == TV_SECRET_DOOR) {
                if (dungeonDeleteObject(Coord_t{y, x})) {
                    destroyed = true;
                    printMessage("There is a bright flash of light!");
                }
            } else if (item.category_id == TV_CHEST && item.flags != 0) {
                destroyed = true;
                printMessage("Click!");

                item.flags &= ~(config::treasure::chests::CH_TRAPPED | config::treasure::chests::CH_LOCKED);
                item.special_name_id = special_name_ids::SN_UNLOCKED;

                spellItemIdentifyAndRemoveRandomInscription(item);
            }
        }
    } while ((distance <= config::treasure::OBJECT_BOLTS_MAX_RANGE) || tile->feature_id <= MAX_OPEN_SPACE);

    return destroyed;
}

// Polymorph a monster -RAK-
// NOTE: cannot polymorph a winning creature (BALROG)
bool spellPolymorphMonster(int y, int x, int direction) {
    int distance = 0;
    bool morphed = false;
    bool finished = false;

    while (!finished) {
        (void) playerMovePosition(direction, y, x);
        distance++;

        Tile_t const &tile = dg.floor[y][x];

        if (distance > config::treasure::OBJECT_BOLTS_MAX_RANGE || tile.feature_id >= MIN_CLOSED_SPACE) {
            finished = true;
            continue;
        }

        if (tile.creature_id > 1) {
            Monster_t const &monster = monsters[tile.creature_id];
            Creature_t const &creature = creatures_list[monster.creature_id];

            if (randomNumber(MON_MAX_LEVELS) > creature.level) {
                finished = true;

                dungeonDeleteMonster((int) tile.creature_id);

                // Place_monster() should always return true here.
                morphed = monsterPlaceNew(y, x, randomNumber(monster_levels[MON_MAX_LEVELS] - monster_levels[0]) - 1 + monster_levels[0], false);

                // don't test tile.field_mark here, only permanent_light/temporary_light
                if (morphed && coordInsidePanel(Coord_t{y, x}) && (tile.temporary_light || tile.permanent_light)) {
                    morphed = true;
                }
            } else {
                auto name = monsterNameDescription(creature.name, monster.lit);
                printMonsterActionText(name, "is unaffected.");
            }
        }
    }

    return morphed;
}

// Create a wall. -RAK-
bool spellBuildWall(int y, int x, int direction) {
    int distance = 0;
    bool built = false;
    bool finished = false;

    while (!finished) {
        (void) playerMovePosition(direction, y, x);
        distance++;

        Tile_t &tile = dg.floor[y][x];

        if (distance > config::treasure::OBJECT_BOLTS_MAX_RANGE || tile.feature_id >= MIN_CLOSED_SPACE) {
            finished = true;
            continue; // we're done here, break out of the loop
        }

        if (tile.treasure_id != 0) {
            (void) dungeonDeleteObject(Coord_t{y, x});;
        }

        if (tile.creature_id > 1) {
            finished = true;

            Monster_t &monster = monsters[tile.creature_id];
            Creature_t const &creature = creatures_list[monster.creature_id];

            if ((creature.movement & config::monsters::move::CM_PHASE) == 0u) {
                // monster does not move, can't escape the wall
                int damage;
                if ((creature.movement & config::monsters::move::CM_ATTACK_ONLY) != 0u) {
                    // this will kill everything
                    damage = 3000;
                } else {
                    damage = diceRoll(Dice_t{4, 8});
                }

                auto name = monsterNameDescription(creature.name, monster.lit);

                printMonsterActionText(name, "wails out in pain!");

                if (monsterTakeHit((int) tile.creature_id, damage) >= 0) {
                    printMonsterActionText(name, "is embedded in the rock.");
                    displayCharacterExperience();
                }
            } else if (creature.sprite == 'E' || creature.sprite == 'X') {
                // must be an earth elemental, an earth spirit,
                // or a Xorn to increase its hit points
                monster.hp += diceRoll(Dice_t{4, 8});
            }
        }

        tile.feature_id = TILE_MAGMA_WALL;
        tile.field_mark = false;

        // Permanently light this wall if it is lit by player's lamp.
        tile.permanent_light = (tile.temporary_light || tile.permanent_light);
        dungeonLiteSpot(Coord_t{y, x});

        built = true;
    }

    return built;
}

// Replicate a creature -RAK-
bool spellCloneMonster(int y, int x, int direction) {
    int distance = 0;
    bool finished = false;

    while (!finished) {
        (void) playerMovePosition(direction, y, x);
        distance++;

        Tile_t const &tile = dg.floor[y][x];

        if (distance > config::treasure::OBJECT_BOLTS_MAX_RANGE || tile.feature_id >= MIN_CLOSED_SPACE) {
            finished = true;
        } else if (tile.creature_id > 1) {
            monsters[tile.creature_id].sleep_count = 0;

            // monptr of 0 is safe here, since can't reach here from creatures
            return monsterMultiply(y, x, (int) monsters[tile.creature_id].creature_id, 0);
        }
    }

    return false;
}

// Move the creature record to a new location -RAK-
void spellTeleportAwayMonster(int monster_id, int distance_from_player) {
    int y, x;
    int counter = 0;
    Monster_t &monster = monsters[monster_id];

    do {
        do {
            y = monster.y + (randomNumber(2 * distance_from_player + 1) - (distance_from_player + 1));
            x = monster.x + (randomNumber(2 * distance_from_player + 1) - (distance_from_player + 1));
        } while (!coordInBounds(Coord_t{y, x}));

        counter++;
        if (counter > 9) {
            counter = 0;
            distance_from_player += 5;
        }
    } while ((dg.floor[y][x].feature_id >= MIN_CLOSED_SPACE) || (dg.floor[y][x].creature_id != 0));

    dungeonMoveCreatureRecord(Coord_t{monster.y, monster.x}, Coord_t{y, x});
    dungeonLiteSpot(Coord_t{monster.y, monster.x});

    monster.y = (uint8_t) y;
    monster.x = (uint8_t) x;

    // this is necessary, because the creature is
    // not currently visible in its new position.
    monster.lit = false;
    monster.distance_from_player = (uint8_t) coordDistanceBetween(Coord_t{py.row, py.col}, Coord_t{y, x});

    monsterUpdateVisibility(monster_id);
}

// Teleport player to spell casting creature -RAK-
void spellTeleportPlayerTo(int y, int x) {
    int to_y, to_x;
    int distance = 1;
    int counter = 0;

    do {
        to_y = y + (randomNumber(2 * distance + 1) - (distance + 1));
        to_x = x + (randomNumber(2 * distance + 1) - (distance + 1));
        counter++;
        if (counter > 9) {
            counter = 0;
            distance++;
        }
    } while (!coordInBounds(Coord_t{to_y, to_x}) || (dg.floor[to_y][to_x].feature_id >= MIN_CLOSED_SPACE) || (dg.floor[to_y][to_x].creature_id >= 2));

    dungeonMoveCreatureRecord(Coord_t{py.row, py.col}, Coord_t{to_y, to_x});

    for (int row = py.row - 1; row <= py.row + 1; row++) {
        for (int col = py.col - 1; col <= py.col + 1; col++) {
            dg.floor[row][col].temporary_light = false;
            dungeonLiteSpot(Coord_t{row, col});
        }
    }

    dungeonLiteSpot(Coord_t{py.row, py.col});

    py.row = (int16_t) to_y;
    py.col = (int16_t) to_x;

    dungeonResetView();

    // light creatures
    updateMonsters(false);
}

// Teleport all creatures in a given direction away -RAK-
bool spellTeleportAwayMonsterInDirection(int y, int x, int direction) {
    int distance = 0;
    bool teleported = false;
    bool finished = false;

    while (!finished) {
        (void) playerMovePosition(direction, y, x);
        distance++;

        Tile_t const &tile = dg.floor[y][x];

        if (distance > config::treasure::OBJECT_BOLTS_MAX_RANGE || tile.feature_id >= MIN_CLOSED_SPACE) {
            finished = true;
            continue;
        }

        if (tile.creature_id > 1) {
            // wake it up
            monsters[tile.creature_id].sleep_count = 0;

            spellTeleportAwayMonster((int) tile.creature_id, config::monsters::MON_MAX_SIGHT);

            teleported = true;
        }
    }

    return teleported;
}

// Delete all creatures within max_sight distance -RAK-
// NOTE : Winning creatures cannot be killed by genocide.
bool spellMassGenocide() {
    bool killed = false;

    for (int id = next_free_monster_id - 1; id >= config::monsters::MON_MIN_INDEX_ID; id--) {
        Monster_t const &monster = monsters[id];
        Creature_t const &creature = creatures_list[monster.creature_id];

        if (monster.distance_from_player <= config::monsters::MON_MAX_SIGHT && (creature.movement & config::monsters::move::CM_WIN) == 0) {
            killed = true;
            dungeonDeleteMonster(id);
        }
    }

    return killed;
}

// Delete all creatures of a given type from level. -RAK-
// This does not keep creatures of type from appearing later.
// NOTE : Winning creatures can not be killed by genocide.
bool spellGenocide() {
    char creature_char;
    if (!getCommand("Which type of creature do you wish exterminated?", creature_char)) {
        return false;
    }

    bool killed = false;

    for (int id = next_free_monster_id - 1; id >= config::monsters::MON_MIN_INDEX_ID; id--) {
        Monster_t const &monster = monsters[id];
        Creature_t const &creature = creatures_list[monster.creature_id];

        if (creature_char == creatures_list[monster.creature_id].sprite) {
            if ((creature.movement & config::monsters::move::CM_WIN) == 0) {
                killed = true;
                dungeonDeleteMonster(id);
            } else {
                // genocide is a powerful spell, so we will let the player
                // know the names of the creatures they did not destroy,
                // this message makes no sense otherwise
                printMessage(("The " + std::string(creature.name) + " is unaffected.").c_str());
            }
        }
    }

    return killed;
}

// Change speed of any creature . -RAK-
// NOTE: cannot slow a winning creature (BALROG)
bool spellSpeedAllMonsters(int speed) {
    bool speedy = false;

    for (int id = next_free_monster_id - 1; id >= config::monsters::MON_MIN_INDEX_ID; id--) {
        Monster_t &monster = monsters[id];
        Creature_t const &creature = creatures_list[monster.creature_id];

        auto name = monsterNameDescription(creature.name, monster.lit);

        if (monster.distance_from_player > config::monsters::MON_MAX_SIGHT || !los(py.row, py.col, monster.y, monster.x)) {
            continue; // do nothing
        }

        if (speed > 0) {
            monster.speed += speed;
            monster.sleep_count = 0;

            if (monster.lit) {
                speedy = true;
                printMonsterActionText(name, "starts moving faster.");
            }
        } else if (randomNumber(MON_MAX_LEVELS) > creature.level) {
            monster.speed += speed;
            monster.sleep_count = 0;

            if (monster.lit) {
                speedy = true;
                printMonsterActionText(name, "starts moving slower.");
            }
        } else if (monster.lit) {
            monster.sleep_count = 0;
            printMonsterActionText(name, "is unaffected.");
        }
    }

    return speedy;
}

// Sleep any creature . -RAK-
bool spellSleepAllMonsters() {
    bool asleep = false;

    for (int id = next_free_monster_id - 1; id >= config::monsters::MON_MIN_INDEX_ID; id--) {
        Monster_t &monster = monsters[id];
        Creature_t const &creature = creatures_list[monster.creature_id];

        auto name = monsterNameDescription(creature.name, monster.lit);

        if (monster.distance_from_player > config::monsters::MON_MAX_SIGHT || !los(py.row, py.col, monster.y, monster.x)) {
            continue; // do nothing
        }

        if (randomNumber(MON_MAX_LEVELS) < creature.level || ((creature.defenses & config::monsters::defense::CD_NO_SLEEP) != 0)) {
            if (monster.lit) {
                if ((creature.defenses & config::monsters::defense::CD_NO_SLEEP) != 0) {
                    creature_recall[monster.creature_id].defenses |= config::monsters::defense::CD_NO_SLEEP;
                }
                printMonsterActionText(name, "is unaffected.");
            }
        } else {
            monster.sleep_count = 500;
            if (monster.lit) {
                asleep = true;
                printMonsterActionText(name, "falls asleep.");
            }
        }
    }

    return asleep;
}

// Polymorph any creature that player can see. -RAK-
// NOTE: cannot polymorph a winning creature (BALROG)
bool spellMassPolymorph() {
    bool morphed = false;

    for (int id = next_free_monster_id - 1; id >= config::monsters::MON_MIN_INDEX_ID; id--) {
        Monster_t const &monster = monsters[id];

        if (monster.distance_from_player <= config::monsters::MON_MAX_SIGHT) {
            Creature_t const &creature = creatures_list[monster.creature_id];

            if ((creature.movement & config::monsters::move::CM_WIN) == 0) {
                int y = monster.y;
                int x = monster.x;
                dungeonDeleteMonster(id);

                // Place_monster() should always return true here.
                morphed = monsterPlaceNew(y, x, randomNumber(monster_levels[MON_MAX_LEVELS] - monster_levels[0]) - 1 + monster_levels[0], false);
            }
        }
    }

    return morphed;
}

// Display evil creatures on current panel -RAK-
bool spellDetectEvil() {
    bool detected = false;

    for (int id = next_free_monster_id - 1; id >= config::monsters::MON_MIN_INDEX_ID; id--) {
        Monster_t &monster = monsters[id];

        if (coordInsidePanel(Coord_t{monster.y, monster.x}) && ((creatures_list[monster.creature_id].defenses & config::monsters::defense::CD_EVIL) != 0)) {
            monster.lit = true;

            detected = true;

            // works correctly even if hallucinating
            panelPutTile((char) creatures_list[monster.creature_id].sprite, Coord_t{monster.y, monster.x});
        }
    }

    if (detected) {
        printMessage("You sense the presence of evil!");
        printMessage(CNIL);

        // must unlight every monster just lighted
        updateMonsters(false);
    }

    return detected;
}

// Change players hit points in some manner -RAK-
bool spellChangePlayerHitPoints(int adjustment) {
    if (py.misc.current_hp >= py.misc.max_hp) {
        return false;
    }

    py.misc.current_hp += adjustment;
    if (py.misc.current_hp > py.misc.max_hp) {
        py.misc.current_hp = py.misc.max_hp;
        py.misc.current_hp_fraction = 0;
    }
    printCharacterCurrentHitPoints();

    adjustment = adjustment / 5;

    if (adjustment < 3) {
        if (adjustment == 0) {
            printMessage("You feel a little better.");
        } else {
            printMessage("You feel better.");
        }
    } else {
        if (adjustment < 7) {
            printMessage("You feel much better.");
        } else {
            printMessage("You feel very good.");
        }
    }

    return true;
}

static void earthquakeHitsMonster(int monsterID) {
    Monster_t &monster = monsters[monsterID];
    Creature_t const &creature = creatures_list[monster.creature_id];

    if ((creature.movement & config::monsters::move::CM_PHASE) == 0u) {
        int damage;
        if ((creature.movement & config::monsters::move::CM_ATTACK_ONLY) != 0u) {
            // this will kill everything
            damage = 3000;
        } else {
            damage = diceRoll(Dice_t{4, 8});
        }

        auto name = monsterNameDescription(creature.name, monster.lit);

        printMonsterActionText(name, "wails out in pain!");

        if (monsterTakeHit(monsterID, damage) >= 0) {
            printMonsterActionText(name, "is embedded in the rock.");
            displayCharacterExperience();
        }
    } else if (creature.sprite == 'E' || creature.sprite == 'X') {
        // must be an earth elemental or an earth spirit, or a
        // Xorn increase its hit points
        monster.hp += diceRoll(Dice_t{4, 8});
    }
}

// This is a fun one.  In a given block, pick some walls and
// turn them into open spots.  Pick some open spots and dg.game_turn
// them into walls.  An "Earthquake" effect. -RAK-
void spellEarthquake() {
    for (int y = py.row - 8; y <= py.row + 8; y++) {
        for (int x = py.col - 8; x <= py.col + 8; x++) {
            if ((y != py.row || x != py.col) && coordInBounds(Coord_t{y, x}) && randomNumber(8) == 1) {
                Tile_t &tile = dg.floor[y][x];

                if (tile.treasure_id != 0) {
                    (void) dungeonDeleteObject(Coord_t{y, x});;
                }

                if (tile.creature_id > 1) {
                    earthquakeHitsMonster(tile.creature_id);
                }

                if (tile.feature_id >= MIN_CAVE_WALL && tile.feature_id != TILE_BOUNDARY_WALL) {
                    tile.feature_id = TILE_CORR_FLOOR;
                    tile.permanent_light = false;
                    tile.field_mark = false;
                } else if (tile.feature_id <= MAX_CAVE_FLOOR) {
                    int tmp = randomNumber(10);

                    if (tmp < 6) {
                        tile.feature_id = TILE_QUARTZ_WALL;
                    } else if (tmp < 9) {
                        tile.feature_id = TILE_MAGMA_WALL;
                    } else {
                        tile.feature_id = TILE_GRANITE_WALL;
                    }

                    tile.field_mark = false;
                }
                dungeonLiteSpot(Coord_t{y, x});
            }
        }
    }
}

// Create some high quality mush for the player. -RAK-
void spellCreateFood() {
    // Note: must take reference to this location as dungeonPlaceRandomObjectAt()
    // below, changes the tile values.
    Tile_t const &tile = dg.floor[py.row][py.col];

    // take no action here, don't want to destroy object under player
    if (tile.treasure_id != 0) {
        // set player_free_turn so that scroll/spell points won't be used
        game.player_free_turn = true;

        printMessage("There is already an object under you.");

        return;
    }

    dungeonPlaceRandomObjectAt(Coord_t{py.row, py.col}, false);
    inventoryItemCopyTo(config::dungeon::objects::OBJ_MUSH, treasure_list[tile.treasure_id]);
}

// Attempts to destroy a type of creature.  Success depends on
// the creatures level VS. the player's level -RAK-
bool spellDispelCreature(int creature_defense, int damage) {
    bool dispelled = false;

    for (int id = next_free_monster_id - 1; id >= config::monsters::MON_MIN_INDEX_ID; id--) {
        Monster_t const &monster = monsters[id];

        if (monster.distance_from_player <= config::monsters::MON_MAX_SIGHT && ((creature_defense & creatures_list[monster.creature_id].defenses) != 0) && los(py.row, py.col, monster.y, monster.x)) {
            Creature_t const &creature = creatures_list[monster.creature_id];

            creature_recall[monster.creature_id].defenses |= creature_defense;

            dispelled = true;

            auto name = monsterNameDescription(creature.name, monster.lit);

            int hit = monsterTakeHit(id, randomNumber(damage));

            // Should get these messages even if the monster is not visible.
            if (hit >= 0) {
                printMonsterActionText(name, "dissolves!");
            } else {
                printMonsterActionText(name, "shudders.");
            }

            if (hit >= 0) {
                displayCharacterExperience();
            }
        }
    }

    return dispelled;
}

// Attempt to turn (confuse) undead creatures. -RAK-
bool spellTurnUndead() {
    bool turned = false;

    for (int id = next_free_monster_id - 1; id >= config::monsters::MON_MIN_INDEX_ID; id--) {
        Monster_t &monster = monsters[id];
        Creature_t const &creature = creatures_list[monster.creature_id];

        if (monster.distance_from_player <= config::monsters::MON_MAX_SIGHT && ((creature.defenses & config::monsters::defense::CD_UNDEAD) != 0) && los(py.row, py.col, monster.y, monster.x)) {
            auto name = monsterNameDescription(creature.name, monster.lit);

            if (py.misc.level + 1 > creature.level || randomNumber(5) == 1) {
                if (monster.lit) {
                    creature_recall[monster.creature_id].defenses |= config::monsters::defense::CD_UNDEAD;

                    turned = true;

                    printMonsterActionText(name, "runs frantically!");
                }

                monster.confused_amount = (uint8_t) py.misc.level;
            } else if (monster.lit) {
                printMonsterActionText(name, "is unaffected.");
            }
        }
    }

    return turned;
}

// Leave a glyph of warding. Creatures will not pass over! -RAK-
void spellWardingGlyph() {
    if (dg.floor[py.row][py.col].treasure_id == 0) {
        int free_id = popt();
        dg.floor[py.row][py.col].treasure_id = (uint8_t) free_id;
        inventoryItemCopyTo(config::dungeon::objects::OBJ_SCARE_MON, treasure_list[free_id]);
    }
}

// Lose a strength point. -RAK-
void spellLoseSTR() {
    if (!py.flags.sustain_str) {
        (void) playerStatRandomDecrease(py_attrs::A_STR);
        printMessage("You feel very sick.");
    } else {
        printMessage("You feel sick for a moment,  it passes.");
    }
}

// Lose an intelligence point. -RAK-
void spellLoseINT() {
    if (!py.flags.sustain_int) {
        (void) playerStatRandomDecrease(py_attrs::A_INT);
        printMessage("You become very dizzy.");
    } else {
        printMessage("You become dizzy for a moment,  it passes.");
    }
}

// Lose a wisdom point. -RAK-
void spellLoseWIS() {
    if (!py.flags.sustain_wis) {
        (void) playerStatRandomDecrease(py_attrs::A_WIS);
        printMessage("You feel very naive.");
    } else {
        printMessage("You feel naive for a moment,  it passes.");
    }
}

// Lose a dexterity point. -RAK-
void spellLoseDEX() {
    if (!py.flags.sustain_dex) {
        (void) playerStatRandomDecrease(py_attrs::A_DEX);
        printMessage("You feel very sore.");
    } else {
        printMessage("You feel sore for a moment,  it passes.");
    }
}

// Lose a constitution point. -RAK-
void spellLoseCON() {
    if (!py.flags.sustain_con) {
        (void) playerStatRandomDecrease(py_attrs::A_CON);
        printMessage("You feel very sick.");
    } else {
        printMessage("You feel sick for a moment,  it passes.");
    }
}

// Lose a charisma point. -RAK-
void spellLoseCHR() {
    if (!py.flags.sustain_chr) {
        (void) playerStatRandomDecrease(py_attrs::A_CHR);
        printMessage("Your skin starts to itch.");
    } else {
        printMessage("Your skin starts to itch, but feels better now.");
    }
}

// Lose experience -RAK-
void spellLoseEXP(int32_t adjustment) {
    if (adjustment > py.misc.exp) {
        py.misc.exp = 0;
    } else {
        py.misc.exp -= adjustment;
    }
    displayCharacterExperience();

    int exp = 0;
    while ((signed) (py.base_exp_levels[exp] * py.misc.experience_factor / 100) <= py.misc.exp) {
        exp++;
    }

    // increment exp once more, because level 1 exp is stored in player_base_exp_levels[0]
    exp++;

    if (py.misc.level != exp) {
        py.misc.level = (uint16_t) exp;

        playerCalculateHitPoints();

        Class_t const &character_class = classes[py.misc.class_id];

        if (character_class.class_to_use_mage_spells == config::spells::SPELL_TYPE_MAGE) {
            playerCalculateAllowedSpellsCount(py_attrs::A_INT);
            playerGainMana(py_attrs::A_INT);
        } else if (character_class.class_to_use_mage_spells == config::spells::SPELL_TYPE_PRIEST) {
            playerCalculateAllowedSpellsCount(py_attrs::A_WIS);
            playerGainMana(py_attrs::A_WIS);
        }
        printCharacterLevel();
        printCharacterTitle();
    }
}

// Slow Poison -RAK-
bool spellSlowPoison() {
    if (py.flags.poisoned > 0) {
        py.flags.poisoned = (int16_t) (py.flags.poisoned / 2);
        if (py.flags.poisoned < 1) {
            py.flags.poisoned = 1;
        }
        printMessage("The effect of the poison has been reduced.");
        return true;
    }

    return false;
}

static void replaceSpot(int y, int x, int typ) {
    Tile_t &tile = dg.floor[y][x];

    switch (typ) {
        case 1:
        case 2:
        case 3:
            tile.feature_id = TILE_CORR_FLOOR;
            break;
        case 4:
        case 7:
        case 10:
            tile.feature_id = TILE_GRANITE_WALL;
            break;
        case 5:
        case 8:
        case 11:
            tile.feature_id = TILE_MAGMA_WALL;
            break;
        case 6:
        case 9:
        case 12:
            tile.feature_id = TILE_QUARTZ_WALL;
            break;
        default:
            break;
    }

    tile.permanent_light = false;
    tile.field_mark = false;
    tile.perma_lit_room = false; // this is no longer part of a room

    if (tile.treasure_id != 0) {
        (void) dungeonDeleteObject(Coord_t{y, x});;
    }

    if (tile.creature_id > 1) {
        dungeonDeleteMonster((int) tile.creature_id);
    }
}

// The spell of destruction. -RAK-
// NOTE:
//   Winning creatures that are deleted will be considered as teleporting to another level.
//   This will NOT win the game.
void spellDestroyArea(int y, int x) {
    if (dg.current_level > 0) {
        for (int pos_y = y - 15; pos_y <= y + 15; pos_y++) {
            for (int pos_x = x - 15; pos_x <= x + 15; pos_x++) {
                if (coordInBounds(Coord_t{pos_y, pos_x}) && dg.floor[pos_y][pos_x].feature_id != TILE_BOUNDARY_WALL) {
                    int distance = coordDistanceBetween(Coord_t{pos_y, pos_x}, Coord_t{y, x});

                    // clear player's spot, but don't put wall there
                    if (distance == 0) {
                        replaceSpot(pos_y, pos_x, 1);
                    } else if (distance < 13) {
                        replaceSpot(pos_y, pos_x, randomNumber(6));
                    } else if (distance < 16) {
                        replaceSpot(pos_y, pos_x, randomNumber(9));
                    }
                }
            }
        }
    }

    printMessage("There is a searing blast of light!");
    py.flags.blind += 10 + randomNumber(10);
}

// Enchants a plus onto an item. -RAK-
// `limit` param is the maximum bonus allowed; usually 10,
// but weapon's maximum damage when enchanting melee weapons to damage.
bool spellEnchantItem(int16_t &plusses, int16_t max_bonus_limit) {
    // avoid randomNumber(0) call
    if (max_bonus_limit <= 0) {
        return false;
    }

    int chance = 0;

    if (plusses > 0) {
        chance = plusses;

        // very rarely allow enchantment over limit
        if (randomNumber(100) == 1) {
            chance = randomNumber(chance) - 1;
        }
    }

    if (randomNumber(max_bonus_limit) > chance) {
        plusses += 1;
        return true;
    }

    return false;
}

// Removes curses from items in inventory -RAK-
bool spellRemoveCurseFromAllItems() {
    bool removed = false;

    for (int id = player_equipment::EQUIPMENT_WIELD; id <= player_equipment::EQUIPMENT_OUTER; id++) {
        if ((inventory[id].flags & config::treasure::flags::TR_CURSED) != 0u) {
            inventory[id].flags &= ~config::treasure::flags::TR_CURSED;
            playerRecalculateBonuses();
            removed = true;
        }
    }

    return removed;
}

// Restores any drained experience -RAK-
bool spellRestorePlayerLevels() {
    if (py.misc.max_exp > py.misc.exp) {
        printMessage("You feel your life energies returning.");

        // this while loop is not redundant, ptr_exp may reduce the exp level
        while (py.misc.exp < py.misc.max_exp) {
            py.misc.exp = py.misc.max_exp;
            displayCharacterExperience();
        }

        return true;
    }

    return false;
}
