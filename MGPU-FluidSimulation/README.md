# MGPU Fluid Simulation

Standalone fluid simulation imported from [PR #8](https://github.com/Mikhanil/Multi-GPU-Render-DX12/pull/8), head `d9006742f6333374fe2a322e7347bafaa83eed12`.

Open `DX12.sln`, select **MGPU-FluidSimulation** as startup project, and build **Release | x64**. The executable and its resources are placed in `x64/Release/MGPU-FluidSimulation/`. Run it with that directory as its working directory (Visual Studio is configured accordingly). Initial scene loading and automatic particle/render-path calibration can take several minutes.

The sample preserves the PR's fluid shaders, particle spawning, single-GPU simulation path, cross-adapter compute path, and startup calibration. It uses the repository's existing ImGui submodule, NuGet packages, shared assets, and resource-copy build targets. Its shader directory overrides shared shaders with the versions from the PR.

`Engine/` is a private snapshot of the support code required by the PR. Its component API and graphics interfaces differ from the current shared engine. Keeping it private preserves existing samples, including MGPU-Particles. Do not link this executable to the shared Common/Graphics libraries as well.

Integration changes: Visual Studio v145 and C++20, separate object/output directories, relocated ParticleSpawner include, self-contained benchmark header, and a stable reference returned from the model cache. Unused allocator demo implementations are excluded.

Validation: Release x64 builds successfully. Existing compiler warnings remain in the imported engine; long-running stability and completion of every calibration mode are not certified by a successful build.
