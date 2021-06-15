#ifndef VOX_2_VOXEL_H
#define VOX_2_VOXEL_H

#include "../util/math/vector3i.h"
#include "../util/math/color8.h"
#include "../util/fixed_array.h"
#include <vector>
#include <core/reference.h>




class VoxelBuffer;

class voxx : public Reference {
	GDCLASS(voxx, Reference);
	public:
		class Vox : public Reference {
			GDCLASS(Vox, Reference);
			public:
				int x;
				int y;
				int z;
				Color8 color;
		};
		class Data : public Reference {
			GDCLASS(Data, Reference);
			public:
				Vector3i size;
				std::vector<uint8_t> color_indexes;
				FixedArray<Color8, 256> palette;
				bool has_palette;
				
				std::vector<Vox*>output ;
			};

		Data* dados;

		// TODO Eventually, will need specialized loaders, because data structures may vary and memory shouldn't be wasted
		Error load_vox(const String &fpath, Data* voxx);

	private:
		static void _bind_methods();
}; // namespace vox

// Simple loader for MagicaVoxel
class Vox2Voxel : public Reference {
	GDCLASS(Vox2Voxel, Reference);

public:
	void load_from_file(String fpath);
	voxx* vox;	
	voxx* get_voxx();

	// TODO Have chunked loading for better memory usage
	// TODO Saving

private:
	static void _bind_methods();

};

#endif // VOX_2_VOXEL_H