#include "vox_2_voxel.h"
#include "../storage/voxel_buffer.h"
#include <core/os/file_access.h>
#include <core/dictionary.h>


Error voxx::load_vox(const String &fpath, voxx::Data* data,float scale) {
	const uint32_t PALETTE_SIZE = 256;

	Error err;
	FileAccessRef f = FileAccess::open(fpath, FileAccess::READ, &err);
	if (f == nullptr) {
		return err;
	}
	char magic[5] = { 0 };
	ERR_FAIL_COND_V(f->get_buffer((uint8_t *)magic, 4) != 4, ERR_PARSE_ERROR);
	ERR_FAIL_COND_V(strcmp(magic, "VOX ") != 0, ERR_PARSE_ERROR);

	const uint32_t version = f->get_32();
	ERR_FAIL_COND_V(version != 150, ERR_PARSE_ERROR);

	data->color_indexes.clear();
	data->size = Vector3i();
	data->has_palette = false;
	const size_t flen = f->get_len();

	while (f->get_position() < flen) {
		char chunk_id[5] = { 0 };
		ERR_FAIL_COND_V(f->get_buffer((uint8_t *)chunk_id, 4) != 4, ERR_PARSE_ERROR);
		const uint32_t chunk_size = f->get_32();
		f->get_32(); // child_chunks_size

		if (strcmp(chunk_id, "SIZE") == 0) {
			data->size.x = f->get_32();
			data->size.y = f->get_32();
			data->size.z = f->get_32();
			ERR_FAIL_COND_V(data->size.x > 0xff, ERR_PARSE_ERROR);
			ERR_FAIL_COND_V(data->size.y > 0xff, ERR_PARSE_ERROR);
			ERR_FAIL_COND_V(data->size.z > 0xff, ERR_PARSE_ERROR);

		} else if (strcmp(chunk_id, "XYZI") == 0) {
			data->color_indexes.resize(data->size.x * data->size.y * data->size.z, 0);
			const uint32_t num_voxels = f->get_32();

			for (uint32_t i = 0; i < num_voxels; ++i) {
				const uint32_t z = f->get_8();
				const uint32_t x = f->get_8();
				const uint32_t y = f->get_8();
				const uint32_t c = f->get_8();
				ERR_FAIL_COND_V(x >= static_cast<uint32_t>(data->size.x), ERR_PARSE_ERROR);
				ERR_FAIL_COND_V(y >= static_cast<uint32_t>(data->size.y), ERR_PARSE_ERROR);
				ERR_FAIL_COND_V(z >= static_cast<uint32_t>(data->size.z), ERR_PARSE_ERROR);
				data->color_indexes[Vector3i(x, y, z).get_zxy_index(data->size)] = c;
				voxx::Vox voxel;
				voxel.color_index = c;
				voxel.x = x*scale;
				voxel.y = y*scale;
				voxel.z = z*scale;
				data->output.push_back(voxel);
			}

		} else if (strcmp(chunk_id, "RGBA") == 0) {
			data->palette[0] = Color8{ 0, 0, 0, 0 };
			data->has_palette = true;
			for (uint32_t i = 1; i < data->palette.size(); ++i) {
				Color8 c;
				c.r = f->get_8();
				c.g = f->get_8();
				c.b = f->get_8();
				c.a = f->get_8();
				data->palette[i] = c;

			}
			f->get_32();

		} else {
			// Ignore chunk
			f->seek(f->get_position() + chunk_size);
		}


	}
	for(uint32_t i=0;i<data->output.size();i++){


		data->output[i].color = data->palette[data->output[i].color_index];
	}


	return OK;

}



void voxx::_bind_methods() {
	ClassDB::bind_method(D_METHOD("get_data","i"), &voxx::get_data);
	ClassDB::bind_method(D_METHOD("get_full_size"), &voxx::get_full_size);
}



void Vox2Voxel::load_from_file(String fpath,float scale) {

	vox.instance();
	vox->dados = memnew(voxx::Data());

	Error err = vox->load_vox(fpath,vox->dados,scale);

}

Ref<voxx> Vox2Voxel::get_voxx(){
	return vox;
}

void Vox2Voxel::_bind_methods() {
	ClassDB::bind_method(D_METHOD("load_from_file", "fpath","scale"), &Vox2Voxel::load_from_file);
	ClassDB::bind_method(D_METHOD("get_voxx"), &Vox2Voxel::get_voxx);
}
