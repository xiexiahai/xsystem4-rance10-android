/* scene-batch.inc — include at end of production scene.c */
#ifndef SCENE_BATCH_INC
#define SCENE_BATCH_INC

#include "scene-batch.h"
#include <stdint.h>
#include <stdlib.h>

struct scene_batch_item {
    struct sprite *sp;
    int z, z2;
    size_t input;
    bool registered;
};

static int scene_batch_cmp(const void *pa, const void *pb)
{
    const struct scene_batch_item *a = pa, *b = pb;
    if (a->z < b->z) return -1;
    if (a->z > b->z) return 1;
    if (a->z2 < b->z2) return -1;
    if (a->z2 > b->z2) return 1;
    if (a->input < b->input) return -1;
    if (a->input > b->input) return 1;
    return 0;
}

static void scene_set_sprites_z2_batch_fallback(
        const struct scene_z2_update *updates, size_t count)
{
    for (size_t i = 0; i < count; i++)
        scene_set_sprite_z2(updates[i].sp, updates[i].z, updates[i].z2);
}

void scene_set_sprites_z2_batch(
        const struct scene_z2_update *updates, size_t count)
{
    if (!updates || count == 0)
        return;
    if (count > SIZE_MAX / sizeof(struct scene_batch_item)) {
        scene_set_sprites_z2_batch_fallback(updates, count);
        return;
    }
    struct scene_batch_item *items =
        malloc(count * sizeof(struct scene_batch_item));
    if (!items) {
        scene_set_sprites_z2_batch_fallback(updates, count);
        return;
    }

    for (size_t i = 0; i < count; i++) {
        struct sprite *sp = updates[i].sp;
        items[i].sp = sp;
        items[i].z = updates[i].z;
        items[i].z2 = updates[i].z2;
        items[i].input = i;
        items[i].registered = sp->in_scene;
    }

    for (size_t i = 0; i < count; i++) {
        struct sprite *sp = items[i].sp;
        sp->z = items[i].z;
        sp->z2 = items[i].z2;
        if (items[i].registered && sp->id == 0)
            sp->id = ++id_counter;
    }
    for (size_t i = 0; i < count; i++)
        if (items[i].registered)
            scene_unregister_sprite(items[i].sp);

    size_t nr_targets = 0;
    for (size_t i = 0; i < count; i++)
        if (items[i].registered)
            items[nr_targets++] = items[i];

    if (nr_targets == 0) {
        free(items);
        return;
    }

    qsort(items, nr_targets, sizeof(*items), scene_batch_cmp);

    struct sprite *non = TAILQ_FIRST(&sprite_list);
    TAILQ_INIT(&sprite_list);
    size_t ti = 0;
    while (non || ti < nr_targets) {
        if (ti >= nr_targets) {
            struct sprite *next = TAILQ_NEXT(non, entry);
            TAILQ_INSERT_TAIL(&sprite_list, non, entry);
            non = next;
        } else if (!non) {
            struct scene_batch_item *it = &items[ti++];
            it->sp->in_scene = true;
            TAILQ_INSERT_TAIL(&sprite_list, it->sp, entry);
        } else {
            struct scene_batch_item *it = &items[ti];
            if (non->z < it->z ||
                    (non->z == it->z && non->z2 <= it->z2)) {
                struct sprite *next = TAILQ_NEXT(non, entry);
                TAILQ_INSERT_TAIL(&sprite_list, non, entry);
                non = next;
            } else {
                it->sp->in_scene = true;
                TAILQ_INSERT_TAIL(&sprite_list, it->sp, entry);
                ti++;
            }
        }
    }
    free(items);
}

#endif
