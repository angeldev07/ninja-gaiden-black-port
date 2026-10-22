/*
 * pushbuf.c: decodificador del push buffer (ver pushbuf.h).
 */
#include "pushbuf.h"

#include <string.h>

static void emit(const pb_callbacks_t *cb, void *user, int kind, uint32_t at, uint32_t word,
                 uint32_t target) {
    if (cb && cb->event)
        cb->event(user, kind, at, word, target);
}

uint32_t pb_run(pb_state_t *st, const uint8_t *base, uint32_t limit, uint32_t get, uint32_t put,
                uint32_t max_words, const pb_callbacks_t *cb, void *user) {
    uint32_t done = 0, ok_page = 0xFFFFFFFFu;
    while (get != put && done < max_words) {
        uint32_t word, at = get;
        st->cur = get;
        if (get > limit || limit - get < 4 || (get & 3)) {
            st->errors++;
            emit(cb, user, PB_EVENT_ERROR, get, 0, limit);
            return get;
        }
        if (cb && cb->readable && get >> 12 != ok_page) {
            if (!cb->readable(user, get)) {
                st->errors++;
                emit(cb, user, PB_EVENT_ERROR, get, 0, limit);
                return get;
            }
            ok_page = get >> 12;
        }
        if (st->count > 1 && st->non_increasing && cb && cb->bulk) {
            /* Racha de datos: hasta PUT (o el límite), el fin de la página y el tope de palabras. */
            uint32_t n = st->count, room = ((put > get && put < limit ? put : limit) - get) / 4;
            uint32_t page = (0x1000u - (get & 0xFFFu)) / 4;
            if (n > room)
                n = room;
            if (n > page)
                n = page;
            if (n > max_words - done)
                n = max_words - done;
            if (n > 1 && cb->bulk(user, st->subchannel, st->method, (const uint32_t *)(base + get), n)) {
                get += n * 4;
                done += n;
                st->words += n;
                st->count -= n;
                continue;
            }
        }
        memcpy(&word, base + get, 4);
        get += 4;
        done++;
        st->words++;
        if (st->count) {
            if (cb && cb->method)
                cb->method(user, st->subchannel, st->method, word, st->non_increasing);
            if (!st->non_increasing)
                st->method = (st->method + 4) & 0x1FFC;
            st->count--;
            continue;
        }
        if ((word & 0xE0000003u) == 0x20000000u) {
            get = word & 0x1FFFFFFFu;
            emit(cb, user, PB_EVENT_OLD_JUMP, at, word, get);
        } else if ((word & 3u) == 1u) {
            get = word & ~3u;
            emit(cb, user, PB_EVENT_JUMP, at, word, get);
        } else if ((word & 3u) == 2u) {
            if (st->in_subroutine) {
                st->errors++;
                emit(cb, user, PB_EVENT_ERROR, at, word, 0);
                return at;
            }
            st->in_subroutine = 1;
            st->return_get = get;
            get = word & ~3u;
            emit(cb, user, PB_EVENT_CALL, at, word, get);
        } else if (word == 0x00020000u) {
            if (!st->in_subroutine) {
                st->errors++;
                emit(cb, user, PB_EVENT_ERROR, at, word, 0);
                continue; /* como xemu: se registra y se sigue */
            }
            st->in_subroutine = 0;
            get = st->return_get;
            emit(cb, user, PB_EVENT_RETURN, at, word, get);
        } else if ((word & 0xE0030003u) == 0u || (word & 0xE0030003u) == 0x40000000u) {
            st->method = word & 0x1FFCu;
            st->subchannel = (word >> 13) & 7u;
            st->count = (word >> 18) & 0x7FFu;
            st->non_increasing = (word & 0x40000000u) != 0;
        } else {
            st->errors++;
            emit(cb, user, PB_EVENT_ERROR, at, word, 0);
        }
    }
    st->cur = get;
    return get;
}
