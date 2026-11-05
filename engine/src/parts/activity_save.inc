/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Included by activity.c, using the same owned metadata as the live registry. */
struct parts_activity_snapshot { struct activity *head; };

static bool activity_saved_part(int number, bool save_hidden)
{
    struct parts *p = parts_try_get(number);
    return p && p->want_save && (save_hidden ||
        (p->global.show && PE_controller_visible(p->controller_no)));
}

void parts_activity_save(struct iarray_writer *w, bool save_hidden)
{
    unsigned count_pos = iarray_writer_pos(w);
    int count = 0;
    iarray_write(w, 0);
    for (struct activity *a = activities; a; a = a->next) {
        count++;
        iarray_write_string(w, a->name);
        unsigned parts_pos = iarray_writer_pos(w);
        int parts_count = 0;
        iarray_write(w, 0);
        for (int i = 0; i < a->nr_parts; i++) {
            struct activity_part *p = &a->parts[i];
            if (!activity_saved_part(p->number, save_hidden)) continue;
            parts_count++;
            iarray_write_string(w, p->name);
            iarray_write(w, p->number);
            iarray_write(w, p->closes);
            iarray_write(w, p->has_intent);
            iarray_write(w, p->intent_type);
            iarray_write(w, p->nr_destinations);
            for (int j = 0; j < p->nr_destinations; j++)
                iarray_write_string(w, p->destinations[j]);
        }
        iarray_write_at(w, parts_pos, parts_count);
        iarray_write(w, a->nr_end_keys);
        for (int i = 0; i < a->nr_end_keys; i++) iarray_write(w, a->end_keys[i]);
    }
    iarray_write_at(w, count_pos, count);
}

void parts_activity_snapshot_free(struct parts_activity_snapshot *s)
{
    if (!s) return;
    while (s->head) {
        struct activity *a = s->head;
        s->head = a->next;
        free_activity(a);
    }
    free(s);
}

static int activity_read_count(struct iarray_reader *r, int maximum)
{
    int count = iarray_read(r);
    if (r->error || count < 0 || count > maximum ||
        (unsigned)count > r->size-r->pos) r->error = 1;
    return r->error ? 0 : count;
}

static struct string *activity_read_name(struct iarray_reader *r)
{
    char bytes[4096];
    size_t length = 0;
    while (!r->error) {
        int c = iarray_read(r);
        if (!c) {
            if (!length) r->error = 1;
            return make_string(bytes, length);
        }
        /* iarray strings store a packed two-byte character in one cell.
         * Keep the bound in decoded bytes, with the same lead-byte rule as
         * iarray_write_string/string_push_back. Signed single-byte cells are
         * valid on platforms where char is signed. */
        int width = SJIS_2BYTE(c) ? 2 : 1;
        if (c < -128 || c > 65535 ||
            (width == 1 && c > 255) || (width == 2 && c < 256) ||
            (size_t)width > sizeof(bytes) - length) {
            r->error = 1;
            break;
        }
        bytes[length++] = c & 0xff;
        if (width == 2) bytes[length++] = (unsigned)c >> 8;
    }
    return make_string("", 0);
}

bool parts_activity_read(struct iarray_reader *r, struct parts_activity_snapshot **out)
{
    *out = NULL;
    struct parts_activity_snapshot *s = xcalloc(1, sizeof(*s));
    struct activity **next = &s->head;
    int count = activity_read_count(r, 4096), total_parts = 0;
    for (int i = 0; i < count && !r->error; i++) {
        struct activity *a = xcalloc(1, sizeof(*a));
        *next = a; next = &a->next;
        a->name = activity_read_name(r);
        for (struct activity *old = s->head; old != a; old = old->next)
            if (!strcmp(old->name->text, a->name->text)) r->error = 1;
        int parts_count = activity_read_count(r, 65536-total_parts);
        total_parts += parts_count;
        a->parts = xcalloc(parts_count, sizeof(*a->parts));
        for (int j = 0; j < parts_count && !r->error; j++) {
            struct activity_part *p = &a->parts[a->nr_parts++];
            p->name = activity_read_name(r);
            p->number = iarray_read(r);
            int closes = iarray_read(r), intent = iarray_read(r);
            if (p->number <= 0 || closes < 0 || closes > 1 || intent < 0 || intent > 1)
                r->error = 1;
            p->closes = closes; p->has_intent = intent;
            p->intent_type = iarray_read(r);
            for (int k = 0; k < j; k++)
                if (a->parts[k].number == p->number || !strcmp(a->parts[k].name->text, p->name->text))
                    r->error = 1;
            int destinations = activity_read_count(r, 4096);
            if (!intent && destinations) r->error = 1;
            p->destinations = xcalloc(destinations, sizeof(*p->destinations));
            for (int k = 0; k < destinations && !r->error; k++)
                p->destinations[p->nr_destinations++] = activity_read_name(r);
        }
        int keys = activity_read_count(r, 4096);
        a->end_keys = xcalloc(keys, sizeof(*a->end_keys));
        for (int j = 0; j < keys && !r->error; j++) {
            int key = iarray_read(r);
            if (key < 0) r->error = 1;
            for (int k = 0; k < j; k++) if (a->end_keys[k] == key) r->error = 1;
            a->end_keys[a->nr_end_keys++] = key;
        }
    }
    if (r->error) { parts_activity_snapshot_free(s); return false; }
    *out = s;
    return true;
}

bool parts_activity_install(struct parts_activity_snapshot *s)
{
    if (!s) return false;
    /* Validate all weak references before replacing any live names. */
    for (struct activity *a = s->head; a; a = a->next)
        for (int i = 0; i < a->nr_parts; i++)
            if (!PE_IsExist(a->parts[i].number)) return false;
    PE_ActivityReset();
    activities = s->head;
    s->head = NULL;
    return true;
}
