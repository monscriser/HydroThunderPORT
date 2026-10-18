# Xenia (master + Canary) GPU cvars that affect GPU cost / FPS, focus on Vulkan

Sources read directly: raw source files on GitHub (xenia-project/xenia `master`, xenia-canary/xenia-canary `canary_experimental`), and two xenia.jp blog posts by Triang3l (2021-04 render target cache, 2022-01 presentation update). Several large source files were only read for their first ~100k chars (truncated by the fetch tool); cvars defined later in those files may be missing. Quotes are short; most descriptions are paraphrased.

Key source URLs:
- Canary gpu_flags.cc: https://raw.githubusercontent.com/xenia-canary/xenia-canary/canary_experimental/src/xenia/gpu/gpu_flags.cc
- Master gpu_flags.cc: https://raw.githubusercontent.com/xenia-project/xenia/master/src/xenia/gpu/gpu_flags.cc
- Master render_target_cache.cc: https://raw.githubusercontent.com/xenia-project/xenia/master/src/xenia/gpu/render_target_cache.cc
- Canary render_target_cache.cc: https://raw.githubusercontent.com/xenia-canary/xenia-canary/canary_experimental/src/xenia/gpu/render_target_cache.cc
- Master/Canary vulkan_render_target_cache.cc: https://raw.githubusercontent.com/xenia-project/xenia/master/src/xenia/gpu/vulkan/vulkan_render_target_cache.cc , https://raw.githubusercontent.com/xenia-canary/xenia-canary/canary_experimental/src/xenia/gpu/vulkan/vulkan_render_target_cache.cc
- Master d3d12_render_target_cache.cc: https://raw.githubusercontent.com/xenia-project/xenia/master/src/xenia/gpu/d3d12/d3d12_render_target_cache.cc
- Master texture_cache.cc: https://raw.githubusercontent.com/xenia-project/xenia/master/src/xenia/gpu/texture_cache.cc
- Canary command_processor.cc: https://raw.githubusercontent.com/xenia-canary/xenia-canary/canary_experimental/src/xenia/gpu/command_processor.cc
- Canary graphics_system.cc: https://raw.githubusercontent.com/xenia-canary/xenia-canary/canary_experimental/src/xenia/gpu/graphics_system.cc
- Canary vulkan_shared_memory.cc: https://raw.githubusercontent.com/xenia-canary/xenia-canary/canary_experimental/src/xenia/gpu/vulkan/vulkan_shared_memory.cc
- Canary vulkan_pipeline_cache.cc: https://raw.githubusercontent.com/xenia-canary/xenia-canary/canary_experimental/src/xenia/gpu/vulkan/vulkan_pipeline_cache.cc
- Canary vulkan_presenter.cc: https://raw.githubusercontent.com/xenia-canary/xenia-canary/canary_experimental/src/xenia/ui/vulkan/vulkan_presenter.cc
- Master/Canary presenter.cc: https://raw.githubusercontent.com/xenia-project/xenia/master/src/xenia/ui/presenter.cc , https://raw.githubusercontent.com/xenia-canary/xenia-canary/canary_experimental/src/xenia/ui/presenter.cc
- Canary emulator_window.cc: https://raw.githubusercontent.com/xenia-canary/xenia-canary/canary_experimental/src/xenia/app/emulator_window.cc
- Blog "Leaving no pixel behind" (2021-04-27): https://xenia.jp/updates/2021/04/27/leaving-no-pixel-behind-new-render-target-cache-3x3-resolution-scaling.html
- Blog "Presenting the presentation update" (2022-01-29): https://xenia.jp/updates/2022/01/29/presenting-the-presentation-update-amd-fidelityfx-fsr-fxaa-cas-vrr.html

## 1. Render target path (render_target_path_vulkan / render_target_path_d3d12): cost and per-vendor default

### Takeaway
On Vulkan the default is always host render targets ("fbo"); FSI is opt-in and described by the Xenia authors themselves as "much slower now". On D3D12 the default is RTV for NVIDIA and AMD, ROV only on Intel (stencil bug workaround). Switching to FSI/ROV would almost certainly increase, not decrease, GPU load for Hydro Thunder.

### Cited Findings
- `render_target_path_vulkan` (string, default `""`, values any/fbo/fsi). fbo = host framebuffers + fixed-function blend/depth/stencil, copying between RTs when needed, "Lower accuracy", performance "generally higher", limited mainly by RT layout changes that need copies. fsi = manual pixel packing/blend/depth-stencil in shader, free RT layout changes, highest accuracy, "Performance limited primarily by overdraw". Any other value picks "currently always FB because the FSI path is much slower now". Identical text in master and Canary. — [master vulkan_render_target_cache.cc](https://raw.githubusercontent.com/xenia-project/xenia/master/src/xenia/gpu/vulkan/vulkan_render_target_cache.cc); [Canary vulkan_render_target_cache.cc](https://raw.githubusercontent.com/xenia-canary/xenia-canary/canary_experimental/src/xenia/gpu/vulkan/vulkan_render_target_cache.cc)
- Vulkan selection code has no vendor check: path is host RTs unless cvar == "fsi"; even then it falls back to fbo unless the device has fragmentShaderSample/PixelInterlock, fragmentStoresAndAtomics, sampleRateShading, standardSampleLocations, and enough storage-buffer slots. — same sources
- `render_target_path_d3d12` (rtv/rov/any): ROV "Highest accuracy"; "On AMD drivers, currently causes shader compiler crashes in many cases"; default "currently always RTV because the ROV path is much slower now", except Intel (stencil-test bug breaks D3D9 clears) which gets ROV. Code comment: "The ROV path is currently much slower generally." TODO: make ROV default "when it's optimized better". — [master d3d12_render_target_cache.cc](https://raw.githubusercontent.com/xenia-project/xenia/master/src/xenia/gpu/d3d12/d3d12_render_target_cache.cc)
- How ROV/FSI costs: output merger implemented in shaders; depth test in shader, whole shader runs interlocked, hierarchical depth cannot be used; long shaders slow per-pixel work and driver compilation. RTV keeps host early-Z/Hi-Z. Support: NVIDIA since 2nd-gen Maxwell, AMD from Vega but only in D3D (not Vulkan), Intel all D3D12 GPUs. — [xenia.jp 2021-04](https://xenia.jp/updates/2021/04/27/leaving-no-pixel-behind-new-render-target-cache-3x3-resolution-scaling.html)
- Benchmark (GTX 1070, frame time ms, old cache / new RTV / ROV): Halo 3 lobby 4xAA 79.8 / 23.5 / 52.5; Halo 3 gameplay 34.1 / 13.6 / 23.8; Banjo-Kazooie menu 2xAA 31.6 / 17.5 / 59.4; Sonic 2xAA 19.5 / 9.2 / 11.2. New RTV fastest in all four (ROV 1.2x-3.4x slower on NVIDIA). — [xenia.jp 2021-04](https://xenia.jp/updates/2021/04/27/leaving-no-pixel-behind-new-render-target-cache-3x3-resolution-scaling.html)
- `execute_unclipped_draw_vs_on_cpu_for_psi_render_backend` (bool, default true): runs VS on CPU for unclipped draws under the PSI/FSI backend to avoid excessive barriers; on by default because "the GPU is overall usually the bottleneck" with that backend. — [master render_target_cache.cc](https://raw.githubusercontent.com/xenia-project/xenia/master/src/xenia/gpu/render_target_cache.cc)
- Canary `occlusion_query_viz` notes VIZ predication "can both cause a noticeable penalty with ROV and FSI". — [Canary command_processor.cc](https://raw.githubusercontent.com/xenia-canary/xenia-canary/canary_experimental/src/xenia/gpu/command_processor.cc)

### Inferences
- For our port (RTX 5060 Ti, Vulkan), keep fbo. FSI would trade RT-copy cost for per-pixel interlocked shading and lose Hi-Z/early-Z; for an overdraw-heavy racing game with water/spray, that is likely worse.
- On the fbo path, the main GPU overheads Xenia itself adds are RT ownership-transfer copies (format/pitch/MSAA changes) and resolves; these are what the EDRAM cvars below tune.

### Gaps
- No independent 2023-2026 benchmarks (Reddit/Discord) of Vulkan FSI on NVIDIA RTX 40/50 or AMD RDNA were found; searches returned nothing relevant.
- Vulkan-specific FSI vs FBO numbers not published; the only numbers are D3D12 ROV vs RTV on GTX 1070 (2021).

## 2. EDRAM / render target / resolve options

### Takeaway
Most EDRAM cvars are accuracy toggles whose defaults are already the cheap choice; the ones with real GPU cost are draw_resolution_scale (quadratic pixel cost), depth_float24_convert_in_pixel_shader (kills depth compression/early-Z, per-sample shading), native_stencil_value_output=false (8-quad stencil path), readback_resolve full/readback_memexport (CPU-GPU sync stalls), and gamma_render_target_as_unorm16 (possible copy cost on view switches).

### Cited Findings (name — default — what it does — perf — fidelity risk; all exist on Vulkan unless noted)
- `draw_resolution_scale_x` / `_y` (int32, default 1; 1-3, >1 depends on device). Integer render scale. 2x2 = 4x pixels to rasterize/shade and 4x RT data to copy; at 2x2 "stable 30 FPS could not be maintained" in several games on mid-range systems; 1x2/2x1 "much lighter than full 2x2"; 3x3 = 9x pixels, "massive" cost (RTX 3080-class for 30 FPS in most games). Risk: effects may break. — [master texture_cache.cc](https://raw.githubusercontent.com/xenia-project/xenia/master/src/xenia/gpu/texture_cache.cc); [xenia.jp 2022-01](https://xenia.jp/updates/2022/01/29/presenting-the-presentation-update-amd-fidelityfx-fsr-fxaa-cas-vrr.html); [xenia.jp 2021-04](https://xenia.jp/updates/2021/04/27/leaving-no-pixel-behind-new-render-target-cache-3x3-resolution-scaling.html)
- `draw_resolution_scale_threshold` (Canary only, uint32, default 0 = off): keep small-pitch RTs (effect buffers) at native scale; host-RT path only. Would cut cost of scaling low-res effect buffers; no perf figure given. — [Canary render_target_cache.cc](https://raw.githubusercontent.com/xenia-canary/xenia-canary/canary_experimental/src/xenia/gpu/render_target_cache.cc)
- `draw_resolution_scaled_texture_offsets` (bool, default true): scales texture fetch offsets for render-to-texture (shadow filtering, bloom, blur); no stated perf cost; may give undesirable results in some cases. — [master render_target_cache.cc](https://raw.githubusercontent.com/xenia-project/xenia/master/src/xenia/gpu/render_target_cache.cc); [xenia.jp 2021-04](https://xenia.jp/updates/2021/04/27/leaving-no-pixel-behind-new-render-target-cache-3x3-resolution-scaling.html)
- `gamma_render_target_as_unorm16` (bool, default true; GPU.Debug in Canary): uses UNORM16 for 8_8_8_8_GAMMA RTs when host can't blend gamma correctly in linear space; "may result in render target copying costs" if game switches between GAMMA and non-GAMMA views of the same EDRAM. Also doubles bytes/pixel vs 8-bit (inference). Setting false → piecewise-linear conversion in shader (blending incorrect). — [master render_target_cache.cc](https://raw.githubusercontent.com/xenia-project/xenia/master/src/xenia/gpu/render_target_cache.cc); [Canary render_target_cache.cc](https://raw.githubusercontent.com/xenia-canary/xenia-canary/canary_experimental/src/xenia/gpu/render_target_cache.cc)
- `gamma_render_target_as_srgb` (older option, 2021): sRGB writes for linear blending on non-ROV path; was "disabled by default due to noticeably incorrect colors in certain games". Not present in the current master/Canary render_target_cache.cc cvar lists read here → appears superseded by `gamma_render_target_as_unorm16`. — [xenia.jp 2021-04](https://xenia.jp/updates/2021/04/27/leaving-no-pixel-behind-new-render-target-cache-3x3-resolution-scaling.html)
- `snorm16_render_target_full_range` (bool, default true): remaps -32..32 into host -1..1 SNORM16; cost is "multiplicative blending correctness", not speed. — [Canary render_target_cache.cc](https://raw.githubusercontent.com/xenia-canary/xenia-canary/canary_experimental/src/xenia/gpu/render_target_cache.cc)
- `depth_float24_round` (bool, default false): round vs truncate when converting 32-bit host depth to 20e4. With pixel-shader conversion: true is "Slower - depth rejection before the pixel shader is not possible", false "Faster". Recommended false. — [Canary render_target_cache.cc](https://raw.githubusercontent.com/xenia-canary/xenia-canary/canary_experimental/src/xenia/gpu/render_target_cache.cc)
- `depth_float24_convert_in_pixel_shader` (bool, default false): converts depth to 20e4 in guest pixel shaders to avoid precision artifacts after reloads. "costly option because it makes the GPU unable to use depth buffer compression"; with MSAA forces per-sample shading, "2 or 4 times heavier". Ignored with software depth (FSI). Keep false. — [master render_target_cache.cc](https://raw.githubusercontent.com/xenia-project/xenia/master/src/xenia/gpu/render_target_cache.cc)
- `depth_transfer_not_equal_test` (bool, default true): not-equal test on depth transfers to avoid rewriting unchanged depth (friendlier to depth compression); false "may reduce bandwidth usage during transfers". — [Canary render_target_cache.cc](https://raw.githubusercontent.com/xenia-canary/xenia-canary/canary_experimental/src/xenia/gpu/render_target_cache.cc)
- `aliased_depth_read_only` (Canary only, bool, default true): keeps read-only depth bound when color writes only unused stencil bits; "May slightly increase overhead". — [Canary render_target_cache.cc](https://raw.githubusercontent.com/xenia-canary/xenia-canary/canary_experimental/src/xenia/gpu/render_target_cache.cc)
- `native_2x_msaa` (master, default true) / `debug_msaa_2x_as_4x` (Canary, default false, inverse sense): host 2x MSAA; the 4x-with-2-samples substitute is "similar or worse quality and higher memory usage". Keep native. — [master render_target_cache.cc](https://raw.githubusercontent.com/xenia-project/xenia/master/src/xenia/gpu/render_target_cache.cc); [Canary render_target_cache.cc](https://raw.githubusercontent.com/xenia-canary/xenia-canary/canary_experimental/src/xenia/gpu/render_target_cache.cc)
- `native_stencil_value_output` (bool, default true): uses shader stencil-ref export for RT copies; disabling → "much more expensive drawing of 8 quads". The blog notes only AMD exposed SV_StencilRef in D3D12 (NVIDIA/Intel used 8 quads); in Vulkan this depends on VK_EXT_shader_stencil_export support (inference). — [master render_target_cache.cc](https://raw.githubusercontent.com/xenia-project/xenia/master/src/xenia/gpu/render_target_cache.cc); [xenia.jp 2021-04](https://xenia.jp/updates/2021/04/27/leaving-no-pixel-behind-new-render-target-cache-3x3-resolution-scaling.html)
- `mrt_edram_used_range_clamp_to_min` (bool, default true): clamps estimated EDRAM range of MRTs; false = rare accuracy gain but "may increase the amount of copying". — [master render_target_cache.cc](https://raw.githubusercontent.com/xenia-project/xenia/master/src/xenia/gpu/render_target_cache.cc)
- `readback_resolve` (Canary, string; DEFINE default "none" but `UPDATE_from_string(... 2025,12,4 ..., "fast")` makes effective default "fast"): CPU readback of resolves. fast = previous-frame data, "slight performance hit", no stall; full = waits for GPU, "accurate but slow, GPU-CPU sync stall"; none = disabled. The 2021 blog: waiting right after submission forced GPU/CPU lockstep, halving frame rate in its example (was `d3d12_readback_resolve`). — [Canary command_processor.cc](https://raw.githubusercontent.com/xenia-canary/xenia-canary/canary_experimental/src/xenia/gpu/command_processor.cc); [xenia.jp 2021-04](https://xenia.jp/updates/2021/04/27/leaving-no-pixel-behind-new-render-target-cache-3x3-resolution-scaling.html)
- `readback_resolve_half_pixel_offset` (Canary, default false): sample center of scaled pixel block on readback with scaling. — [Canary command_processor.cc](https://raw.githubusercontent.com/xenia-canary/xenia-canary/canary_experimental/src/xenia/gpu/command_processor.cc)
- `readback_memexport` (Canary, default false): CPU readback of memexport; "causes mid-frame synchronization, so it has a huge performance impact". — [Canary command_processor.cc](https://raw.githubusercontent.com/xenia-canary/xenia-canary/canary_experimental/src/xenia/gpu/command_processor.cc)
- `clear_memory_page_state` (Canary, default false): refreshes page state so GPU-written data is visible; for "Team Ninja" games. No perf note, but forces re-upload/invalidation (inference). — [Canary command_processor.cc](https://raw.githubusercontent.com/xenia-canary/xenia-canary/canary_experimental/src/xenia/gpu/command_processor.cc)
- `resolve_resolution_scale_fill_half_pixel_offset`: not found in the files read (master/Canary render_target_cache.cc, Canary command_processor.cc). See Gaps.

### Inferences
- Our config (scale 1, unorm16 true, readback none) already sits on the cheap side. readback_resolve=none is cheaper than Canary's new effective default "fast".
- If the app's "Nativa" quality at 1080p output uses draw_resolution_scale 2 (1280x720 → 2560x1440 internal, since 1.5x is not an integer option), that alone is 4x pixel/RT-copy work and is the most likely explanation of 99% GPU / 85 FPS vs 46% / 120 FPS for FSR1 at scale 1. Verify in `RenderScale()` (`app/src/ht_setup_wizard.cpp`).
- `draw_resolution_scale_threshold` (Canary) is a candidate to backport if a scale >1 mode is kept: keep small effect buffers at 1x.

### Gaps
- `resolve_resolution_scale_fill_half_pixel_offset` and `draw_resolution_scale_*` location in Canary were not confirmed (Canary texture_cache.cc not fetched); may live in texture_cache.cc or have been renamed.
- No measured cost for gamma unorm16 vs 8-bit on modern GPUs.

## 3. Texture options

### Takeaway
Texture cache limits only affect VRAM residency/eviction (not per-frame shading); anisotropic_override is a per-sample cost, mostly negligible on modern NVIDIA at 720p; vulkan_sparse_shared_memory=true is the low-VRAM default.

### Cited Findings
- `texture_cache_memory_limit_soft` (uint32 MB, default 384): above it, old textures destroyed. `texture_cache_memory_limit_soft_lifetime` (default 30 s unused before deletion when over soft). `texture_cache_memory_limit_hard` (default 768): above it, destroyed ASAP. `texture_cache_memory_limit_render_to_texture` (default 24 MB): portion scaled by draw resolution scale, included in soft/hard. — [master texture_cache.cc](https://raw.githubusercontent.com/xenia-project/xenia/master/src/xenia/gpu/texture_cache.cc)
- `anisotropic_override` (int32, default -1 = no override, up to 16). — [Canary gpu_flags.cc](https://raw.githubusercontent.com/xenia-canary/xenia-canary/canary_experimental/src/xenia/gpu/gpu_flags.cc)
- `non_seamless_cube_map` (default true; VK_EXT_non_seamless_cube_map, D3D9 behaviour). — [master gpu_flags.cc](https://raw.githubusercontent.com/xenia-project/xenia/master/src/xenia/gpu/gpu_flags.cc)
- `gpu_3d_to_2d_texture` (Canary, default true): treat 3D samples as 2D slice 0. — [Canary gpu_flags.cc](https://raw.githubusercontent.com/xenia-canary/xenia-canary/canary_experimental/src/xenia/gpu/gpu_flags.cc)
- `vulkan_sparse_shared_memory` (default true): sparse binding for the 512 MB guest-memory buffer; disabling creates a full 512 MB buffer (more VRAM), only useful for debuggers. — [Canary vulkan_shared_memory.cc](https://raw.githubusercontent.com/xenia-canary/xenia-canary/canary_experimental/src/xenia/gpu/vulkan/vulkan_shared_memory.cc)
- `gpu_allow_invalid_upload_range` (Canary, default false): allow reading no-access pages. — [Canary gpu_flags.cc](https://raw.githubusercontent.com/xenia-canary/xenia-canary/canary_experimental/src/xenia/gpu/gpu_flags.cc)

### Inferences
- Our 384/768 equal the defaults; lowering them would just cause more texture re-uploads (CPU+transfer cost), not lower shading load.
- anisotropic_override=4 vs -1: small cost; dropping it is a minor lever at best (no Xenia measurement found).

### Gaps
- No Xenia measurement of anisotropic cost. No dedicated mip/LOD-bias cvars found in the files read.

## 4. Occlusion queries

### Takeaway
Master fakes occlusion results (no GPU query cost); Canary added real queries with a "fast" default and "strict" (stall) / full-counter options that cost performance.

### Cited Findings
- Master: `query_occlusion_fake_sample_count` (int32, default 1000): fake count per EVENT_WRITE_ZPD; -1 writes nothing (games may hang), 0 = everything occluded. — [master gpu_flags.cc](https://raw.githubusercontent.com/xenia-project/xenia/master/src/xenia/gpu/gpu_flags.cc)
- Canary: `occlusion_query` (string, default "fast"): fake (no GPU, effects may be slightly wrong) / fast (cached result immediately, updated later, biased to visible) / fast-alt / strict (waits real result, "may be somewhat less performant"). — [Canary command_processor.cc](https://raw.githubusercontent.com/xenia-canary/xenia-canary/canary_experimental/src/xenia/gpu/command_processor.cc)
- Canary: `occlusion_query_full_counters` (default false): in-shader ZFail/StencilFail/Total counters, "typically slow and rife with readback sync". `occlusion_query_viz` (default false): VIZ_QUERY predication, penalty with ROV/FSI. `occlusion_query_fake_lower_threshold`/`upper_threshold` (80/100). — [Canary command_processor.cc](https://raw.githubusercontent.com/xenia-canary/xenia-canary/canary_experimental/src/xenia/gpu/command_processor.cc); [Canary gpu_flags.cc](https://raw.githubusercontent.com/xenia-canary/xenia-canary/canary_experimental/src/xenia/gpu/gpu_flags.cc)

### Inferences
- `occlusion_query_enable` does not exist under that name in the files read. If ReXGlue follows master's fake path, occlusion has ~zero GPU cost; fake-visible results mean the game never culls via queries, so it may draw more than on console (could raise GPU load for occlusion-culled content; need to check if HTH uses ZPD).

### Gaps
- Whether Hydro Thunder issues ZPD queries is unknown.

## 5. Present / swap / post-processing

### Takeaway
Present-side passes (FSR/CAS/FXAA, dither, letterbox) are cheap per the Xenia authors; frame pacing is controlled by vsync/framerate_limit; Vulkan present-mode cvars only choose among immediate > mailbox > fifo_relaxed > fifo.

### Cited Findings
- `vsync` (default true); `framerate_limit` (Canary, uint64, default 0 = unlimited; with vsync on and no limit, limiter locks to 60). Canary limiter sleeps 90% then spins 10% of vblank on Windows; Linux full sleep and catch-up capped at 1 s. — [Canary gpu_flags.cc](https://raw.githubusercontent.com/xenia-canary/xenia-canary/canary_experimental/src/xenia/gpu/gpu_flags.cc); [Canary graphics_system.cc](https://raw.githubusercontent.com/xenia-canary/xenia-canary/canary_experimental/src/xenia/gpu/graphics_system.cc)
- `vulkan_allow_present_mode_immediate` / `_mailbox` / `_fifo_relaxed` (all default true; priority immediate > mailbox > fifo_relaxed > FIFO fallback). FIFO/FIFO-relaxed mark vsync implicit. `vulkan_semaphore_reuse_workaround` (default false) waits queue idle each frame, "costs significant performance". — [Canary vulkan_presenter.cc](https://raw.githubusercontent.com/xenia-canary/xenia-canary/canary_experimental/src/xenia/ui/vulkan/vulkan_presenter.cc)
- Blog: Xenia "prefers lowering latency over preventing tearing"; to avoid tearing on Vulkan disable immediate and fifo_relaxed; FIFO fallback can block the UI thread. — [xenia.jp 2022-01](https://xenia.jp/updates/2022/01/29/presenting-the-presentation-update-amd-fidelityfx-fsr-fxaa-cas-vrr.html)
- `host_present_from_non_ui_thread` (default true), `present_render_pass_clear` (default true), `present_letterbox` (default true), `present_safe_area_x/y` (master 90, Canary 100). — [master presenter.cc](https://raw.githubusercontent.com/xenia-project/xenia/master/src/xenia/ui/presenter.cc); [Canary presenter.cc](https://raw.githubusercontent.com/xenia-canary/xenia-canary/canary_experimental/src/xenia/ui/presenter.cc)
- There is no `present_effect` cvar in current code; effects are `postprocess_antialiasing` (""/none/fxaa/fxaa_extreme), `postprocess_scaling_and_sharpening` (""→bilinear/cas/fsr), `postprocess_ffx_cas_additional_sharpness`, `postprocess_ffx_fsr_max_upsampling_passes` (reduce on 4K/8K "in case the performance impact of multiple FSR upsampling passes is too high"), `postprocess_ffx_fsr_sharpness_reduction`, `postprocess_dither` (default true). — [Canary emulator_window.cc](https://raw.githubusercontent.com/xenia-canary/xenia-canary/canary_experimental/src/xenia/app/emulator_window.cc)
- FSR "at virtually no performance cost"; effects overall "low, or even near-zero, performance cost". EASU applied per up-to-2x2 step, then one RCAS; CAS used when not upscaling; FXAA on final image before FSR/CAS; dithering blue-noise to 8bpc, intermediates 10bpc. — [xenia.jp 2022-01](https://xenia.jp/updates/2022/01/29/presenting-the-presentation-update-amd-fidelityfx-fsr-fxaa-cas-vrr.html)

### Inferences
- 720p→1440p is exactly 2x2, so one EASU pass + RCAS: cheap. The GPU difference between modes comes from guest rendering, not presentation.
- With vsync=true/60 cap, GPU% at a capped FPS reflects per-frame cost; uncapped (fps_limit=0) GPU will naturally go toward 100%. Compare GPU% only at equal FPS caps.

### Gaps
- No ms figures for FSR/CAS/FXAA passes.

## 6. Shaders / pipelines

### Takeaway
Shader cvars mostly affect stutter (CPU/driver compile), not steady-state GPU load; defaults are already the performance choice.

### Cited Findings
- `async_shader_compilation` (Canary, default true): compile shaders/pipelines on background threads. Canary uses placeholder pipeline hot-swap to reduce stutter. — [Canary gpu_flags.cc](https://raw.githubusercontent.com/xenia-canary/xenia-canary/canary_experimental/src/xenia/gpu/gpu_flags.cc); [Canary vulkan_pipeline_cache.cc](https://raw.githubusercontent.com/xenia-canary/xenia-canary/canary_experimental/src/xenia/gpu/vulkan/vulkan_pipeline_cache.cc)
- `vulkan_pipeline_creation_threads` (int32, default -1 = 75% of logical cores; 0 = no multithreading). — [Canary vulkan_pipeline_cache.cc](https://raw.githubusercontent.com/xenia-canary/xenia-canary/canary_experimental/src/xenia/gpu/vulkan/vulkan_pipeline_cache.cc)
- `store_shaders` (default true, GPU.Debug): persist shaders to avoid "runtime spikes and freezes". — [Canary graphics_system.cc](https://raw.githubusercontent.com/xenia-canary/xenia-canary/canary_experimental/src/xenia/gpu/graphics_system.cc)
- `dump_shaders` (path, default ""): debugging only. — [master gpu_flags.cc](https://raw.githubusercontent.com/xenia-project/xenia/master/src/xenia/gpu/gpu_flags.cc)
- `gpu_allow_invalid_fetch_constants`: master default false ("generally unsafe"), Canary default true. Compatibility, not performance. — [master gpu_flags.cc](https://raw.githubusercontent.com/xenia-project/xenia/master/src/xenia/gpu/gpu_flags.cc); [Canary gpu_flags.cc](https://raw.githubusercontent.com/xenia-canary/xenia-canary/canary_experimental/src/xenia/gpu/gpu_flags.cc)
- Other shader-semantic cvars (Canary): `half_pixel_offset` (true), `force_depth_clamp` (false), `mulsc_round_toward_zero` (false), `xenos_sample_positions` (false), `no_discard_stencil_in_transfer_pipelines` (false), `vulkan_depth_bias_force_unorm` (true). No perf notes. — [Canary gpu_flags.cc](https://raw.githubusercontent.com/xenia-canary/xenia-canary/canary_experimental/src/xenia/gpu/gpu_flags.cc); [Canary vulkan_command_processor.cc](https://raw.githubusercontent.com/xenia-canary/xenia-canary/canary_experimental/src/xenia/gpu/vulkan/vulkan_command_processor.cc)

### Inferences
- None of these will lower steady GPU%. Placeholder pipelines during async compile could briefly skip draws (fewer GPU work, visual pops).

### Gaps
- `spirv_disable_rounding_mode_rte` referenced but not read.

## 7. Recommended performance presets / game configs

### Takeaway
No official Xenia/Canary "performance preset" was found; the only official guidance is implicit in cvar defaults and the blog posts (keep RTV/FBO, scale 1, readback off/fast, avoid depth conversion in PS).

### Cited Findings
- Defaults intentionally favour speed: FB/RTV "because the FSI path is much slower now"; `depth_float24_convert_in_pixel_shader` false; `readback_memexport` false. — [master vulkan_render_target_cache.cc](https://raw.githubusercontent.com/xenia-project/xenia/master/src/xenia/gpu/vulkan/vulkan_render_target_cache.cc); [master render_target_cache.cc](https://raw.githubusercontent.com/xenia-project/xenia/master/src/xenia/gpu/render_target_cache.cc); [Canary command_processor.cc](https://raw.githubusercontent.com/xenia-canary/xenia-canary/canary_experimental/src/xenia/gpu/command_processor.cc)
- Non-square 1x2 / 2x1 scaling suggested as cheaper middle ground than 2x2. — [xenia.jp 2022-01](https://xenia.jp/updates/2022/01/29/presenting-the-presentation-update-amd-fidelityfx-fsr-fxaa-cas-vrr.html)
- Canary also exposes `custom_internal_display_resolution_x/y` (default 0; range up to 1920x1080) overriding the guest internal resolution. — [Canary graphics_system.cc](https://raw.githubusercontent.com/xenia-canary/xenia-canary/canary_experimental/src/xenia/gpu/graphics_system.cc)

### Inferences
- Lowest-GPU sensible Xenia-style config for HTH: render_target_path=fbo, draw scale 1x1 (or 2x1/1x2 instead of 2x2 if extra sharpness wanted), readback_resolve=none, readback_memexport=false, depth_float24_convert_in_pixel_shader=false, depth_float24_round=false, native_stencil_value_output=true, FSR present upscale, vsync/frame cap at target FPS. Current settings already match nearly all of this; remaining GPU load at scale 1 is mostly the game's own rendering + Xenia RT transfers, which needs profiling (e.g. Nsight/RenderDoc counts of transfer/resolve draws), not cvar changes.

### Gaps
- Searches for Reddit/Canary-wiki performance guides and game-patches/config for Hydro Thunder returned nothing relevant; no per-game config for title 5841096A was checked.
- Removed/renamed history: `d3d12_readback_resolve` (2021) → Canary `readback_resolve`; `query_occlusion_fake_sample_count` (master) → Canary `occlusion_query*`; `native_2x_msaa` (master) → `debug_msaa_2x_as_4x` (Canary); `gamma_render_target_as_srgb` → apparently replaced by `gamma_render_target_as_unorm16`. Exact commit dates not verified.
