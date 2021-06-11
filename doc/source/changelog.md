Changelog
============

This is a high-level list of features, changes and fixes that have been made over time.

At the moment, this module doesn't have a distinct release schedule, so this changelog follows Godot's version numbers and binary releases. Almost each version mentionned here should have an associated Git branch (for THIS repo, not Godot's) containing features at the time of the version. Backports aren't done so far.

Semver is not yet in place, so each version can have breaking changes, although it shouldn't happen often.

09/05/2021 - `godot3.3`
-----------------------

- General
    - Introduction of Voxel Server, which shares threaded tasks among all voxel nodes
    - Voxel data is no longer copied when sent to processing threads, reducing high memory spikes in some scenarios
    - Added a utility class to load `.vox` files created with MagicaVoxel (scripts only)
    - Voxel nodes can be moved, scaled and rotated
    - Voxel nodes can be limited to specific bounds, rather than being infinitely paging volumes (multiples of block size).
    - Meshers are now resources so you can choose and configure them per terrain
    - Added [FastNoiseLite](https://github.com/Auburn/FastNoise) for a wider variety of noises
    - Generators are no longer limited to a single background thread
    - Added `VoxelStreamSQLite`, allowing to save volumes as a single SQLite database
    - Implemented `copy` and `paste` for `VoxelToolTerrain`
    - Added ability to double block size used for meshes and instancing, improving rendering speed at the cost of slower modification
    - Added collision layer and mask properties

- Editor
    - Streaming/LOD can be set to follow the editor camera instead of being centered on world origin. Use with caution, fast big movements and zooms can cause lag
    - The amount of pending background tasks is now indicated when the node is selected
    - Added About window

- Smooth voxels
    - Shaders now have access to the transform of each block, useful for triplanar mapping on moving volumes
    - Transvoxel runs faster (almost x2 speedup)
    - The SDF channel is now 16-bit by default instead of 8-bit, which reduces terracing in big terrains
    - Optimized `VoxelGeneratorGraph` by making it detect empty blocks more accurately and process by buffers
    - `VoxelGeneratorGraph` now exposes performance tuning parameters
    - Added `SdfSphereHeightmap` and `Normalize` nodes to voxel graph, which can help making planets
    - Added `SdfSmoothUnion` and `SdfSmoothSubtract` nodes to voxel graph
    - Added `VoxelInstancer` to instantiate items on top of `VoxelLodTerrain`, aimed at spawning natural elements such as rocks and foliage
    - Implemented `VoxelToolLodterrain.raycast()`
    - Added experimental API for LOD fading, with some limitations.

- Blocky voxels
    - Introduced a second blocky mesher dedicated to colored cubes, with greedy meshing and palette support
    - Replaced `transparent` property with `transparency_index` for more control on the culling of transparent faces
    - The TYPE channel is now 16-bit by default instead of 8-bit, allowing to store up to 65,536 types (part of this channel might actually be used to store rotation in the future)
    - Added normalmaps support
    - `VoxelRaycastResult` now also contains hit distance, so it is possible to determine the exact hit position

- Breaking changes
    - `VoxelViewer` now replaces the `viewer_path` property on `VoxelTerrain`, and allows multiple loading points
    - Defined `COLOR` channel in `VoxelBuffer`, previously known as `DATA3`
    - `VoxelGenerator` is no longer the base for script-based generators, use `VoxelGeneratorScript` instead
    - `VoxelGenerator` no longer inherits `VoxelStream`
    - `VoxelStream` is no longer the base for script-based streams, use `VoxelStreamScript` instead
    - Generators and streams have been split. Streams are more dedicated to files and use a single background thread. Generators are dedicated to generation and can be used by more than one background thread. Terrains have one property for each.
    - The meshing system no longer "guesses" how voxels will look like. Instead it uses the mesher assigned to the terrain.
    - SDF and TYPE channels have different default depth, so if you relied on 8-bit depth, you may have to explicitely set that format in your generator, to avoid mismatch with existing savegames
    - The block serialization format has changed, and migration is not implemented, so old saves using it cannot be used. See documentation for more information.
    - Terrains no longer auto-save when they are destroyed while having a `stream` assigned. You have to call `save_modified_blocks()` explicitely before doing that.
    - `VoxelLodTerrain.lod_split_scale` has been replaced with `lod_distance` for clarity. It is the distance from the viewer where the first LOD level may extend. 

- Fixes
    - C# should be able to properly implement generator/stream functions

- Known issues
    - `VoxelLodTerrain` does not entirely support `VoxelViewer`, but a refactoring pass is planned for it.


08/09/2020 - `godot3.2.3`
--------------------------

- General
    - Added per-voxel and per-block metadata, which are saved by file streams along with voxel data
    - `StringName` is now used when possible to call script functions, to reduce overhead
    - Exposed block serializer to allow encoding voxels for network or files from script
    - Added terrain methods to trigger saves explicitely
    - The module only prints debug logs if the engine is in verbose mode
    - `VoxelTerrain` now emit signals when blocks are loaded and unloaded

- Editor
    - Terrain nodes now render in the editor, unless scripts are involved (can be changed with an option)

- Blocky voxels
    - Added collision masks to blocky voxels, which takes effect with `VoxelBoxMover` and voxel raycasts
    - Added random tick API for blocky terrains
    - Blocky mesh collision shapes now take all surfaces into account

- Smooth voxels
    - Added a graph-based generator, dedicated to SDF data for now

- Fixes
    - Fixed crash when `VoxelTerrain` is used without a stream
    - Fixed `Voxel.duplicate()` not implemented properly
    - Fixed collision shapes only being generated for the first mesh surface
    - Fixed `VoxelTool` truncating 64-bit values


27/03/2020 - (no branch) - Tokisan Games binary version
--------------------------------------------------------

- General
    - Optimized `VoxelBuffer` memory allocation with a pool (best when sizes are often the same)
    - Optimized block visibility by removing meshes from the world rather than setting the `visible` property
    - Added customizable bit depth to `VoxelBuffer` (8, 16, 32 and 64 bits)
    - Added node configuration warnings in case terrain nodes are wrongly setup
    - Implemented VoxelTool for VoxelBuffers
    - Added `VoxelStreamNoise2D`
    - Added blur option in `VoxelGeneratorImage`
    - Generators now all inherit `VoxelGenerator`

- Blocky voxels
    - Blocky voxels can be defined with a custom mesh, sides get culled automatically even when not a cube
    - Blocky voxels can have custom collision AABBs for use with `VoxelBoxMover`
    - The 3D noise generator now works in blocky mode with `VoxelTerrain`
    - Blocky voxels support 8 bit and 16 bit depths

- Smooth voxels
    - Implemented Transvoxel mesher, supporting transition meshes for seamless LOD. It now replaces DMC by default.
    - Heightmap generators now have an `iso_scale` tuning property which helps reducing terracing

- Fixes
    - Fixed dark normals occasionally appearing on smooth terrains
    - Fixed Transvoxel mesher being offset compared to other meshers
    - Fixed stuttering caused by `ShaderMaterial` creation, now they are pooled
    - Fixed cases where `VoxelLodTerrain` was getting stuck when teleporting far away
    - Fixed a few bugs with `VoxelStreamBlockFiles` and `VoxelStreamRegionFiles`

- Breaking changes
    - Removed channel enum from `Voxel`, it was redundant with `VoxelBuffer`
    - Renamed `VoxelBuffer.CHANNEL_ISOLEVEL` => `CHANNEL_SDF`
    - Removed `VoxelIsoSurfaceTool`, superseded by `VoxelTool`
    - Removed `VoxelMesherMC`, superseded by `VoxelMesherTransvoxel`
    - Replaced `VoxelGeneratorTest` by `VoxelGeneratorWaves` and `VoxelGeneratorFlat`


03/10/2019 - `godot3.1`
------------------------

Initial reference version.

- Support for blocky, dual marching cubes and simple marching cubes meshers
- Terrain with constant LOD paging, or variable LOD (smooth voxels only)
- Voxels editable in game (limited to LOD0)
- Various simple streams to generate, save and load voxel terrain data
- Meshers and storage primitives available to use by scripts

...

- 01/05/2016 - Creation of the module, using Godot 3.0 beta

