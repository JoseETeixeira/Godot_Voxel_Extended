#include "voxel_tool_lod_terrain.h"
#include "../constants/voxel_string_names.h"
#include "../terrain/voxel_lod_terrain.h"
#include "../util/funcs.h"
#include "../util/island_finder.h"
#include "../util/voxel_raycast.h"
#include "funcs.h"

#include <scene/3d/collision_shape.h>
#include <scene/3d/mesh_instance.h>
#include <scene/3d/physics_body.h>
#include <scene/main/timer.h>
#include <core/func_ref.h>

VoxelToolLodTerrain::VoxelToolLodTerrain(VoxelLodTerrain *terrain, VoxelDataMap &map) :
		_terrain(terrain), _map(&map) {
	ERR_FAIL_COND(terrain == nullptr);
	// At the moment, only LOD0 is supported.
	// Don't destroy the terrain while a voxel tool still references it
}

bool VoxelToolLodTerrain::is_area_editable(const Box3i &box) const {
	ERR_FAIL_COND_V(_terrain == nullptr, false);
	// TODO Take volume bounds into account
	return _map->is_area_fully_loaded(box);
}

template <typename Volume_F>
float get_sdf_interpolated(const Volume_F &f, Vector3 pos) {
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
		const Volume_F &f, Vector3 pos0, Vector3 dir, float d1, int iterations) {
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
	// TODO Implement reverse raycast? (going from inside ground to air, could be useful for undigging)

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

				inline float operator()(const Vector3i &pos) const {
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

void VoxelToolLodTerrain::do_sphere(Vector3 center, float radius) {
	ERR_FAIL_COND(_terrain == nullptr);

	if (_mode != MODE_TEXTURE_PAINT) {
		VoxelTool::do_sphere(center, radius);
		return;
	}

	VOXEL_PROFILE_SCOPE();

	const Box3i box(Vector3i(center) - Vector3i(Math::floor(radius)), Vector3i(Math::ceil(radius) * 2));

	if (!is_area_editable(box)) {
		PRINT_VERBOSE("Area not editable");
		return;
	}

	_map->write_box_2(box, VoxelBuffer::CHANNEL_INDICES, VoxelBuffer::CHANNEL_WEIGHTS,
			TextureBlendSphereOp{ center, radius, _texture_params });

	_post_edit(box);
}

void VoxelToolLodTerrain::copy(Vector3i pos, Ref<VoxelBuffer> dst, uint8_t channels_mask) {
	ERR_FAIL_COND(_terrain == nullptr);
	ERR_FAIL_COND(dst.is_null());
	if (channels_mask == 0) {
		channels_mask = (1 << _channel);
	}
	_map->copy(pos, **dst, channels_mask);
}

float VoxelToolLodTerrain::get_voxel_f_interpolated(Vector3 position) const {
	ERR_FAIL_COND_V(_terrain == nullptr, 0);
	const VoxelDataMap *map = _map;
	const int channel = get_channel();
	// TODO Optimization: is it worth a making a fast-path for this?
	return get_sdf_interpolated([map, channel](Vector3i ipos) {
		return map->get_voxel_f(ipos, channel);
	},
			position);
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

void VoxelToolLodTerrain::_post_edit(const Box3i &box) {
	ERR_FAIL_COND(_terrain == nullptr);
	_terrain->post_edit_area(box);
}

int VoxelToolLodTerrain::get_raycast_binary_search_iterations() const {
	return _raycast_binary_search_iterations;
}

void VoxelToolLodTerrain::set_raycast_binary_search_iterations(int iterations) {
	_raycast_binary_search_iterations = clamp(iterations, 0, 16);
}

// Turns floating chunks of voxels into rigidbodies:
// Detects separate groups of connected voxels within a box. Each group fully contained in the box is removed from
// the source volume, and turned into a rigidbody.
// This is one way of doing it, I don't know if it's the best way (there is rarely a best way)
// so there are probably other approaches that could be explored in the future, if they have better performance
static Array separate_floating_chunks(VoxelTool &voxel_tool, Box3i world_box, Node *parent_node, Transform transform,
		Ref<VoxelMesher> mesher, Array materials) {
	VOXEL_PROFILE_SCOPE();

	// Checks
	ERR_FAIL_COND_V(mesher.is_null(), Array());
	ERR_FAIL_COND_V(parent_node == nullptr, Array());

	// Copy source data

	// TODO Do not assume channel, at the moment it's hardcoded for smooth terrain
	static const int channels_mask = (1 << VoxelBuffer::CHANNEL_SDF);
	static const int main_channel = VoxelBuffer::CHANNEL_SDF;

	Ref<VoxelBuffer> source_copy_buffer;
	{
		VOXEL_PROFILE_SCOPE_NAMED("Copy");
		source_copy_buffer.instance();
		source_copy_buffer->create(world_box.size);
		voxel_tool.copy(world_box.pos, source_copy_buffer, channels_mask);
	}

	// Label distinct voxel groups

	static thread_local std::vector<uint8_t> ccl_output;
	ccl_output.resize(world_box.size.volume());

	unsigned int label_count = 0;

	{
		VOXEL_PROFILE_SCOPE_NAMED("CCL scan");
		IslandFinder island_finder;
		island_finder.scan_3d(
				Box3i(Vector3i(), world_box.size), [&source_copy_buffer](Vector3i pos) {
					// TODO Can be optimized further with direct access
					return source_copy_buffer->get_voxel_f(pos.x, pos.y, pos.z, main_channel) < 0.f;
				},
				to_span(ccl_output), &label_count);
	}

	struct Bounds {
		Vector3i min_pos;
		Vector3i max_pos; // inclusive
		bool valid = false;
	};

	// Compute bounds of each group

	std::vector<Bounds> bounds_per_label;
	{
		VOXEL_PROFILE_SCOPE_NAMED("Bounds calculation");

		// Adding 1 because label 0 is the index for "no label"
		bounds_per_label.resize(label_count + 1);

		unsigned int ccl_index = 0;
		for (int z = 0; z < world_box.size.z; ++z) {
			for (int x = 0; x < world_box.size.x; ++x) {
				for (int y = 0; y < world_box.size.y; ++y) {
					CRASH_COND(ccl_index >= ccl_output.size());
					const uint8_t label = ccl_output[ccl_index];
					++ccl_index;

					if (label == 0) {
						continue;
					}

					CRASH_COND(label >= bounds_per_label.size());
					Bounds &bounds = bounds_per_label[label];

					if (bounds.valid == false) {
						bounds.min_pos = Vector3i(x, y, z);
						bounds.max_pos = bounds.min_pos;
						bounds.valid = true;

					} else {
						if (x < bounds.min_pos.x) {
							bounds.min_pos.x = x;
						} else if (x > bounds.max_pos.x) {
							bounds.max_pos.x = x;
						}

						if (y < bounds.min_pos.y) {
							bounds.min_pos.y = y;
						} else if (y > bounds.max_pos.y) {
							bounds.max_pos.y = y;
						}

						if (z < bounds.min_pos.z) {
							bounds.min_pos.z = z;
						} else if (z > bounds.max_pos.z) {
							bounds.max_pos.z = z;
						}
					}
				}
			}
		}
	}

	// Eliminate groups that touch the box border,
	// because that means we can't tell if they are truly hanging in the air or attached to land further away

	const Vector3i lbmax = world_box.size - Vector3i(1);
	for (unsigned int label = 1; label < bounds_per_label.size(); ++label) {
		CRASH_COND(label >= bounds_per_label.size());
		Bounds &local_bounds = bounds_per_label[label];
		ERR_CONTINUE(!local_bounds.valid);

		if (
				local_bounds.min_pos.x == 0 ||
				local_bounds.min_pos.y == 0 ||
				local_bounds.min_pos.z == 0 ||
				local_bounds.max_pos.x == lbmax.x ||
				local_bounds.max_pos.y == lbmax.y ||
				local_bounds.max_pos.z == lbmax.z) {
			//
			local_bounds.valid = false;
		}
	}

	// Create voxel buffer for each group

	struct InstanceInfo {
		Ref<VoxelBuffer> voxels;
		Vector3i world_pos;
		unsigned int label;
	};
	std::vector<InstanceInfo> instances_info;

	const int min_padding = 2; //mesher->get_minimum_padding();
	const int max_padding = 2; //mesher->get_maximum_padding();

	{
		VOXEL_PROFILE_SCOPE_NAMED("Extraction");

		for (unsigned int label = 1; label < bounds_per_label.size(); ++label) {
			CRASH_COND(label >= bounds_per_label.size());
			const Bounds local_bounds = bounds_per_label[label];

			if (!local_bounds.valid) {
				continue;
			}

			const Vector3i world_pos = world_box.pos + local_bounds.min_pos - Vector3i(min_padding);
			const Vector3i size = local_bounds.max_pos - local_bounds.min_pos + Vector3i(1 + max_padding + min_padding);

			Ref<VoxelBuffer> buffer;
			buffer.instance();
			buffer->create(size);

			// Read voxels from the source volume
			voxel_tool.copy(world_pos, buffer, channels_mask);

			// Cleanup padding borders
			const Box3i inner_box(Vector3i(min_padding), buffer->get_size() - Vector3i(min_padding + max_padding));
			Box3i(Vector3i(), buffer->get_size())
					.difference(inner_box, [&buffer](Box3i box) {
						buffer->fill_area_f(1.f, box.pos, box.pos + box.size, main_channel);
					});

			// Filter out voxels that don't belong to this label
			for (int z = local_bounds.min_pos.z; z <= local_bounds.max_pos.z; ++z) {
				for (int x = local_bounds.min_pos.x; x <= local_bounds.max_pos.x; ++x) {
					for (int y = local_bounds.min_pos.y; y <= local_bounds.max_pos.y; ++y) {
						const unsigned int ccl_index = Vector3i(x, y, z).get_zxy_index(world_box.size);
						CRASH_COND(ccl_index >= ccl_output.size());
						const uint8_t label2 = ccl_output[ccl_index];

						if (label2 != 0 && label != label2) {
							buffer->set_voxel_f(1.f,
									min_padding + x - local_bounds.min_pos.x,
									min_padding + y - local_bounds.min_pos.y,
									min_padding + z - local_bounds.min_pos.z, main_channel);
						}
					}
				}
			}

			instances_info.push_back(InstanceInfo{ buffer, world_pos, label });
		}
	}

	// Erase voxels from source volume.
	// Must be done after we copied voxels from it.

	{
		VOXEL_PROFILE_SCOPE_NAMED("Erasing");

		voxel_tool.set_channel(main_channel);

		for (unsigned int instance_index = 0; instance_index < instances_info.size(); ++instance_index) {
			CRASH_COND(instance_index >= instances_info.size());
			const InstanceInfo info = instances_info[instance_index];
			ERR_CONTINUE(info.voxels.is_null());

			voxel_tool.sdf_stamp_erase(info.voxels, info.world_pos);
		}
	}

	// Create instances

	Array nodes;

	{
		VOXEL_PROFILE_SCOPE_NAMED("Remeshing and instancing");

		for (unsigned int instance_index = 0; instance_index < instances_info.size(); ++instance_index) {
			CRASH_COND(instance_index >= instances_info.size());
			const InstanceInfo info = instances_info[instance_index];
			ERR_CONTINUE(info.voxels.is_null());

			CRASH_COND(info.label >= bounds_per_label.size());
			const Bounds local_bounds = bounds_per_label[info.label];
			ERR_CONTINUE(!local_bounds.valid);

			// DEBUG
			// print_line(String("--- Instance {0}").format(varray(instance_index)));
			// for (int z = 0; z < info.voxels->get_size().z; ++z) {
			// 	for (int x = 0; x < info.voxels->get_size().x; ++x) {
			// 		String s;
			// 		for (int y = 0; y < info.voxels->get_size().y; ++y) {
			// 			float sdf = info.voxels->get_voxel_f(x, y, z, VoxelBuffer::CHANNEL_SDF);
			// 			if (sdf < -0.1f) {
			// 				s += "X ";
			// 			} else if (sdf < 0.f) {
			// 				s += "x ";
			// 			} else {
			// 				s += "- ";
			// 			}
			// 		}
			// 		print_line(s);
			// 	}
			// 	print_line("//");
			// }

			const Transform local_transform(Basis(), info.world_pos.to_vec3());

			for (int i = 0; i < materials.size(); ++i) {
				Ref<ShaderMaterial> sm = materials[i];
				if (sm.is_valid() &&
						sm->get_shader().is_valid() &&
						sm->get_shader()->has_param(VoxelStringNames::get_singleton()->u_block_local_transform)) {
					// That parameter should have a valid default value matching the local transform relative to the volume,
					// which is usually per-instance, but in Godot 3 we have no such feature, so we have to duplicate.
					sm = sm->duplicate(false);
					sm->set_shader_param(VoxelStringNames::get_singleton()->u_block_local_transform, local_transform);
					materials[i] = sm;
				}
			}

			Ref<Mesh> mesh = mesher->build_mesh(info.voxels, materials);
			// The mesh is not supposed to be null,
			// because we build these buffers from connected groups that had negative SDF.
			ERR_CONTINUE(mesh.is_null());

			// DEBUG
			// {
			// 	Ref<VoxelBlockSerializer> serializer;
			// 	serializer.instance();
			// 	Ref<StreamPeerBuffer> peer;
			// 	peer.instance();
			// 	serializer->serialize(peer, info.voxels, false);
			// 	String fpath = String("debug_data/split_dump_{0}.bin").format(varray(instance_index));
			// 	FileAccess *f = FileAccess::open(fpath, FileAccess::WRITE);
			// 	PoolByteArray bytes = peer->get_data_array();
			// 	PoolByteArray::Read bytes_read = bytes.read();
			// 	f->store_buffer(bytes_read.ptr(), bytes.size());
			// 	f->close();
			// 	memdelete(f);
			// }

			// TODO Option to make multiple convex shapes
			// TODO Use the fast way. This is slow because of the internal TriangleMesh thing.
			Ref<Shape> shape = mesh->create_convex_shape();
			ERR_CONTINUE(shape.is_null());
			CollisionShape *collision_shape = memnew(CollisionShape);
			collision_shape->set_shape(shape);
			// Center the shape somewhat, because Godot is confusing node origin with center of mass
			const Vector3i size = local_bounds.max_pos - local_bounds.min_pos + Vector3i(1 + max_padding + min_padding);
			const Vector3 offset = -size.to_vec3() * 0.5f;
			collision_shape->set_translation(offset);

			RigidBody *rigid_body = memnew(RigidBody);
			rigid_body->set_transform(transform * local_transform.translated(-offset));
			rigid_body->add_child(collision_shape);
			rigid_body->set_mode(RigidBody::MODE_KINEMATIC);

			// Switch to rigid after a short time to workaround clipping with terrain,
			// because colliders are updated asynchronously
			Timer *timer = memnew(Timer);
			timer->set_wait_time(0.2);
			timer->set_one_shot(true);
			timer->connect("timeout", rigid_body, "set_mode", varray(RigidBody::MODE_RIGID));
			// Cannot use start() here because it requires to be inside the SceneTree,
			// and we don't know if it will be after we add to the parent.
			timer->set_autostart(true);
			rigid_body->add_child(timer);

			MeshInstance *mesh_instance = memnew(MeshInstance);
			mesh_instance->set_mesh(mesh);
			mesh_instance->set_translation(offset);
			rigid_body->add_child(mesh_instance);

			parent_node->add_child(rigid_body);

			nodes.append(rigid_body);
		}
	}

	return nodes;
}

Array VoxelToolLodTerrain::separate_floating_chunks(AABB world_box, Node *parent_node) {
	ERR_FAIL_COND_V(_terrain == nullptr, Array());
	Ref<VoxelMesher> mesher = _terrain->get_mesher();
	Array materials;
	materials.append(_terrain->get_material());
	const Box3i int_world_box(world_box.position.floor(), world_box.size.ceil());
	return ::separate_floating_chunks(
			*this, int_world_box, parent_node, _terrain->get_global_transform(), mesher, materials);
}

void VoxelToolLodTerrain::for_each_voxel_metadata_in_area(AABB voxel_area, Ref<FuncRef> callback,int lod_index) {
	ERR_FAIL_COND(_terrain == nullptr);
	ERR_FAIL_COND(callback.is_null());

	const Box3i voxel_box = Box3i(Vector3i(voxel_area.position), Vector3i(voxel_area.size));
	ERR_FAIL_COND(!is_area_editable(voxel_box));

	const Box3i data_block_box = voxel_box.downscaled(_terrain->get_data_block_size());

	const VoxelDataMap &map = _terrain->get_data_map(lod_index);

	data_block_box.for_each_cell([&map, &callback, voxel_box](Vector3i block_pos) {
		const VoxelDataBlock *block = map.get_block(block_pos);

		if (block == nullptr) {
			return;
		}

		ERR_FAIL_COND(block->voxels.is_null());
		const Vector3i block_origin = block_pos * map.get_block_size();
		const Box3i rel_voxel_box(voxel_box.pos - block_origin, voxel_box.size);

		block->voxels->for_each_voxel_metadata_in_area(rel_voxel_box, [&callback, block_origin](Vector3i rel_pos, Variant meta) {
			const Variant key = (rel_pos + block_origin).to_vec3();
			const Variant *args[2] = { &key, &meta };
			Variant::CallError err;
			callback->call_func(args, 2, err);

			ERR_FAIL_COND_MSG(err.error != Variant::CallError::CALL_OK,
					String("FuncRef call failed at {0}").format(varray(key)));

			// TODO Can't provide detailed error because FuncRef doesn't give us access to the object
			// ERR_FAIL_COND_MSG(err.error != Variant::CallError::CALL_OK, false,
			// 		Variant::get_call_error_text(callback->get_object(), method_name, nullptr, 0, err));
		});
	});
}


void VoxelToolLodTerrain::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_raycast_binary_search_iterations", "iterations"),
			&VoxelToolLodTerrain::set_raycast_binary_search_iterations);
	ClassDB::bind_method(D_METHOD("get_raycast_binary_search_iterations"),
			&VoxelToolLodTerrain::get_raycast_binary_search_iterations);
	ClassDB::bind_method(D_METHOD("get_voxel_f_interpolated", "position"),
			&VoxelToolLodTerrain::get_voxel_f_interpolated);
	ClassDB::bind_method(D_METHOD("separate_floating_chunks", "box", "parent_node"),
			&VoxelToolLodTerrain::separate_floating_chunks);
	ClassDB::bind_method(D_METHOD("for_each_voxel_metadata_in_area", "voxel_area", "callback","lod_index"),
			&VoxelToolLodTerrain::for_each_voxel_metadata_in_area);
}
