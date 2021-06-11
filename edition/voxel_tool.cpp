#include "voxel_tool.h"
#include "../storage/voxel_buffer.h"
#include "../terrain/voxel_lod_terrain.h"
#include "../util/macros.h"
#include "../util/math/sdf.h"
#include "../util/profiling.h"

Vector3 VoxelRaycastResult::_b_get_position() const {
	return position.to_vec3();
}

Vector3 VoxelRaycastResult::_b_get_previous_position() const {
	return previous_position.to_vec3();
}

float VoxelRaycastResult::_b_get_distance() const {
	return distance_along_ray;
}

void VoxelRaycastResult::_bind_methods() {
	ClassDB::bind_method(D_METHOD("get_position"), &VoxelRaycastResult::_b_get_position);
	ClassDB::bind_method(D_METHOD("get_previous_position"), &VoxelRaycastResult::_b_get_previous_position);
	ClassDB::bind_method(D_METHOD("get_distance"), &VoxelRaycastResult::_b_get_distance);

	ADD_PROPERTY(PropertyInfo(Variant::VECTOR3, "position"), "", "get_position");
	ADD_PROPERTY(PropertyInfo(Variant::VECTOR3, "previous_position"), "", "get_previous_position");
	ADD_PROPERTY(PropertyInfo(Variant::REAL, "distance"), "", "get_distance");
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

VoxelTool::VoxelTool() {
	_sdf_scale = VoxelBuffer::get_sdf_quantization_scale(VoxelBuffer::DEFAULT_SDF_CHANNEL_DEPTH);
}

void VoxelTool::set_value(uint64_t val) {
	_value = val;
}

uint64_t VoxelTool::get_value() const {
	return _value;
}

void VoxelTool::set_eraser_value(uint64_t value) {
	_eraser_value = value;
}

uint64_t VoxelTool::get_eraser_value() const {
	return _eraser_value;
}

void VoxelTool::set_channel(int channel) {
	ERR_FAIL_INDEX(channel, VoxelBuffer::MAX_CHANNELS);
	_channel = channel;
}

int VoxelTool::get_channel() const {
	return _channel;
}

void VoxelTool::set_mode(Mode mode) {
	_mode = mode;
}

VoxelTool::Mode VoxelTool::get_mode() const {
	return _mode;
}

float VoxelTool::get_sdf_scale() const {
	return _sdf_scale;
}

void VoxelTool::set_sdf_scale(float s) {
	_sdf_scale = max(s, 0.00001f);
}

Ref<VoxelRaycastResult> VoxelTool::raycast(Vector3 pos, Vector3 dir, float max_distance, uint32_t collision_mask) {
	ERR_PRINT("Not implemented");
	return Ref<VoxelRaycastResult>();
	// See derived classes for implementations
}

uint64_t VoxelTool::get_voxel(Vector3i pos) const {
	return _get_voxel(pos);
}

float VoxelTool::get_voxel_f(Vector3i pos) const {
	return _get_voxel_f(pos);
}

void VoxelTool::set_voxel(Vector3i pos, uint64_t v) {
	Rect3i box(pos, Vector3i(1));
	if (!is_area_editable(box)) {
		PRINT_VERBOSE("Area not editable");
		return;
	}
	_set_voxel(pos, v);
	_post_edit(box);
}

void VoxelTool::set_voxel_f(Vector3i pos, float v) {
	Rect3i box(pos, Vector3i(1));
	if (!is_area_editable(box)) {
		PRINT_VERBOSE("Area not editable");
		return;
	}
	_set_voxel_f(pos, v);
	_post_edit(box);
}

void VoxelTool::do_point(Vector3i pos) {
	Rect3i box(pos, Vector3i(1));
	if (!is_area_editable(box)) {
		return;
	}
	if (_channel == VoxelBuffer::CHANNEL_SDF) {
		_set_voxel_f(pos, _mode == MODE_REMOVE ? 1.0 : -1.0);
	} else {
		_set_voxel(pos, _mode == MODE_REMOVE ? _eraser_value : _value);
	}
	_post_edit(box);
}

void VoxelTool::do_line(Vector3i begin, Vector3i end) {
	ERR_PRINT("Not implemented");
}

void VoxelTool::do_circle(Vector3i pos, int radius, Vector3i direction) {
	ERR_PRINT("Not implemented");
}

uint64_t VoxelTool::_get_voxel(Vector3i pos) const {
	ERR_PRINT("Not implemented");
	return 0;
}

float VoxelTool::_get_voxel_f(Vector3i pos) const {
	ERR_PRINT("Not implemented");
	return 0;
}

void VoxelTool::_set_voxel(Vector3i pos, uint64_t v) {
	ERR_PRINT("Not implemented");
}

void VoxelTool::_set_voxel_f(Vector3i pos, float v) {
	ERR_PRINT("Not implemented");
}

// TODO May be worth using VoxelBuffer::read_write_action() in the future with a lambda,
// so we avoid the burden of going through get/set, validation and rehash access to blocks.
// Would work well by avoiding virtual as well using a specialized implementation.

namespace {
inline float sdf_blend(float src_value, float dst_value, VoxelTool::Mode mode) {
	float res;
	switch (mode) {
		case VoxelTool::MODE_ADD:
			res = sdf_union(src_value, dst_value);
			break;

		case VoxelTool::MODE_REMOVE:
			// Relative complement (or difference)
			res = sdf_subtract(dst_value, src_value);
			break;

		case VoxelTool::MODE_SET:
			res = src_value;
			break;

		default:
			res = 0;
			break;
	}
	return res;
}
} // namespace

void VoxelTool::do_sphere(Vector3 center, float radius) {
	VOXEL_PROFILE_SCOPE();

	const Rect3i box(Vector3i(center) - Vector3i(Math::floor(radius)), Vector3i(Math::ceil(radius) * 2));

	if (!is_area_editable(box)) {
		PRINT_VERBOSE("Area not editable");
		return;
	}

	if (_channel == VoxelBuffer::CHANNEL_SDF) {
		box.for_each_cell([this, center, radius](Vector3i pos) {
			float d = _sdf_scale * (pos.to_vec3().distance_to(center) - radius);
			_set_voxel_f(pos, sdf_blend(d, get_voxel_f(pos), _mode));
		});

	} else {
		int value = _mode == MODE_REMOVE ? _eraser_value : _value;

		box.for_each_cell([this, center, radius, value](Vector3i pos) {
			float d = pos.to_vec3().distance_to(center);
			if (d <= radius) {
				_set_voxel(pos, value);
			}
		});
	}

	_post_edit(box);
}

void VoxelTool::do_box(Vector3i begin, Vector3i end) {
	VOXEL_PROFILE_SCOPE();
	Vector3i::sort_min_max(begin, end);
	Rect3i box = Rect3i::from_min_max(begin, end + Vector3i(1, 1, 1));

	if (!is_area_editable(box)) {
		PRINT_VERBOSE("Area not editable");
		return;
	}

	if (_channel == VoxelBuffer::CHANNEL_SDF) {
		// TODO Better quality
		box.for_each_cell([this](Vector3i pos) {
			_set_voxel_f(pos, sdf_blend(-1.0, get_voxel_f(pos), _mode));
		});

	} else {
		int value = _mode == MODE_REMOVE ? _eraser_value : _value;
		box.for_each_cell([this, value](Vector3i pos) {
			_set_voxel(pos, value);
		});
	}

	_post_edit(box);
}

void VoxelTool::copy(Vector3i pos, Ref<VoxelBuffer> dst, uint8_t channel_mask) {
	ERR_FAIL_COND(dst.is_null());
	ERR_PRINT("Not implemented");
}

void VoxelTool::paste(Vector3i p_pos, Ref<VoxelBuffer> p_voxels, uint8_t channels_mask, uint64_t mask_value) {
	ERR_FAIL_COND(p_voxels.is_null());
	ERR_PRINT("Not implemented");
}

bool VoxelTool::is_area_editable(const Rect3i &box) const {
	ERR_PRINT("Not implemented");
	return false;
}

void VoxelTool::_post_edit(const Rect3i &box) {
	ERR_PRINT("Not implemented");
}

void VoxelTool::set_voxel_metadata(Vector3i pos, Variant meta) {
	ERR_PRINT("Not implemented");
}

Variant VoxelTool::get_voxel_metadata(Vector3i pos) {
	ERR_PRINT("Not implemented");
	return Variant();
}

void VoxelTool::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_value", "v"), &VoxelTool::set_value);
	ClassDB::bind_method(D_METHOD("get_value"), &VoxelTool::get_value);

	ClassDB::bind_method(D_METHOD("set_channel", "v"), &VoxelTool::set_channel);
	ClassDB::bind_method(D_METHOD("get_channel"), &VoxelTool::get_channel);

	ClassDB::bind_method(D_METHOD("set_mode", "m"), &VoxelTool::set_mode);
	ClassDB::bind_method(D_METHOD("get_mode"), &VoxelTool::get_mode);

	ClassDB::bind_method(D_METHOD("set_eraser_value", "v"), &VoxelTool::set_eraser_value);
	ClassDB::bind_method(D_METHOD("get_eraser_value"), &VoxelTool::get_eraser_value);

	ClassDB::bind_method(D_METHOD("set_sdf_scale", "scale"), &VoxelTool::set_sdf_scale);
	ClassDB::bind_method(D_METHOD("get_sdf_scale"), &VoxelTool::get_sdf_scale);

	ClassDB::bind_method(D_METHOD("get_voxel", "pos"), &VoxelTool::_b_get_voxel);
	ClassDB::bind_method(D_METHOD("get_voxel_f", "pos"), &VoxelTool::_b_get_voxel_f);
	ClassDB::bind_method(D_METHOD("set_voxel", "pos", "v"), &VoxelTool::_b_set_voxel);
	ClassDB::bind_method(D_METHOD("set_voxel_f", "pos", "v"), &VoxelTool::_b_set_voxel_f);
	ClassDB::bind_method(D_METHOD("do_point", "pos"), &VoxelTool::_b_do_point);
	ClassDB::bind_method(D_METHOD("do_sphere", "center", "radius"), &VoxelTool::_b_do_sphere);
	ClassDB::bind_method(D_METHOD("do_box", "begin", "end"), &VoxelTool::_b_do_box);

	ClassDB::bind_method(D_METHOD("set_voxel_metadata", "pos", "meta"), &VoxelTool::_b_set_voxel_metadata);
	ClassDB::bind_method(D_METHOD("get_voxel_metadata", "pos"), &VoxelTool::_b_get_voxel_metadata);

	ClassDB::bind_method(D_METHOD("copy", "src_pos", "dst_buffer", "channels_mask"), &VoxelTool::_b_copy);
	ClassDB::bind_method(D_METHOD("paste", "dst_pos", "src_buffer", "channels_mask", "src_mask_value"),
			&VoxelTool::_b_paste);

	ClassDB::bind_method(D_METHOD("raycast", "origin", "direction", "max_distance", "collision_mask"),
			&VoxelTool::_b_raycast, DEFVAL(10.0), DEFVAL(0xffffffff));

	ClassDB::bind_method(D_METHOD("is_area_editable", "box"), &VoxelTool::_b_is_area_editable);

	ADD_PROPERTY(PropertyInfo(Variant::INT, "value"), "set_value", "get_value");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "channel", PROPERTY_HINT_ENUM, VoxelBuffer::CHANNEL_ID_HINT_STRING),
			"set_channel", "get_channel");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "eraser_value"), "set_eraser_value", "get_eraser_value");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "mode", PROPERTY_HINT_ENUM, "Add,Remove,Set"), "set_mode", "get_mode");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "sdf_scale"), "set_sdf_scale", "get_sdf_scale");

	BIND_ENUM_CONSTANT(MODE_ADD);
	BIND_ENUM_CONSTANT(MODE_REMOVE);
	BIND_ENUM_CONSTANT(MODE_SET);
}
