#pragma once

#include "scene/Transform.h"

#include <string>

namespace rr::scene {

// Common metadata for every transformable entity in the scene
// (spheres, meshes, lights). Composed into the specific scene-side
// wrappers rather than inherited from - per the development rules,
// composition is preferred so the GPU-upload paths can read POD
// fields directly without any virtual-table indirection.
//
// Materials, render settings, and other "shared resources" do NOT
// embed a SceneObject; they are referenced by index and have no
// transform of their own.
struct SceneObject {
    std::string name;                                  // optional, for debugging / authoring
    Transform   transform = Transform::identity();
    bool        visible   = true;
};

}
