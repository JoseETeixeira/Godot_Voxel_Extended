#include "voxel_tool_lod_terrain.h"
#include "../terrain/voxel_data_map.h"
#include "../terrain/voxel_lod_terrain.h"
#include "../util/voxel_raycast.h"

VoxelToolLodTerrain::VoxelToolLodTerrain(VoxelLodTerrain *terrain, VoxelDataMap &map) :
		_terrain(terrain), _map(&map) {
	ERR_FAIL_COND(terrain == nullptr);
	// At the moment, only LOD0 is supported.
	// Don't destroy the terrain while a voxel tool still references it
}

bool VoxelToolLodTerrain::is_area_editable(const Rect3i &box) const {
	ERR_FAIL_COND_V(_terrain == nullptr, false);
	// TODO Take volume bounds into account
	return _map->is_area_fully_loaded(box.padded(1));
}

template <typename Volume_F>
float get_sdf_interpolated(Volume_F &f, Vector3 pos) {
	const Vector3i c = Vector3i::from_floored(pos);

	const float s000 = f(Vector3i(c.x, c.y, c.z));
	const float s100 = f(Vector3i(c.x + 1, c.y, c.z));
	const float s010 = f(Vector3i(c.x, c.y + 1, c.z));
	const float s110 = f(Vector3i(c.x + 1, c.y + 1, c.z));
	const float s001 = f(Vector3i(c.x, c.y, c.z + 1));
	const float s101 = f(Vector3i(c.x + 1, c.y, c.z + 1));
	const float s011 = f(Vector3i(c.x, c.y + 1, c.z + 1));
	const float s111 = f(Vector3i(c.x + 1, c.y + 1, c.z + 1));

	return interpolate(s000, s100, s101, s001, s010, s110, s111, s011, fract(pos));
}

// Binary search can be more accurate than linear regression because the SDF can be inaccurate in the first place.
// An alternative would be to polygonize a tiny area around the middle-phase hit position.
// `d1` is how far from `pos0` along `dir` the binary search will take place.
// The segment may be adjusted internally if it does not contain a zero-crossing of the
template <typename Volume_F>
float approximate_distance_to_isosurface_binary_search(
		Volume_F &f, Vector3 pos0, Vector3 dir, float d1, int iterations) {

	float d0 = 0.f;
	float sdf0 = get_sdf_interpolated(f, pos0);
	// The position given as argument may be a rough approximation coming from the middle-phase,
	// so it can be slightly below the surface. We can adjust it a little so it is above.
	for (int i = 0; i < 4 && sdf0 < 0.f; ++i) {
		d0 -= 0.5f;
		sdf0 = get_sdf_interpolated(f, pos0 + dir * d0);
	}

	float sdf1 = get_sdf_interpolated(f, pos0 + dir * d1);
	for (int i = 0; i < 4 && sdf1 > 0.f; ++i) {
		d1 += 0.5f;
		sdf1 = get_sdf_interpolated(f, pos0 + dir * d1);
	}

	if ((sdf0 > 0) != (sdf1 > 0)) {
		// Binary search
		for (int i = 0; i < iterations; ++i) {
			const float dm = 0.5f * (d0 + d1);
			const float sdf_mid = get_sdf_interpolated(f, pos0 + dir * dm);

			if ((sdf_mid > 0) != (sdf0 > 0)) {
				sdf1 = sdf_mid;
				d1 = dm;
			} else {
				sdf0 = sdf_mid;
				d0 = dm;
			}
		}
	}

	// Pick distance closest to the surface
	if (Math::abs(sdf0) < Math::abs(sdf1)) {
		return d0;
	} else {
		return d1;
	}
}

Ref<VoxelRaycastResult> VoxelToolLodTerrain::raycast(
		Vector3 pos, Vector3 dir, float max_distance, uint32_t collision_mask) {
	// TODO Transform input if the terrain is rotated
	// TODO Implement broad-phase on blocks to minimize locking and increase performance

	struct RaycastPredicate {
		const VoxelDataMap &map;

		bool operator()(Vector3i pos) {
			// This is not particularly optimized, but runs fast enough for player raycasts
			const float sdf = map.get_voxel_f(pos, VoxelBuffer::CHANNEL_SDF);
			return sdf < 0;
		}
	};

	Ref<VoxelRaycastResult> res;

	// We use grid-raycast as a middle-phase to roughly detect where the hit will be
	RaycastPredicate predicate = { *_map };
	Vector3i hit_pos;
	Vector3i prev_pos;
	float hit_distance;
	float hit_distance_prev;
	// Voxels polygonized using marching cubes influence a region centered on their lower corner,
	// and extend up to 0.5 units in all directions.
	//
	//   o--------o--------o
	//   | A      |     B  |  Here voxel B is full, voxels A, C and D are empty.
	//   |       xxx       |  Matter will show up at the lower corner of B due to interpolation.
	//   |     xxxxxxx     |
	//   o---xxxxxoxxxxx---o
	//   |     xxxxxxx     |
	//   |       xxx       |
	//   | C      |     D  |
	//   o--------o--------o
	//
	// `voxel_raycast` operates on a discrete grid of cubic voxels, so to account for the smooth interpolation,
	// we may offset the ray so that cubes act as if they were centered on the filtered result.
	const Vector3 offset(0.5, 0.5, 0.5);
	if (voxel_raycast(pos + offset, dir, predicate, max_distance, hit_pos, prev_pos, hit_distance, hit_distance_prev)) {
		// Approximate surface

		float d = hit_distance;

		if (_raycast_binary_search_iterations > 0) {
			// This is not particularly optimized, but runs fast enough for player raycasts
			struct VolumeSampler {
				const VoxelDataMap &map;

				inline float operator()(const Vector3i &pos) {
					return map.get_voxel_f(pos, VoxelBuffer::CHANNEL_SDF);
				}
			};

			VolumeSampler sampler{ *_map };
			d = hit_distance_prev + approximate_distance_to_isosurface_binary_search(sampler,
											pos + dir * hit_distance_prev,
											dir, hit_distance - hit_distance_prev,
											_raycast_binary_search_iterations);
		}

		res.instance();
		res->position = hit_pos;
		res->previous_position = prev_pos;
		res->distance_along_ray = d;
	}

	return res;
}

uint64_t VoxelToolLodTerrain::_get_voxel(Vector3i pos) const {
	ERR_FAIL_COND_V(_terrain == nullptr, 0);
	return _map->get_voxel(pos, _channel);
}

float VoxelToolLodTerrain::_get_voxel_f(Vector3i pos) const {
	ERR_FAIL_COND_V(_terrain == nullptr, 0);
	return _map->get_voxel_f(pos, _channel);
}

void VoxelToolLodTerrain::_set_voxel(Vector3i pos, uint64_t v) {
	ERR_FAIL_COND(_terrain == nullptr);
	_map->set_voxel(v, pos, _channel);
}

void VoxelToolLodTerrain::_set_voxel_f(Vector3i pos, float v) {
	ERR_FAIL_COND(_terrain == nullptr);
	_map->set_voxel_f(v, pos, _channel);
}

void VoxelToolLodTerrain::_post_edit(const Rect3i &box) {
	ERR_FAIL_COND(_terrain == nullptr);
	_terrain->post_edit_area(box);
}

int VoxelToolLodTerrain::get_raycast_binary_search_iterations() const {
	return _raycast_binary_search_iterations;
}

void VoxelToolLodTerrain::set_raycast_binary_search_iterations(int iterations) {
	_raycast_binary_search_iterations = clamp(iterations, 0, 16);
}

void VoxelToolLodTerrain::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_raycast_binary_search_iterations", "iterations"),
			&VoxelToolLodTerrain::set_raycast_binary_search_iterations);
	ClassDB::bind_method(D_METHOD("get_raycast_binary_search_iterations"),
			&VoxelToolLodTerrain::get_raycast_binary_search_iterations);
}
