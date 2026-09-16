# Microsoft MiniEngine bitonic sort

The seven shader files in this directory are based on copies from
[Microsoft DirectX-Graphics-Samples / MiniEngine](https://github.com/microsoft/DirectX-Graphics-Samples/tree/213dd4fd4918ea009dd8f35adee1aff1f2ecaba4/MiniEngine/Core/Shaders),
commit `213dd4fd4918ea009dd8f35adee1aff1f2ecaba4`. The upstream MIT license is
included in `LICENSE`.

Grass uses the 64-bit variants: each entry holds an instance index in the low
32 bits and a squared camera-distance key in the high 32 bits. Ascending order
draws nearby instances first. `GrassSort.hlsl` prepares these entries and the
item counter entirely on the GPU, including sentinel padding to a power of two
of at least 2048 entries.

`GrassSorter.cpp` adapts the dispatch schedule from upstream
[BitonicSort.cpp](https://github.com/microsoft/DirectX-Graphics-Samples/blob/213dd4fd4918ea009dd8f35adee1aff1f2ecaba4/MiniEngine/Core/BitonicSort.cpp):
2048-item shared-memory presort, then outer and inner merges with UAV barriers.
It uses direct dispatches for the known padded capacity instead of upstream's
GPU-generated indirect dispatch arguments. The shader root signature and sorting
kernels retain the upstream algorithm. The presort's inner loop now explicitly
uses `[unroll]`, eliminating FXC X3557 when the outer loop is expanded.
No CPU sorting, readback, or upstream CPU validation code
is included.

All three Grass rendering modes use this sorter. Multi-GPU mode sorts the local
result on the primary GPU after copying the expanded grass data.
