# Multi-GPU Fluid Simulation: Branch Architecture Guide

This document explains how the `fluid-sim` branch works, based on the code changes versus `master`.

## 1. What this branch adds

The branch introduces a GPU particle-based fluid simulation integrated into the existing DX12 multi-GPU sample framework.

Main additions:

- A fluid simulation pipeline using compute shaders (`ExternalForces`, `UpdateSpatialHash`, `Reorder`, `CalculateDensities`, `CalculatePressureForce`, `CalculateViscosity`, `UpdatePositions`).
- GPU spatial hashing and neighbor lookup acceleration.
- GPU counting sort + prefix sum helpers for hash key ordering.
- A `SharedFluidParticleEmitter` that can simulate on one GPU and render on the primary GPU, with optional cross-adapter copy.
- A new `RenderMode::Fluid` path and fluid particle draw shader.

## 2. Core runtime flow

The main integration point is `HybridParticleApp`.

Initialization path:

1. `HybridParticleApp::CreateGO` seeds the fluid calibration state and simulation template settings.
2. Startup calibration first tunes the render path without fluid particles active.
3. After render tuning finishes, `RecreateFluidEmitter` builds a fresh fluid `GameObject` and `SharedFluidParticleEmitter`.
4. The emitter is added to `typedRenderer[RenderMode::Fluid]` and begins participating in update/draw.

Per-frame path:

1. `Update` updates transforms/camera data for all game objects.
2. `Draw` chooses compute execution path:
   - Single-GPU: dispatch simulation on primary device.
   - Cross-adapter: dispatch on secondary compute queue, copy position/velocity into shared cross-adapter resources, then copy into primary buffers.
3. Forward pass draws fluid through `RenderMode::Fluid` alongside other scene renderers.

## 3. Startup calibration flow

Before the branch starts the actual particle simulation workload, it runs a startup calibration pass inside `HybridParticleApp`.

This calibration has two goals:

1. Tune the non-fluid render path so the scene alone lands near a stable baseline FPS.
2. Once the baseline is known, tune the fluid particle count so the full scene with simulation lands near the desired runtime FPS.

The calibration logic lives primarily in:

- `HybridParticleApp::HandleStartupCalibration`
- `HybridParticleApp::CalculateFrameStats`
- `HybridParticleApp::ApplyRenderPathTuningPreset`
- `HybridParticleApp::RecreateFluidEmitter`
- `HybridParticleApp::ResetPerformanceSamplingWindow`

### 3.1 Why calibration exists

This branch combines several heavy systems:

- Forward scene rendering with tunable SSAA, shadow-map resolution, and SSAO resolution.
- A particle-based fluid simulation whose cost scales with particle count.
- Optional cross-adapter execution, which changes the performance profile again.

Hardcoding a single particle count would make the sample behave very differently across machines. A fast GPU could leave a lot of headroom unused, while a slower machine could stall or become unreadable. The startup calibration tries to normalize that experience automatically.

### 3.2 High-level stages

Calibration is modeled with `StartupCalibrationStage`:

1. `TuneRenderPath`
2. `TuneFluidParticles`
3. `Completed`

Important detail:

- During `TuneRenderPath`, the app does **not** simulate fluid particles yet. It calibrates the render path first using the static scene.
- Only after render tuning finishes does the app create a `SharedFluidParticleEmitter` and start particle-count calibration.

That separation is intentional. It prevents particle cost from polluting the baseline render-path decision.

### 3.3 Render-path calibration targets

The render-only target window is:

- Minimum: `75 FPS`
- Maximum: `85 FPS`

The tunable render preset is stored in `RenderPathTuningPreset`:

- `ShadowMapSize`
- `SsaaMultiplier`
- `SsaoDivisor`

The current default startup preset is:

- `ShadowMapSize = 1024`
- `SsaaMultiplier = 2`
- `SsaoDivisor = 1`

Preset bounds are:

- Shadow map size: `512` to `16384`
- SSAA multiplier: `1` to `16`
- SSAO divisor: `1` to `4`

Interpretation of "heavier" vs "lighter":

- Heavier SSAA means increasing `SsaaMultiplier`.
- Heavier shadows means increasing `ShadowMapSize`.
- Heavier SSAO means decreasing `SsaoDivisor`, because a smaller divisor renders SSAO closer to full resolution.

### 3.4 Render-path search strategy

Render tuning is not a generic optimizer. It is a deterministic ordered walk through quality axes.

The quality order is:

1. SSAA
2. Shadow map size
3. SSAO quality

Heavier search order:

1. Increase `SsaaMultiplier` by `1` until it cannot increase further.
2. Increase `ShadowMapSize` by doubling it.
3. Increase SSAO cost by decrementing `SsaoDivisor`.

If the current sample is too light (`fps > 85`), the app tries to make the current axis heavier first.

If the current sample becomes too heavy (`fps < 75`) immediately after a heavier step, the app does **not** bounce forever between the last two neighboring presets. Instead it:

1. Reverts the last heavier step.
2. Advances to the next quality axis.
3. Tries the next way of increasing load.

Example:

1. `SSAA x4` gives `90 FPS`.
2. `SSAA x5` gives `70 FPS`.
3. The app backs out to `SSAA x4`.
4. Instead of retrying `SSAA x5`, it tries a heavier shadow setting next.

This behavior was added specifically to avoid the old oscillation bug where calibration could get stuck bouncing between two adjacent presets.

If the app is already too heavy before any recent heavier step, it uses a lighter fallback order:

1. Reduce SSAA.
2. Reduce shadow map size.
3. Reduce SSAO cost by increasing the SSAO divisor.

### 3.5 What happens when render tuning cannot hit the target window

Not every machine can land inside `75-85 FPS` with the discrete preset combinations available.

The current behavior is:

- If a valid preset inside the window is found, it is selected.
- If no more heavier or lighter moves are available, the app logs a warning and keeps the best terminal preset it reached.
- It still proceeds into fluid calibration.

This last point matters because there was an earlier failure mode where some machines could reach the end of render-path tuning without ever creating the fluid emitter. The current logic always transitions into `TuneFluidParticles`, even when render tuning ends at a boundary preset.

### 3.6 Fluid particle calibration targets

Once render tuning is done, the app switches to full fluid calibration.

The fluid target window is:

- Minimum: `55 FPS`
- Maximum: `65 FPS`
- Target: `60 FPS`

The fluid tuning variables tracked by `HybridParticleApp` are:

- `fluidCalibrationTargetParticleCount`
- `fluidCalibrationActualParticleCount`
- `fluidCalibrationIterations`
- `fluidSpawnRegion`
- `fluidSimulationScale`

The particle-count calibration has a hard cap of `16` iterations.

### 3.7 How fluid emitter recreation works

Each fluid calibration step fully recreates the emitter through `RecreateFluidEmitter`.

That method:

1. Removes the old fluid renderer from `typedRenderer[RenderMode::Fluid]`.
2. Removes the old fluid `GameObject`.
3. Recomputes the intended simulation bounds from the target particle count.
4. Builds spawn data with `ParticleSpawner`.
5. Measures the actual particle count produced by the spawner.
6. Recomputes bounds again using the actual spawned count.
7. Creates a fresh `SharedFluidParticleEmitter`.
8. Attaches it to a new `GameObject`.
9. Re-inserts that object into the render/update lists.

This matters because the actual spawned particle count is grid-derived, not a guaranteed exact match for the original target. The code therefore calibrates using the true spawned count rather than assuming the requested target was achieved perfectly.

### 3.8 Fluid particle count update rule

The fluid tuning loop uses proportional scaling:

- `nextTarget = currentTarget * (measuredFps / targetFps)`

With `targetFps = 60`, this means:

- If measured FPS is above `60`, particle count increases.
- If measured FPS is below `60`, particle count decreases.

The result is clamped to avoid going below the minimum particle floor.

There is also a small nudge rule:

- If rounding would produce the exact same particle target as the previous iteration, the app nudges the target by `+64` or `-64` depending on whether the FPS is above or below target.

That prevents the calibration loop from stalling on a no-op update caused by rounding.

### 3.9 How the 5-second measurement window works

Calibration uses `CalibrationSampleSeconds = 5.0f`.

The timing flow is:

1. `CalculateFrameStats` accumulates frames while calibration is active.
2. Once at least `5` seconds have elapsed, it computes the average FPS for that sample window.
3. That average is passed into `HandleStartupCalibration`.
4. If calibration changes the render preset or recreates the fluid emitter, the sampling window is reset immediately.

The reset is important.

Without a reset, a "5 second" sample taken after changing presets could accidentally include frames from both the old and new configurations. The current implementation avoids that by calling `ResetPerformanceSamplingWindow()` after:

- Applying a new render-path preset
- Starting fluid calibration
- Recreating the fluid emitter for a new particle target

### 3.10 FPS calculation semantics

The calibration window is "at least 5 seconds", but the FPS is computed from the **actual elapsed duration** of that sample rather than dividing by a hardcoded `5.0`.

This matters because frame boundaries are discrete. In practice, the sample may end a little after 5 seconds. The code now computes:

- `fps = frameCount / actualElapsedSeconds`

That gives a true average over the real post-apply window instead of a slightly biased estimate.

### 3.11 Interaction with normal runtime stats

After calibration reaches `Completed`, `CalculateFrameStats` switches back to the normal one-second stat cadence used for window title updates and logging.

So there are effectively two timing modes:

- Calibration mode: 5-second averaging windows
- Steady-state mode: 1-second reporting windows

### 3.12 Failure modes that the current design addresses

The current calibration code is shaped by a few specific bugs and edge cases:

1. Preset oscillation
   The old logic could bounce forever between neighboring render presets, especially across SSAA boundaries.

2. Mixed-window FPS samples
   A calibration result could include frames from before and after a preset/emitter change, producing misleading decisions.

3. No-particles-after-calibration on some machines
   If render tuning ended at a preset boundary without landing in the target window, the old control flow could fail to enter fluid calibration. The current code always advances into emitter creation.

4. Particle-target deadband from rounding
   The fluid loop could request the same particle count again after rounding. The `+64/-64` nudge prevents that.

## 4. Fluid data model

`FluidSimulationData` (`Common/FluidSimulationData.h`) is the simulation constant buffer:

- Particle count, gravity, timestep, sim time.
- Pressure/density/viscosity parameters.
- Local/world matrices for bounds-space collision.
- Interaction inputs (reserved for external interaction).

`ParticleSpawner` (`MGPU-Particles/ParticleSpawner.h`) generates initial particle positions/velocities by filling one or more cube regions at a target density.

## 5. Simulation resources and PSO setup

`FluidSimulationResources` (`Common/SharedFluidParticleEmitter.h/.cpp`) owns per-device simulation state:

- Structured buffers for:
  - Positions, predicted positions, velocities, densities.
  - Reorder target buffers.
  - Spatial hash keys/indices/offsets.
- Compute root signature + all fluid compute PSOs.
- Descriptor heap binding all UAV/SRV views needed by kernels.

Each GPU gets its own `FluidSimulationResources`:

- `PrimaryResources` for rendering-facing state.
- `SecondaryResources` for optional second-GPU compute.

## 6. Simulation step order (compute pipeline)

`SharedFluidParticleEmitter::RunSimulationStep` executes kernels in this order:

1. `ExternalForces`
2. `UpdateSpatialHash`
3. `SpatialHash.Run` (counting sort + offsets)
4. `Reorder`
5. `ReorderCopyBack`
6. `CalculateDensities`
7. `CalculatePressureForce`
8. `CalculateViscosity` (optional when viscosity > 0)
9. `UpdatePositions`

Between each stage, UAV barriers are used to enforce ordering.

`Dispatch` runs multiple substeps per frame (`IterationsPerFrame`, default 3), updates sim constants, and advances `simTime`.

## 7. Spatial hash and neighbor search

Neighbor queries are accelerated with hash bins:

- `UpdateSpatialHash` computes a key per particle from predicted position.
- `SpatialHash` (`Common/SpacialHash.*`) sorts indices by key.
- `SpacialOffsetsCalculator` computes, for each key/bin, the first index in the sorted array.
- Density/pressure/viscosity kernels iterate only candidate particles from the 27 neighboring cells.

Key helper modules:

- `GPUCountingSort` (`Common/GPUCountingSort.*` + `Shaders/Helpers/GPUSort/CountingSort.hlsl`)
- `GPUPrefixSum` (`Common/GPUPrefixSum.*` + `Shaders/Helpers/GPUSort/PrefixSum.hlsl`)
- `SpacialOffsetsCalculator` (`Common/SpacialOffsetsCalculator.*` + `Shaders/Helpers/SpacialOffsets.hlsl`)

## 8. Multi-GPU behavior

Cross-adapter resources are represented by `GCrossAdapterResource`:

- One resource view in primary device heap.
- One shared view opened by secondary device.

Fluid path when `UseCrossAdapter == true` in `HybridParticleApp::Draw`:

1. Run fluid compute on `secondDevice` compute queue.
2. Copy secondary position/velocity buffers to shared cross-adapter resources.
3. On primary command list, copy shared resources into primary simulation buffers.
4. Render from primary buffers.

When `UseCrossAdapter == false`, compute and render both use primary device buffers.

## 9. Fluid rendering path

Rendering uses:

- `SharedFluidParticleEmitter::InitializePSO` for graphics PSO/root signature.
- `Shaders/FluidParticleDraw.hlsl` for billboarding and velocity-based coloring.

Draw call:

- Instanced quad (`Draw(4, numParticles)`), one billboarded quad per particle.
- Position and velocity read as SRVs.
- Color lerped from blue to red by particle speed.

## 10. Files to start with

If you are onboarding to this branch, read in this order:

1. `MGPU-Particles/HybridParticleApp.cpp`
2. `Common/SharedFluidParticleEmitter.h`
3. `Common/SharedFluidParticleEmitter.cpp`
4. `Common/SpacialHash.h`
5. `Common/SpacialHash.cpp`
6. `Common/GPUCountingSort.cpp`
7. `Common/GPUPrefixSum.cpp`
8. `MGPU-Particles/Shaders/FluidSimulation.hlsl`
9. `MGPU-Particles/Shaders/FluidParticleDraw.hlsl`
