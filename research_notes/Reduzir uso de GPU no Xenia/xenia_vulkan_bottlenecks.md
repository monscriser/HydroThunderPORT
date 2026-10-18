# Xenia Vulkan GPU-cost hotspots and upstream fixes (2022-2026), checked against the local ReXGlue SDK

Scope note: "local SDK" means `/root/ht/sdk` in WSL Ubuntu-24.04: rexglue-sdk `c94f5eb` (Release v0.10.0, 2026-08-21) plus this project's `rexglue-modifications.patch`. `provenance.json` names Xenia Edge `b2741d1` (2026-10-14) as the reference. Upstream lines checked: xenia-project/xenia master (Triang3l), xenia-canary/xenia-canary `canary_experimental`, has207/xenia-edge (Herman S., who also works in Canary) and the peerloomllc canary fork. Local checks were greps of the source, not runtime measurements.

## 1. How Xenia emulates EDRAM on Vulkan, and what it costs

### Takeaway
Vulkan has two paths. One uses host render targets (the "FB"/HostRenderTargets path, the default) with EDRAM range ownership transfers and resolves. The other uses fragment shader interlock (FSI, analogous to D3D12 ROV) to do blending and depth in shaders. On the host-RT path, the main architectural GPU costs are: ownership-transfer draws when a game reuses EDRAM with a different format, pitch or MSAA; resolve copies (EDRAM to shared memory) plus clears; float24 depth handling; and the barriers and render-pass breaks around all of these. In the local SDK the host-RT path is the default, and FSI is used only when `render_target_path_vulkan="fsi"`.

### Cited Findings
- The 2021 render target cache redesign (D3D12 first, and the architecture later reused for Vulkan) copies data only when an EDRAM range changes owner. That happens when the range is used with a different format, width or MSAA sample count. The old design copied through a linear buffer and the EDRAM buffer on every binding change. Copies are quads drawn with a runtime-generated pixel shader that reads the source RT, and only the exact range is copied. — [xenia.jp, "Leaving No Pixel Behind" (2021, historical)](https://xenia.jp/updates/2021/04/27/leaving-no-pixel-behind-new-render-target-cache-3x3-resolution-scaling.html)
- Measured GPU time (PIX, GTX 1070), old RT cache vs new RT cache vs ROV: Halo 3 lobby 79.8 / 23.5 / 52.5 ms; Halo 3 gameplay 34.1 / 13.6 / 23.8 ms; Banjo N&B menu 31.6 / 17.5 / 59.4 ms; Sonic 06 19.5 / 9.2 / 11.2 ms. The new host-RT cache beat ROV in every case shown. — [xenia.jp 2021](https://xenia.jp/updates/2021/04/27/leaving-no-pixel-behind-new-render-target-cache-3x3-resolution-scaling.html)
- Costs of ROV/FSI: no early or hierarchical Z, very long shaders, and slow compilation. AMD does not expose shader interlock on Vulkan. — [xenia.jp 2021](https://xenia.jp/updates/2021/04/27/leaving-no-pixel-behind-new-render-target-cache-3x3-resolution-scaling.html). The older Xenia wiki says the opposite, that RT copying has a "very heavy performance impact" and ROV is faster. That page predates the new RT cache and conflicts with the 2021 measurements. — [Xenia wiki ROV](https://github.com/xenia-project/xenia/wiki/ROV)
- Barriers in the old design were serial between copy, compute and draw (a Halo 3 menu frame took 80 ms in PIX). The new design needs a barrier only on the source RT. Stencil transfers on hardware without stencil export (Nvidia/Intel in D3D12) draw 8 quads, one per stencil bit. Depth transfers can use a "not equal" test to preserve depth compression, which is optional because it can raise bandwidth. — [xenia.jp 2021](https://xenia.jp/updates/2021/04/27/leaving-no-pixel-behind-new-render-target-cache-3x3-resolution-scaling.html)
- Draws without a viewport force conservative height estimates. That can make a draw appear to touch all of EDRAM and trigger expensive ownership round trips. The article lists this as an open problem. — [xenia.jp 2021](https://xenia.jp/updates/2021/04/27/leaving-no-pixel-behind-new-render-target-cache-3x3-resolution-scaling.html)
- Float24 depth: the host keeps a full-precision 32-bit float alongside the guest float24. `depth_float24_conversion` in the pixel shader is accurate but slow and is off by default. Vulkan can use `VK_EXT_depth_range_unrestricted`. — [xenia.jp 2021](https://xenia.jp/updates/2021/04/27/leaving-no-pixel-behind-new-render-target-cache-3x3-resolution-scaling.html)
- Vulkan design plan (Triang3l, issue opened 2022-05-30) has these elements:
  - Ownership transfers between host RTs.
  - Reusing images across same-size format classes through `imageViewFormatReinterpretation`, to skip transfers. `VK_KHR_image_format_list` is planned for this.
  - Two or four sample-masked draws per transfer when `sampleRateShading` is missing.
  - Resolve into shared memory, then clear.
  - At most 4 descriptor sets.
  - Planned "memory barrier batching" and "eliminating barriers and render pass breaks via range overlap analysis" as future work.

  — [xenia issue #2028](https://github.com/xenia-project/xenia/issues/2028)
- Triang3l's transfer commit says transfers still need work to "eliminate transfers between render targets within Vulkan format compatibility classes by using different views directly". — [xenia 0acb97d (2022-04-03) "[Vulkan] EDRAM range ownership transfers, resolve clears, 2x-as-4x MSAA"](https://github.com/xenia-project/xenia/commit/0acb97d38)
- Local SDK selects `Path::kPixelShaderInterlock` only when `render_target_path_vulkan == "fsi"`. The default `""` means host render targets. Other local defaults:

  | Setting | Local default |
  |---|---|
  | `direct_host_resolve` | true |
  | `depth_float24_convert_in_pixel_shader` | false |
  | `depth_transfer_not_equal_test` | true |
  | `vulkan_dynamic_rendering` | true |
  | `vulkan_sparse_shared_memory` | true |
  | `vulkan_submit_on_primary_buffer_end` | true |
  | `readback_resolve` | "none" |
  | `async_shader_compilation` | true |
  | `gamma_render_target_as_unorm16` | true |

  — local inspection of `/root/ht/sdk/src/graphics/vulkan/render_target_cache.cpp:40,203-215`, `src/graphics/flags.cpp`, `src/graphics/command_processor.cpp`, `src/graphics/pipeline/render_target/cache.cpp:40`

### Inferences
- Hydro Thunder at a 1280x720 internal resolution with FSR output: the GPU-side EDRAM emulation cost should come mostly from ownership transfers (for example, a depth buffer reinterpreted as color for water and reflection passes), resolves into the 512 MB shared memory buffer, and the clears that follow them. That is not proven for this game. A RenderDoc or Nsight capture counting transfer draws and resolve dispatches per frame is the way to confirm it.
- Switching to FSI on an RTX 5060 Ti is unlikely to lower GPU time, based on the 2021 numbers (FSI/ROV lost to host RTs in 4 of 4 cases). It is still a cheap A/B test (`render_target_path_vulkan="fsi"`).

### Gaps
- No public per-pass GPU timing for the Vulkan backend (as opposed to D3D12) was found.
- Whether the local SDK implements the "views within a format compatibility class" transfer skip was not verified line by line.

## 2. Known performance problems on the Vulkan backend

### Takeaway
The publicly documented problems are mostly about CPU/driver overhead and stalls that leave the GPU idle and then busy, not raw shader cost. They include a presenter pipeline recreated on every frame (with a CPU-GPU wait), per-draw descriptor-set writes for constant buffers, `vkCmdCopyBufferToImage` being slow on NVIDIA, full resolve readback memcpy, guest page-protection faults on Linux/Wine, and submissions held back while pipelines compile. Several of these are already fixed in the local SDK, and several are not.

### Cited Findings
- **Presenter pipeline churn (FIXED locally).** rexglue issue #507 (2026-10-12) reports that `GuestOutputPaintPipeline::swapchain_format` was never written. Every paint therefore waited for the previous paint submission, destroyed the swapchain pipeline and recreated it. The reporter measured about 115 pipeline creations per second on a static title screen, and 1 total after the fix (M3 Pro / MoltenVK). — [rexglue-sdk #507](https://github.com/rexglue/rexglue-sdk/issues/507). The local SDK already stores `swapchain_format` after creation. Its comment cites "~20 ms of UI-thread stall per presented frame in heavy scenes, halving the presented frame rate" and says the bug is "also missing upstream in Xenia's Vulkan presenter". — local `/root/ht/sdk/src/ui/vulkan/vulkan_presenter.cpp:2032-2056`
- **Per-draw descriptor writes for constant buffers (NOT fixed locally).** The peerloomllc fork (PR #57, merged 2026-09-29) made two changes:
  - Constant buffers became a fixed 64 KB range per upload page, with one descriptor set per page combination. New constants change only the dynamic offsets, instead of writing a new six-buffer descriptor set on every draw.
  - Type-0 packet and constant-load register writes are now done in bulk.

  Profile: Blue Dragon town, about 9,300 draws per frame. The driver's share of the GPU command thread dropped from 24% to 18%, and constant register writes had been 16% of that thread. Result: 39 → 42-44 FPS in the town; scenes already at 60 FPS were unchanged. — [peerloomllc/xenia-canary PR #57](https://github.com/peerloomllc/xenia-canary/pull/57). Xenia Edge landed an equivalent change: "[Vulkan] Bind the guest constant buffers as dynamic uniform buffers" (2026-08-28, no figure given). — [xenia-edge bfca9b2](https://github.com/has207/xenia-edge/commit/bfca9b2d9). The local SDK has no `VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC` and still writes `VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER` descriptors in `UpdateBindings`. — local `/root/ht/sdk/src/graphics/vulkan/command_processor.cpp` (~line 7120-7165)
- **NVIDIA buffer-to-image copy is slow (NOT in local SDK).** Xenia Edge commit text: "NVIDIA's vkCmdCopyBufferToImage runs at much lower rate than a shader writing the same bytes." The change writes the texture data into an integer staging image with a compute shader, then does an image-to-image copy so the texture keeps its compression. Reported result: "Noticeable FPS boost when resolution scaling, neutral at native res." — [xenia-edge a766c62 (2026-08-29)](https://github.com/has207/xenia-edge/commit/a766c62c7). The local SDK has no `texture_blit_scratch` shaders and still uses `vkCmdCopyBufferToImage` in `texture_cache.cpp` (local grep).
- **Full resolve readback.** In rexglue issue #447 (TTT2, RTX 3080, CachyOS), `readback_resolve=full` ran at roughly half game speed. GDB snapshots showed the GPU thread in `memcpy` inside `IssueCopy_ReadbackResolvePath`, and guest threads in `QueryProtect` reading `/proc/self/maps`. The issue was closed without a stated root cause. — [rexglue-sdk #447](https://github.com/rexglue/rexglue-sdk/issues/447). Canary introduced a double-buffered "fast" readback that reads the previous frame's buffers ("minimal performance impact"). — [canary e2c3368 (2025-10-31)](https://github.com/xenia-canary/xenia-canary/commit/e2c33686c). Local default is `readback_resolve="none"`, so this only matters if Hydro Thunder needs readback.
- **Page-protection overhead.** rexglue #515 (2026-10-15) reports that under Wine each guest page-protection change becomes a wineserver request. rexglue #441 says `memory::QueryProtect` may add significant overhead from GPU write-watch faults on Linux. Both are open. — [rexglue-sdk issues list](https://github.com/rexglue/rexglue-sdk/issues?q=is%3Aissue+performance+OR+gpu+OR+vulkan)
- **Submission held while pipelines compile.** Xenia Edge commit text: "Ending a submission was treated as unsafe while any pipeline was being created... `submit_on_primary_buffer_end` also held submissions back for as long as anything compiled." The fix stops waiting. — [xenia-edge 06fb359 (2026-10-10)](https://github.com/has207/xenia-edge/commit/06fb359de). The local SDK still has `CanEndSubmissionImmediately() { return !submission_open_ || (!scratch_buffer_used_ && !pipeline_cache_->IsCreatingPipelines()); }`. Submissions are therefore still deferred while async compilation runs. — local `/root/ht/sdk/src/graphics/vulkan/command_processor.cpp:5474-5475`
- **Occlusion query (ZPD) stalls (FIXED locally, project-specific).** The local patch comments say the previous ZPD emulation "stalled twice per frame: up to 9 ms per frame, the main cause of 120 -> 50 FPS drops near the portal lights". It is now a running counter with a one-frame delay. — local `rexglue-modifications.patch` (`src/graphics/vulkan/command_processor.cpp`). Canary and Edge made a similar rewrite: [canary 3d233a5 "[GPU] Rewrite ZPD as a running sample counter" (2026-09-04)](https://github.com/xenia-canary/xenia-canary/commit/3d233a5b2)
- **Vertex buffer residency cache, added and then removed.** Canary added it on 2025-12-31 ([3e1b0c7](https://github.com/xenia-canary/xenia-canary/commit/3e1b0c715)). oreyg removed it on 2026-05-15, with a commit titled "Drop vertex buffer residency cache, hoist global lock like D3D12" ([c2674b1](https://github.com/xenia-canary/xenia-canary/commit/c2674b19d)). rexglue #510 (open) reports that in-place-rewritten vertex buffers are never re-uploaded. The local SDK contains "residency" code in `command_processor.cpp` (grep). Its correctness should be checked against #510.
- **Canary state (community view).** In early 2025, users called native Vulkan "nowhere near on par with Windows/D3D12 builds". — [xenia-canary #518](https://github.com/xenia-canary/xenia-canary/issues/518). This is anecdotal and predates most of the 2025-2026 Vulkan work below.

### Inferences
- High GPU utilization at 720p on an RTX 5060 Ti may partly be a symptom rather than shader cost. A command processor that submits late, or a presenter that stalls, produces busy-idle patterns. The real test is GPU time per frame, not the utilization percentage.
- The texture-upload compute blit is the item most likely to lower NVIDIA GPU time when Hydro Thunder streams or invalidates textures, but only if uploads show up in captures. Edge says it is neutral at native resolution.

### Gaps
- No public measurement exists for Hydro Thunder specifically.
- No report was found on Vulkan descriptor-update cost on NVIDIA desktop (the PR #57 numbers come from one CPU-bound scene).

## 3. Upstream commits/PRs (2022-2026) that improved, or plausibly improved, Vulkan GPU/CPU cost, with local status

### Takeaway
Most of Triang3l's 2022 rewrite (barrier merging, descriptor-set consolidation, VMA, native unorm24 depth, unused shader I/O elimination) is inherited by rexglue. The 2025-2026 Canary/Edge work is mixed in the local SDK:

| Status in local SDK | Items |
|---|---|
| Present | direct host resolve, `vkPipelineCache`, shader storage, linear UNORM16 gamma RTs, readback modes |
| Missing | dynamic UBOs, NVIDIA compute-blit uploads, viewport-info caching, `vastcpy` uploads, "don't hold submissions while compiling", `draw_resolution_scale_threshold`, depth-bias-control |

### Cited Findings (Status: "Local" is from greps of `/root/ht/sdk`)

| Date | Commit/PR | What changed | Reported gain | Visual risk | Local status / portability |
|---|---|---|---|---|---|
| 2022-03-21 | [xenia 1259c9f "[Vulkan] Pipeline barrier merging"](https://github.com/xenia-project/xenia/commit/1259c9f7a) | Barriers are batched in the command processor; touched RT cache, shared memory, primitive processor | None stated | None | Inherited (the `PushImageMemoryBarrier`/`SubmitBarriers` pattern is used throughout the local code) |
| 2022-04-03 | [xenia 0acb97d](https://github.com/xenia-project/xenia/commit/0acb97d38) | EDRAM ownership transfers, resolve clears, 2x-as-4x MSAA (+4215 lines in vulkan_render_target_cache.cc) | Architectural (see the 2021 D3D12 numbers) | — | Inherited |
| 2022-06-20 | [xenia 0dc4807 "Render target resolving"](https://github.com/xenia-project/xenia/commit/0dc480721) | Vulkan resolves into shared memory | — | — | Inherited |
| 2022-06-22 | [xenia 4514050](https://github.com/xenia-project/xenia/commit/4514050f5) | Truncate depth to float24 in transfers and resolves by default; keeps the `>=` depth test stable | Correctness | Low | Inherited |
| 2022-07-03 | [xenia 2621dab "Native 24-bit unorm depth where available"](https://github.com/xenia-project/xenia/commit/2621dabf0); [001f648 "VMA for textures"](https://github.com/xenia-project/xenia/commit/001f64852) | Native D24 on AMD; texture suballocation | Fewer allocations | None | Inherited (NVIDIA lacks D24 on Vulkan, so float32 depth is used) |
| 2022-07-07/09 | [xenia e4de866](https://github.com/xenia-project/xenia/commit/e4de8663c), [b3edc56](https://github.com/xenia-project/xenia/commit/b3edc5657) | Uniform buffers in one set; texture and sampler descriptors merged; bound sets cut from 10 → 6 → 4; new linked-type descriptor set allocator | CPU binding overhead (not quantified) | None | Inherited |
| 2022-07-21 | [xenia 1a95bef](https://github.com/xenia-project/xenia/commit/1a95bef8b) | Remove unused shader I/O, UCP culling and centroid on Vulkan, "for more optimal usage of exports and the parameter cache on the host" | Not quantified | None | Inherited |
| 2022-05-22 | [xenia 6aa30ed](https://github.com/xenia-project/xenia/commit/6aa30ed07) | 128-thread groups in all texture load (untiling) shaders | — | None | Inherited |
| 2022-10-09 | [xenia 45050b2](https://github.com/xenia-project/xenia/commit/45050b238) | Vulkan FSI render-backend path | Alternative path | Some games | Inherited (opt-in `fsi`) |
| 2024-12 | [canary PR #451 "[Vulkan] Optimize shaders" (Xphalnos)](https://github.com/xenia-canary/xenia-canary/pull/451) | Internal shaders recompiled with a newer SDK and optimizer | "Small" FPS gain (screenshots only) | None | Check internal SPIR-V headers; cheap |
| 2025-08-19 | [xenia 0b2ffa3](https://github.com/xenia-canary/xenia-canary/commit/0b2ffa314) | Texture load cbuffer → push constants (D3D12 shader bytecode shown) | — | None | Unknown locally |
| 2025-10-31 | [canary e2c3368](https://github.com/xenia-canary/xenia-canary/commit/e2c33686c) | Double-buffered "fast" `readback_resolve` | "minimal performance impact" vs full | Possible one-frame-stale data | Present (string cvar `readback_resolve`) |
| 2025-12-01 | [canary 2ad412a "Add support for vkPipelineCache"](https://github.com/xenia-canary/xenia-canary/commit/2ad412a14) | Driver pipeline cache | Faster pipeline creation, less stutter | None | Present (`vkCreatePipelineCache` in pipeline_cache.cpp) |
| 2025-12-01 | [canary 4d15f0a "same vastcpy upload_buffer optimization as d3d12"](https://github.com/xenia-canary/xenia-canary/commit/4d15f0afe) | Faster CPU copy into the shared-memory upload buffer (+13/-4 lines) | CPU only | None | **Missing** (no `vastcpy` in the local tree); trivial port |
| 2025-12-22 | [canary 5897de3](https://github.com/xenia-canary/xenia-canary/commit/5897de3ea) | Sparse/tiled scaled resolve for >3x resolution scaling on Vulkan | Enables >3x | — | Not relevant at 1x |
| 2025-12-14 | [xenia fe1fd36](https://github.com/xenia-project/xenia/commit/fe1fd3613) | Simplified host GPU fence management (`GPUCompletionTimeline`) | Maintenance | None | Unknown |
| 2026-01-16 | [canary 64e51c5](https://github.com/xenia-canary/xenia-canary/commit/64e51c544), [5845f34](https://github.com/xenia-canary/xenia-canary/commit/5845f3437) | Vulkan shader disk storage; async shader compilation | Stutter, not average GPU time | Brief missing draws | Present (`async_shader_compilation`; local log restores 9 MB Vulkan cache) |
| 2026-01-17 / 2026-07-17 | [xenia cec9ca0](https://github.com/xenia-project/xenia/commit/cec9ca0ef) (D3D12) / [canary 8f55b4a](https://github.com/xenia-canary/xenia-canary/commit/8f55b4abf) (Vulkan) | 8-bit PWL gamma RTs stored as linear R16G16B16A16_UNORM, converted in transfers, clears and readbacks | Accuracy; "small GPU memory and bandwidth usage increase" | Improves blending | Present (`gamma_render_target_as_unorm16=true`). Turning it off could save bandwidth, but quality would get worse |
| 2026-05-15 | [canary 562aa0d](https://github.com/xenia-canary/xenia-canary/commit/562aa0dd9) | Declare only the clip- or cull-distance array that is actually written | Minor shader I/O | None | Unknown |
| 2026-07-16 | [canary 74db632 `draw_resolution_scale_threshold`](https://github.com/xenia-canary/xenia-canary/commit/74db632ab) | Per-surface scale: small-pitch post-process RTs render at 1x | Large when resolution scaling is on | Low (some chains break when upscaled anyway) | **Missing**; relevant only if `resolution_scale` > 1 (the project uses an integer `resolution_scale` for "Qualidade da imagem") |
| 2026-08-06 | [canary/edge 7101021 "Bind shared memory persistently for texture loads and resolves"](https://github.com/xenia-canary/xenia-canary/commit/710102115) | Fewer descriptor switches and barriers for texture loads and resolves | Not stated | None | Local has shared-memory descriptor sets; persistence not verified |
| 2026-08-06/07 | [edge aa2efea "Resolve host render targets directly into shared memory"](https://github.com/has207/xenia-edge/commit/aa2efeafa) (edram_dump_shader) | Skips the intermediate EDRAM buffer pass on resolve | Not stated | Low | Present (`direct_host_resolve=true` in `pipeline/render_target/cache.cpp`) |
| 2026-08-26 | [edge 2d0f197](https://github.com/has207/xenia-edge/commit/2d0f19746) | Resolve directly into the resolution-scaled destination layout | Only with scaling | — | Unknown; only matters when scale > 1 |
| 2026-08-28 | [edge bfca9b2](https://github.com/has207/xenia-edge/commit/bfca9b2d9); [peerloom PR #57](https://github.com/peerloomllc/xenia-canary/pull/57) | Dynamic uniform buffers for guest constants; bulk register writes | 39 → 42-44 FPS in a 9.3k-draw scene; driver 24% → 18% of the GPU thread | None reported | **Missing**; moderate port in `command_processor.cpp` |
| 2026-08-28 | [edge b308af2 "Cache host viewport info across draws"](https://github.com/has207/xenia-edge/commit/b308af27c) | Skips recomputing viewport info per draw (+7/-1) | CPU only | None | **Missing**; trivial |
| 2026-08-28 | [edge c8ac925](https://github.com/has207/xenia-edge/commit/c8ac9258f) | Scaled-resolve barriers pushed into the merger instead of recorded directly | Fewer barriers when scaling | None | Unknown |
| 2026-08-29 | [edge a766c62](https://github.com/has207/xenia-edge/commit/a766c62c7) | Compute-blit texture upload (NVIDIA) | "Noticeable FPS boost when resolution scaling, neutral at native res" | Low | **Missing**; large port (+552 lines in texture_cache plus 3 GLSL shaders) |
| 2026-09-29 | [edge 1222c23](https://github.com/has207/xenia-edge/commit/1222c23f7) | Memexport waits only on the last export's submission, not all queue work | Fewer CPU-GPU stalls | None | Unknown |
| 2026-10-10 | [edge 06fb359](https://github.com/has207/xenia-edge/commit/06fb359de), [8a4d405](https://github.com/has207/xenia-edge/commit/8a4d405af) | Stop waiting for pipeline creation before ending a submission; stand-in decisions | Less GPU starvation while compiling | Draws skipped until pipelines are ready | **Missing** (`CanEndSubmissionImmediately` still checks `IsCreatingPipelines`); small-to-moderate port |
| 2026-08-01 / 09-30 | [canary 1fdbe56](https://github.com/xenia-canary/xenia-canary/commit/1fdbe569e), [c8f214e](https://github.com/xenia-canary/xenia-canary/commit/c8f214e2a) / [peerloom PR #62](https://github.com/peerloomllc/xenia-canary/pull/62) | Restore a dropped TRANSFER_WRITE → TRANSFER_WRITE barrier for double uploads | Correctness; FPS unchanged (~48) | Fixes hazards | Port for correctness |
| 2026-10-11 | [canary a8d034c](https://github.com/xenia-canary/xenia-canary/commit/a8d034c00) | Depth bias through VK_EXT_depth_bias_control | Accuracy | — | Missing; not a performance item |

### Inferences
- Ranked by expected payoff for a GPU-utilization complaint at 1x internal resolution on NVIDIA:
  1. Hold-submission-while-compiling fix (06fb359). It removes GPU idle-then-burst gaps.
  2. Dynamic UBOs (bfca9b2 / PR #57). This is a CPU/driver saving, so it raises FPS when uncapped but does not reduce GPU work per frame.
  3. Compute-blit uploads (a766c62). This only helps if texture uploads are frequent.
  4. `vastcpy` and viewport cache. Trivial CPU-side changes.

  None of these is documented to cut GPU shader time at native resolution. If FPS is uncapped, high GPU usage may simply mean the GPU is the bottleneck, which is expected. Capping to 60/120 FPS is the direct lever.
- `draw_resolution_scale_threshold` becomes the most valuable item if the "Qualidade" presets use `resolution_scale` > 1, because post-process chains then stop rendering at the scaled resolution.

### Gaps
- None of the Edge commits above report numeric gains except a766c62 (qualitative) and PR #57.
- No 2022-2026 commit claiming a measured reduction in GPU (as opposed to CPU) time on the Vulkan host-RT path at native resolution was found.
- The GitHub search API returned 0 commits for the Canary fork (forks are not indexed). The Canary list above comes from path-filtered commit listings since 2022-11, so smaller commits outside `gpu/vulkan`, `ui/vulkan`, `render_target_cache.cc` and `spirv_shader_translator_rb.cc` may be missing.

## 4. What ReXGlue itself says about its GPU backend and performance

### Takeaway
The rexglue README gives no GPU-backend or performance notes. It describes the project as early-stage and "heavily rooted on the foundations of" Xenia. The issue tracker (Sep-Oct 2026) has several Vulkan performance and correctness issues, and #507 is directly about performance.

### Cited Findings
- The README credits Xenia, XenonRecomp and rexdex's recompiler. It says the project is in early development with breaking API changes, and lists no GPU performance notes. — [rexglue-sdk README](https://github.com/rexglue/rexglue-sdk)
- The mintlify docs list Windows x64 with D3D12 and Linux x64 with Vulkan. — [rexglue mintlify](https://www.mintlify.com/rexglue/rexglue-sdk). The local build uses Vulkan on Windows (`render_target_path_vulkan` and the `vulkan_*` cvars are active).
- Relevant open issues (Oct 2026):

  | Issue | Topic | State |
  |---|---|---|
  | #507 | Presenter pipeline recreated every frame | Fixed locally |
  | #508 | Vulkan FBO path: color resolve reads the depth RT after memexport draws | Open |
  | #510 | Stale in-place vertex buffers | Open |
  | #515 | Wine page-protection overhead | Open |
  | #441 | QueryProtect overhead on Linux | Open |
  | #486, #474 | Tessellation descriptor-set and binding bugs | Open |
  | #398 | Loading librenderdoc.so disables Wayland | Open |

  — [rexglue-sdk issues](https://github.com/rexglue/rexglue-sdk/issues?q=is%3Aissue+performance+OR+gpu+OR+vulkan)
- rexglue cvars present locally that are relevant to GPU cost: `render_target_path_vulkan`, `direct_host_resolve`, `depth_transfer_not_equal_test`, `vulkan_dynamic_rendering`, `vulkan_sparse_shared_memory`, `vulkan_submit_on_primary_buffer_end`, `gamma_render_target_as_unorm16`, `readback_resolve`, `occlusion_query_enable`, `resolution_scale`/`draw_resolution_scale_x/y`, `texture_cache_memory_limit_*`, `async_shader_compilation`, `fps_limit`. — local grep of `REXCVAR_DEFINE_*` in `/root/ht/sdk/src/graphics`

### Inferences
- rexglue v0.10.0 (Aug 2026) already contains Edge/Canary features through mid-2026: direct host resolve, readback modes, shader storage, `vkPipelineCache`, ZPD and the gamma format. It does not contain Edge's late-August to October 2026 Vulkan CPU/stall work. Those Edge commits are the most natural port source, because rexglue tracks Edge (provenance `referenceXeniaEdge`).

### Gaps
- Page 2 of the rexglue issue search and its PR list were not reviewed.
- The rexglue wiki and docs were not read for GPU notes.

## 5. GPU profiling methodology used by Xenia developers

### Takeaway
Triang3l measured with PIX GPU timings (D3D12) and built a Vulkan trace viewer and trace dump tool. Herman S. (Edge/Canary) added profiler instrumentation and GPU timestamp queries to the caches and draw paths. Fork authors use frame-pointer CPU profiles plus the Vulkan synchronization validation layer. RenderDoc is supported (rexglue has a RenderDoc-loading path).

### Cited Findings
- The 2021 article's numbers are "PIX GPU time only" on a GTX 1070 / driver 466.11. — [xenia.jp 2021](https://xenia.jp/updates/2021/04/27/leaving-no-pixel-behind-new-render-target-cache-3x3-resolution-scaling.html)
- Triang3l's Vulkan tooling: "[Vulkan] Trace dump tool" (2022-06-13) and "[Vulkan] Trace viewer" (2022-07-01). — [xenia commits search, 127bf34 / 636585e](https://github.com/xenia-project/xenia/commit/636585e0a). The design issue plans "frame tracing with GPU downloads, RenderDoc capture". — [xenia #2028](https://github.com/xenia-project/xenia/issues/2028)
- Edge: "[Metal/Vulkan/Profiling] Instrument caches and draw paths" (2026-08-07) and "Recycle Vulkan GPU time query slots before each submission" (2026-08-09). — [edge 6590128](https://github.com/has207/xenia-edge/commit/659012897), [edge 0db64ec](https://github.com/has207/xenia-edge/commit/0db64ec3a)
- peerloom fork methods:
  - A frame-pointer CPU profile showing the driver's share of the GPU command thread. — [PR #57](https://github.com/peerloomllc/xenia-canary/pull/57)
  - Vulkan sync validation counting write-after-write hazards (44 → 0). — [PR #62](https://github.com/peerloomllc/xenia-canary/pull/62)
- rexglue loads librenderdoc.so unconditionally on Linux (side effect: Wayland disabled). — [rexglue-sdk #398](https://github.com/rexglue/rexglue-sdk/issues?q=is%3Aissue+performance+OR+gpu+OR+vulkan)
- The local SDK already has project-specific `[ht-gpu]`/`[ht-present]` timers in `src/graphics/vulkan/command_processor.cpp`. — local grep

### Inferences
- Suggested method for Hydro Thunder: RenderDoc or Nsight Graphics capture of one in-race frame, counting:
  - ownership-transfer draws (fullscreen quads with the transfer shaders),
  - resolve dispatches and clears,
  - texture-load dispatches and `vkCmdCopyBufferToImage` calls,
  - render-pass breaks.

  Pair the capture with Nsight Systems to see idle gaps on the GPU queue. That would show whether the 5060 Ti's high utilization comes from EDRAM emulation overhead or from the game's own shading, at 2560x1440 (FSR) plus 720p.

### Gaps
- No public document lists "hot passes" measured on the Vulkan backend.
- No Xenia developer blog post about Vulkan performance from 2022-2026 was found (the xenia.jp article is from 2021).
- No Reddit or Discord summaries were reviewed.
