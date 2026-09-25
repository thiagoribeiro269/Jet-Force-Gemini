#pragma once
#include "session.h"

namespace jfg_native {
inline TickInput regionScene(const NativeRegion &region, const AssetPackage &character, const JunoSelectionData &selection) {
    ActorInput input; input.selection.flag1FA = true;
    JunoAnimationController animation(selection, character.clips, character.skeleton.size());
    const auto floor = region.groundBelow(region.sourceSpawn[0], region.sourceSpawn[2], region.sourceSpawn[1]);
    if (!floor) throw std::runtime_error("No ground under the original entry point");
    const float minimum = poseMinimumY(character, composePose(character.skeleton, animation.animation().current()));
    SpawnSpec spawn{1, region.sourceSpawn[0], region.sourceSpawn[1], region.sourceSpawn[2], region.yaw, input,
                    region.playerScale, float(floor->height - region.sourceSpawn[1] - minimum * region.playerScale)};
    TickInput result; result.scene = SceneSpec{region.name, false, {spawn}, region.level}; return result;
}
inline WorldCamera regionCamera(const NativeRegion &region, double seconds) {
    if (!std::isfinite(seconds) || seconds < 0) throw std::runtime_error("Invalid region camera time");
    const double orbit = std::sin(seconds * 0.7) * 0.32;
    WorldCamera camera;
    camera.eye = {region.sourceSpawn[0] + float(std::sin(orbit) * 180), region.sourceSpawn[1] + 80,
                  region.sourceSpawn[2] + float(std::cos(orbit) * 180)};
    camera.target = {region.sourceSpawn[0], region.sourceSpawn[1] + 25, region.sourceSpawn[2] - 70};
    return camera;
}
}
