#ifndef VOX_2_VOXEL_H
#define VOX_2_VOXEL_H

#include "../meshers/cubes/voxel_color_palette.h"
#include "../util/math/vector3i.h"
#include <vector>

class VoxelBuffer;

namespace voxx {

struct Data {
	Vector3i size;
	std::vector<uint8_t> color_indexes;
	FixedArray<Color8, 256> palette;
	bool has_palette;
};

// TODO Eventually, will need specialized loaders, because data structures may vary and memory shouldn't be wasted
Error load_vox(const String &fpath, Data &data);

} // namespace vox

// Simple loader for MagicaVoxel
class Vox2Voxel : public Reference {
	GDCLASS(Vox2Voxel, Reference);

public:
	Error load_from_file(String fpath, Ref<VoxelBuffer> voxels);
	// TODO Have chunked loading for better memory usage
	// TODO Saving

private:
	static void _bind_methods();

	voxx::Data _data;
};

#endif // VOX_2_VOXEL_H
