#ifndef OPENDC_SCENE_LOADER_H
#define OPENDC_SCENE_LOADER_H

#include "scene_types.h"

int  scene_loader_mount(const void *blob, BootSceneState *scene);
void scene_loader_unmount(BootSceneState *scene);

#endif /* OPENDC_SCENE_LOADER_H */
