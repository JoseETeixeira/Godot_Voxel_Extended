#ifndef VOX_2_VOXEL_H
#define VOX_2_VOXEL_H

#include "../util/math/vector3i.h"
#include "../util/math/color8.h"
#include "../util/fixed_array.h"
#include <vector>
#include <core/reference.h>




class VoxelBuffer;

namespace voxx {
struct Vox : Dictionary{
		int x;
		int y;
		int z;
		Color8 color;
};
struct Data : Dictionary{
	Vector3i size;
	std::vector<uint8_t> color_indexes;
	FixedArray<Color8, 256> palette;
	bool has_palette;
	
	std::vector<Vox*>output ;

	
};

// TODO Eventually, will need specialized loaders, because data structures may vary and memory shouldn't be wasted
Error load_vox(const String &fpath, Data &data);

} // namespace vox

// Simple loader for MagicaVoxel
class Vox2Voxel : public Reference {
	GDCLASS(Vox2Voxel, Reference);

public:
	void load_from_file(String fpath);	

	Variant get_data(){
		return Variant(&_data);
	}
	// TODO Have chunked loading for better memory usage
	// TODO Saving

private:
	static void _bind_methods();

	voxx::Data _data;
};

#endif // VOX_2_VOXEL_H
