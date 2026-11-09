#pragma once

#include <glm/glm.hpp>
#include <vector>

struct b3MeshData;

namespace viewer {
struct StaticCollisionFace;

// Restore source-scene adjacency lost when static geometry is split into resident
// meshes. Context includes all source triangles incident to this mesh's vertices.
// Call before attaching mesh to a body. Context/world positions are converted
// with the same origin used to build mesh; stored triangle order may differ.
void applyMeshSourceAdjacency(b3MeshData* mesh, const std::vector<StaticCollisionFace>& context,
                              glm::vec3 origin);
} // namespace viewer
