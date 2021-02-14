#ifndef VOXEL_INSTANCE_GENERATOR_H
#define VOXEL_INSTANCE_GENERATOR_H

//#include "../../storage/voxel_buffer.h"
#include "../../math/vector3i.h"
#include <core/resource.h>
#include <limits>
#include <vector>

// TODO This may have to be moved to the meshing thread some day

// Decides where to spawn instances on top of a voxel surface.
// Note: to generate voxels themselves, see `VoxelGenerator`.
class VoxelInstanceGenerator : public Resource {
	GDCLASS(VoxelInstanceGenerator, Resource)
public:
	// Tells how to interpret where "upwards" is in the current volume
	enum UpMode {
		// The world is a plane, so altitude is obtained from the Y coordinate and upwards is always toward +Y.
		UP_MODE_POSITIVE_Y,
		// The world is a sphere (planet), so altitude is obtained from distance to the origin (0,0,0),
		// and upwards is the normalized vector from origin to current position.
		UP_MODE_SPHERE,
		// How many up modes there are
		UP_MODE_COUNT
	};

	// TODO Option to generate from faces

	// This API might change so for now it's not exposed to scripts
	void generate_transforms(
			std::vector<Transform> &out_transforms,
			Vector3i grid_position,
			int lod_index,
			int layer_id,
			Array surface_arrays,
			const Transform &block_local_transform,
			UpMode up_mode);

	void set_density(float d);
	float get_density() const;

	void set_vertical_alignment(float valign);
	float get_vertical_alignment() const;

	void set_min_scale(float min_scale);
	float get_min_scale() const;

	void set_max_scale(float max_scale);
	float get_max_scale() const;

	// TODO Add scale curve, in real life there are way more small items than big ones

	void set_offset_along_normal(float offset);
	float get_offset_along_normal() const;

	void set_min_slope_degrees(float degrees);
	float get_min_slope_degrees() const;

	void set_max_slope_degrees(float degrees);
	float get_max_slope_degrees() const;

	void set_min_height(float h);
	float get_min_height() const;

	void set_max_height(float h);
	float get_max_height() const;

	void set_random_vertical_flip(bool flip);
	bool get_random_vertical_flip() const;

private:
	static void _bind_methods();

	float _density = 0.1f;
	float _vertical_alignment = 1.f;
	float _min_scale = 1.f;
	float _max_scale = 1.f;
	float _offset_along_normal = 0.f;
	float _min_surface_normal_y = -1.f;
	float _max_surface_normal_y = 1.f;
	float _min_height = std::numeric_limits<float>::min();
	float _max_height = std::numeric_limits<float>::max();
	bool _random_vertical_flip = false;

	// Stored separately for editor
	float _min_slope_degrees = -180.f;
	float _max_slope_degrees = 180.f;
};

#endif // VOXEL_INSTANCE_GENERATOR_H
