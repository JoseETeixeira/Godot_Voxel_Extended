#ifndef VOXEL_BOX_MOVER_H
#define VOXEL_BOX_MOVER_H

#include "voxel_terrain.h"
#include "../../core/math/a_star.h"
#include <core/math/aabb.h>
#include <scene/3d/spatial.h>

class MultiMesh;
// Helper to get simple AABB physics
class VoxelBoxMover : public Reference {
	GDCLASS(VoxelBoxMover, Reference)
public:
	Vector3 get_motion(Vector3 pos, Vector3 motion, AABB aabb, VoxelTerrain *terrain);
	Vector<Vector3> get_points_to_destination(Box3i box, VoxelTerrain *terrain);

	void set_collision_mask(uint32_t mask);
	inline uint32_t get_collision_mask() const { return _collision_mask; }

private:
	Vector3 _b_get_motion(Vector3 p_pos, Vector3 p_motion, AABB p_aabb);

	Vector<Vector3> _b_get_points_to_destination(Vector3 pos,Vector3 destination);

	void _b_set_terrain(Node *p_terrain_node);

	VoxelTerrain* terrain;


	static void _bind_methods();

	uint32_t _collision_mask = 0xffffffff; // Everything
};

#endif // VOXEL_BOX_MOVER_H
