// SPDX-License-Identifier: Apache-2.0
#include <stdio.h>
#include <string.h>
#include "private/pet_diary.h"

static const char *const TYPE_NAMES[PET_DIARY_TYPE_COUNT] = {
    [PET_DIARY_NONE] = "none", [PET_DIARY_BOOT] = "boot", [PET_DIARY_PAT] = "pat", [PET_DIARY_WALK] = "walk",
    [PET_DIARY_FEED] = "feed", [PET_DIARY_GAME] = "game", [PET_DIARY_ACT] = "act", [PET_DIARY_SCREEN] = "screen",
    [PET_DIARY_POWER] = "power", [PET_DIARY_GOAL] = "goal", [PET_DIARY_FED] = "fed", [PET_DIARY_DAY] = "day",
    [PET_DIARY_BUY] = "buy", [PET_DIARY_REVIVE] = "revive",
};

void pet_diary_init(pet_diary_t *d, pet_diary_store_t *store)
{
    memset(d, 0, sizeof(*d));
    d->store = store;
    if (store->magic != PET_DIARY_MAGIC) {
        memset(store, 0, sizeof(*store));
        store->magic = PET_DIARY_MAGIC;
    }
}

void pet_diary_add(pet_diary_t *d, uint32_t now_s, pet_diary_type_t type, uint8_t arg, uint32_t value)
{
    pet_diary_store_t *st = d->store;
    st->e[st->total % PET_DIARY_ENTRIES] = (pet_diary_entry_t) {
        .t = now_s, .type = (uint8_t)type, .arg = arg, .value = (uint16_t)(value > 65535U ? 65535U : value),
    };
    ++st->total;
    ++d->added;
}

static void flush_walk(pet_diary_t *d, uint32_t now_s, const app_game_snapshot_t *s)
{
    if (s->steps_total > d->walk_base) {
        pet_diary_add(d, now_s, PET_DIARY_WALK, 0, s->steps_total - d->walk_base);
    }
    d->walk_base = s->steps_total;
    d->walk_ticks = 0;
}

static void flush_pats(pet_diary_t *d, uint32_t now_s)
{
    if (d->pats) {
        pet_diary_add(d, now_s, PET_DIARY_PAT, 0, d->pats);
    }
    d->pats = 0;
    d->pat_ticks = 0;
}

void pet_diary_flush(pet_diary_t *d, uint32_t now_s, const app_game_snapshot_t *s)
{
    if (d->primed) {
        flush_walk(d, now_s, s);
        flush_pats(d, now_s);
    }
}

void pet_diary_observe(pet_diary_t *d, uint32_t now_s, uint32_t ev, const app_game_snapshot_t *s)
{
    if (!d->primed) {
        d->primed = true;
        d->walk_base = s->steps_total;
        d->act_ended_seq = s->act.ended_seq;
        d->cue_seq = s->cue_seq;
        d->power = s->power;
        d->act_end_tick = 0;
        return;
    }
    /* Acts: how each one ended, and pats that answer one. */
    if (s->act.ended_seq != d->act_ended_seq) {
        d->act_ended_seq = s->act.ended_seq;
        d->act_last_id = s->act.ended_id;
        d->act_end_tick = s->tick;
        pet_diary_add(d, now_s, PET_DIARY_ACT, s->act.ended_how, s->act.ended_id);
    }
    if (ev & APP_EVENT_PETTED) {
        const bool after_act = s->act.active
                               || (d->act_last_id && s->tick - d->act_end_tick < PET_DIARY_AFTER_ACT_TICKS);
        if (after_act) {
            pet_diary_add(d, now_s, PET_DIARY_PAT, 1, s->act.active ? s->act.id : d->act_last_id);
        } else {
            ++d->pats;
        }
    }
    if (++d->pat_ticks >= PET_DIARY_PAT_TICKS) {
        flush_pats(d, now_s);
    }
    if (++d->walk_ticks >= PET_DIARY_WALK_TICKS) {
        flush_walk(d, now_s, s);
    }
    if (ev & APP_EVENT_ATE && s->last_food >= 0) {
        pet_diary_add(d, now_s, PET_DIARY_FEED, (uint8_t)s->last_food, 0);
    }
    if (ev & APP_EVENT_GOAL) {
        pet_diary_add(d, now_s, PET_DIARY_GOAL, 0, s->steps_today / 10U);
    }
    if (ev & APP_EVENT_FED) {
        pet_diary_add(d, now_s, PET_DIARY_FED, 0, 0);
    }
    if (ev & APP_EVENT_REVIVED) {
        pet_diary_add(d, now_s, PET_DIARY_REVIVE, 0, 0);
    }
    if (ev & APP_EVENT_DAY) {
        pet_diary_add(d, now_s, PET_DIARY_DAY, s->hp, s->history[0] / 10U);
    }
    if (ev & APP_EVENT_BOUGHT) {
        pet_diary_add(d, now_s, PET_DIARY_BUY, 0, 0);
    }
    if (ev & APP_EVENT_SCREEN && s->power == APP_POWER_AWAKE) {
        pet_diary_add(d, now_s, PET_DIARY_SCREEN, (uint8_t)s->screen, 0);
    }
    /* Awake or asleep (dimming is not worth an entry). */
    const app_power_t power = s->power == APP_POWER_DOZE ? APP_POWER_DOZE : APP_POWER_AWAKE;
    if (power != d->power) {
        d->power = power;
        pet_diary_add(d, now_s, PET_DIARY_POWER, (uint8_t)power, 0);
    }
    /* Arcade results. */
    if (ev & APP_EVENT_SLOT_DONE) {
        pet_diary_add(d, now_s, PET_DIARY_GAME, APP_ARCADE_SLOTS, s->slots.payout);
    }
    if (ev & APP_EVENT_GRID_DONE) {
        pet_diary_add(d, now_s, PET_DIARY_GAME, APP_ARCADE_GRID, s->grid.payout);
    }
    if (ev & APP_EVENT_BIT_DONE) {
        pet_diary_add(d, now_s, PET_DIARY_GAME, APP_ARCADE_BITS, s->bits.payout);
    }
    if (ev & APP_EVENT_CASH_OUT) {
        pet_diary_add(d, now_s, PET_DIARY_GAME, APP_ARCADE_CARDS, 0);
    }
    if (s->cue_seq != d->cue_seq) {
        d->cue_seq = s->cue_seq;
        const bool result = s->cue == APP_CUE_WIN || s->cue == APP_CUE_COINS || s->cue == APP_CUE_LOSE;
        if (result && s->arcade == APP_ARCADE_SAYS) {
            pet_diary_add(d, now_s, PET_DIARY_GAME, APP_ARCADE_SAYS, s->cue == APP_CUE_LOSE ? 0 : s->says.payout);
        } else if (result && s->arcade == APP_ARCADE_STACK) {
            pet_diary_add(d, now_s, PET_DIARY_GAME, APP_ARCADE_STACK, s->cue == APP_CUE_LOSE ? 0 : s->stack.payout);
        }
    }
}

int pet_diary_json(const pet_diary_store_t *st, uint32_t since, char *out, size_t cap, uint32_t *next)
{
    const uint32_t oldest = st->total > PET_DIARY_ENTRIES ? st->total - PET_DIARY_ENTRIES : 0;
    uint32_t n = since < oldest ? oldest : since;
    size_t used = 0;
    while (n < st->total) {
        const pet_diary_entry_t *e = &st->e[n % PET_DIARY_ENTRIES];
        const char *name = e->type < PET_DIARY_TYPE_COUNT ? TYPE_NAMES[e->type] : "none";
        const int w = snprintf(out + used, cap - used, "%s{\"n\":%lu,\"t\":%lu,\"e\":\"%s\",\"a\":%u,\"v\":%u}",
                               used ? "," : "", (unsigned long)n, (unsigned long)e->t, name, e->arg, e->value);
        if (w < 0 || (size_t)w >= cap - used) {
            out[used] = '\0';   /* This one did not fit: it starts the next piece. */
            break;
        }
        used += (size_t)w;
        ++n;
    }
    if (next) {
        *next = n;
    }
    return (int)used;
}

uint32_t pet_diary_last_act(const pet_diary_store_t *st, uint16_t id)
{
    const uint32_t count = st->total > PET_DIARY_ENTRIES ? PET_DIARY_ENTRIES : st->total;
    for (uint32_t i = 0; i < count; ++i) {
        const pet_diary_entry_t *e = &st->e[(st->total - 1U - i) % PET_DIARY_ENTRIES];
        if (e->type == PET_DIARY_ACT && e->value == id) {
            return e->t;
        }
    }
    return 0;
}
