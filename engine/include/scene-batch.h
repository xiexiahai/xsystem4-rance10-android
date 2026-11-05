/* scene-batch.h */
#ifndef SCENE_BATCH_H
#define SCENE_BATCH_H

#include <stddef.h>

struct sprite;

struct scene_z2_update {
    struct sprite *sp;
    int z;
    int z2;
};

void scene_set_sprites_z2_batch(
    const struct scene_z2_update *updates, size_t count);

#endif
