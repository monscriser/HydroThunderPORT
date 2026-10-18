# Vendor-, presentation- and upscaler-specific ways to lower GPU usage (Xenia-derived Vulkan port, NVIDIA Windows + AMD RADV handhelds)

Context: internal 1280x720, FSR1 (EASU+RCAS) or FSR 3.1 (no game motion vectors) to 1080p/1440p, 60 FPS cap or uncapped; RTX 5060 Ti. Measured by the project: ~46% GPU at 120 FPS 1080p FSR1, 99% at 85 FPS 1080p native. Research done 2026-10-15; 15 tool calls; many sources are forum-grade and flagged as such.

## NVIDIA: FSI vs host render targets, driver settings, and what "utilization %" means

### Takeaway
No public benchmark of Vulkan FSI cost on NVIDIA in Xenia was found. Xenia's own docs say the host-RT path pays a heavy EDRAM copy cost, and the interlock path pays for doing blending and depth in shaders. You have to A/B it per game. Of the driver settings, only the present method and the max frame rate setting plausibly affect a Vulkan app. Low Latency Mode does not apply to Vulkan. GPU "utilization %" is time-busy at the *current* clock, so read it next to clocks, board power and frame time. On its own it is a misleading metric.

### Cited Findings
- Xenia uses VK_EXT_fragment_shader_interlock for guest output-merger emulation (custom pixel formats), via `--render_target_path_vulkan=fsi`. The D3D12 equivalent is `--render_target_path_d3d12=rov`. — [Mesa MR 22250 (RADV FSI)](https://gitlab.freedesktop.org/mesa/mesa/-/merge_requests/22250)
- Xenia wiki (last edited 2021-08-05, **outdated**): the RT path copies between host RT textures and the 10 MB EDRAM buffer, with a "very heavy performance impact". ROV renders directly into the EDRAM buffer and does blending and depth/stencil in pixel shaders, removing the copies. The page header contradicts itself ("Improves accuracy at the cost of performance … Recommended to be disabled"), so it is ambiguous. The minimum NVIDIA hardware listed is Maxwell 2 (GTX 950). — [Xenia wiki: ROV](https://github.com/xenia-project/xenia/wiki/ROV)
- On Intel D3D12, Xenia Canary defaults to ROV because of an old driver bug. A user reported RTV renders correctly and is "much faster in my case". This was Intel, not NVIDIA, but it shows the faster path is vendor- and game-dependent. — [xenia-canary issue #542](https://github.com/xenia-canary/xenia-canary/issues/542)
- The Khronos FSI spec provides a critical section and ordering guarantees for overlapping fragments. It makes no performance statement. — [Khronos VK_EXT_fragment_shader_interlock](https://registry.khronos.org/vulkan/specs/1.3-extensions/man/html/VK_EXT_fragment_shader_interlock.html)
- Our SDK already branches on `edram_fragment_shader_interlock`, as part of the pipeline storage magic in `rexglue-modifications.patch` (line ~2500). So both paths exist in the port. — local file `rexglue-modifications.patch`
- **Low Latency Mode (NVCP)** launched for DX9/DX11 only. In DX12/Vulkan "the game decides when to queue frames", so the setting has no effect. The sources are 2019 NVIDIA wording quoted by forums plus a Khronos thread, so current status is unconfirmed. — [Blur Busters forum](https://forums.blurbusters.com/viewtopic.php?p=44688); [Khronos community](https://community.khronos.org/t/where-in-the-pipeline-does-nvidias-low-latency-mode-kick-in/106486/2); [tier1settings](https://tier1settings.com/nvidia-low-latency-mode-explained/)
- **Vulkan/OpenGL present method** (Auto / Prefer native / Prefer layered on DXGI swapchain): the layered option routes Vulkan presentation through DXGI flip model. A Ryujinx PR reported lower present latency and VRR support with layered, while noting NVIDIA's tooltip warns of possible overhead. An RTX 4090 user found Auto→Native removed a high vkQueuePresentKHR time. Another user found layered fixed stutter in a different game. All anecdotal and game-dependent; there is no official NVIDIA explanation. — [Ryujinx PR 6645](https://git.tdem.in/Ryujinx/Ryujinx/pull/6645); [NVIDIA forum: vkQueuePresentKHR blocks](https://forums.developer.nvidia.com/t/vkqueuepresentkhr-switches-to-dx12-context-and-blocks-gpu-workload/312536); [NVIDIA forum: DXGI present method](https://forums.developer.nvidia.com/t/use-of-dxgi-swapchain-vulkan-opengl-present-method/329070); [Khronos forum native vs layered](https://community.khronos.org/t/vulkan-present-mode-preferred-native-vs-layered-on-dxgi-swapchain/112055)
- An NVIDIA forum thread reports a GDI copy path when a Vulkan swapchain was never created with FIFO mode. The title alone suggests non-FIFO present modes can take a costlier copy path on NVIDIA Windows. — [NVIDIA forum](https://forums.developer.nvidia.com/t/copy-w-gpu-gdi-when-vulkan-swapchain-wasnt-ever-made-with-fifo-mode/325429) (only the title/snippet was seen, not the full thread)
- **Power management mode**: per community explainers, "Prefer maximum performance" keeps 3D clocks at max regardless of load. "Optimal power" (the default since ~driver 368) behaves adaptively and reportedly reuses the last frame when the screen is static. Idle behavior is disputed and there is no official NVIDIA doc. Inference for us: "Prefer maximum performance" raises power and makes "utilization %" look *lower*. It is the opposite of what we want for power. — [ROG forum](https://rog-forum.asus.com/t5/nvidia-graphics-cards/question-about-optimal-power-and-fps-drops-on-gtx-1080/td-p/667177); [MSFS forum observations](https://forums.flightsimulator.com/t/nvidia-power-managment-mode-observations/476571); [Blur Busters](https://forums.blurbusters.com/viewtopic.php?p=93976)
- **Utilization % semantics**: NVIDIA's utilization metric is "the fraction of a sampling period during which at least one kernel was executing". It does not measure how much of the GPU's capacity is used. — [Netdata guide](https://www.netdata.cloud/guides/nvidia-gpu/nvidia-gpu-utilization-misleading/)
- The usage figure ignores power state and clock. A 24% reading at 202.5 MHz is ~2.6% of capacity at the 1823 MHz max. To compare with true capability, scale the percentage by current clock / max clock. — [Wallpaper Engine help](https://help.wallpaperengine.io/en/performance/gpu.html); [Hyperlogos: NVIDIA instrumentation](https://hyperlogos.org/blog/drink/nvidia-instrumentation-lies-lot)
- Recommended practice: log FPS, frame times/1% lows, core clock, board power and temperature together in a repeatable scene. Tools (HWiNFO vs GPU-Z) can disagree on utilization at low load. — [Evetech](https://evezone.evetech.co.za/build-lab/gpu-usage-stuck-at-60-percent-games-find); [HWiNFO forum](https://www.hwinfo.com/forum/threads/wrong-gpu-clock-speed.826/)

### Inferences
- Our "~46% at 120 FPS FSR1" is probably measured at a reduced P-state clock. That is fine and not evidence of waste. The right efficiency metric is **board power (W) and GPU clock at a fixed FPS in the same race scene** (e.g. log `nvidia-smi --query-gpu=power.draw,clocks.gr,clocks.mem,pstate,utilization.gpu --format=csv -lms 500` alongside `[ht-present]`). Compare configs by watts at a fixed 60 FPS cap, not by %.
- The 99% at 85 FPS for native 1080p vs 120 FPS with FSR1 implies that internal shading cost (Xenia translated shaders + EDRAM emulation), not the upscaler, dominates GPU time.
- FSI vs host-RT should be A/B tested in-race on the 5060 Ti, measuring watts at 60 FPS and uncapped FPS. Visual risk: host-RT can cause transparency/shadow-acne/format issues (per wiki). Check water/translucent surfaces in Hydro Thunder.
- Low Latency Mode, threaded optimization and similar NVCP toggles are not worth exposing to Ian for a Vulkan app. The present method and NVCP Max Frame Rate are the only plausibly relevant ones.

### Gaps
- No measured numbers for FSI cost on NVIDIA Ada/Blackwell in Xenia (or any app) were found.
- No official NVIDIA doc for the present method, power modes, or shader cache size effects on Vulkan.
- I did not read the full "GDI copy without FIFO" NVIDIA thread; whether it affects our swapchain is unverified.

## AMD / Mesa RADV (Steam Deck, ROG Ally on CachyOS): env vars, Gamescope, power

### Takeaway
Most historically useful RADV perf toggles are now defaults: NGG culling on RDNA2 since Mesa 21.3, ACO as the compiler, GPL by default. The remaining RADV_PERFTEST options are niche. For power on handhelds, the big levers are a frame cap *plus* a lowered panel refresh, plus TDP/GPU clock limits. Expect Xenia-class workloads to be heavy on the Deck. Xenia Canary has native Linux builds, but performance reports are mixed.

### Cited Findings
- The current Mesa envvars page lists these RADV_PERFTEST options: `cswave32`, `dccmsaa`, `dmashaders` ("upload shaders to invisible VRAM (might be useful for non-resizable BAR systems)"), `gewave32`, `localbos`, `nggc` ("enable NGG culling for GFX11+"), `nircache`, `nogttspill`, `nosam`, `pswave32`, `rtwave64`, `sam` ("enable optimizations to move more driver internal objects to VRAM"). `gpl` is no longer a perftest flag. Instead `RADV_DEBUG=nogpl` exists ("disable VK_EXT_graphics_pipeline_library"), which implies GPL is on by default. — [Mesa docs envvars](https://docs.mesa3d.org/envvars.html)
- RADV_DEBUG flags that can *diagnose* (not improve) performance: `nodcc`, `nodisplaydcc`, `nohiz`, `nofastclears`, `nobinning`, `nongg`, `nonggc` (GFX10/10.3), `nocache`. Disabling compression/HiZ normally costs bandwidth, so these are for A/B debugging only. — [Mesa docs envvars](https://docs.mesa3d.org/envvars.html)
- `MESA_VK_WSI_PRESENT_MODE` overrides the app's present mode: `fifo`, `relaxed`, `mailbox`, `immediate`. — [Mesa docs envvars](https://docs.mesa3d.org/envvars.html)
- `MESA_SHADER_CACHE_MAX_SIZE` defaults to 1 GB. — [Mesa docs envvars](https://docs.mesa3d.org/envvars.html)
- `ACO_DEBUG` options are all debug or opt-out (`novn`, `noopt`, `nosched`…). There is nothing to "enable" for speed, since ACO is the default compiler. — [Mesa docs envvars](https://docs.mesa3d.org/envvars.html)
- NGG culling became the default on RDNA2 (GFX10.3) in Mesa 21.3, with `RADV_DEBUG=nonggc` as an opt-out. NGG was also enabled for Van Gogh (Steam Deck APU). On RDNA1 the gain was "a wash". These are 2021 sources, but still the baseline. — [Phoronix Mesa 21.3](https://www.phoronix.com/news/Mesa-21.3-RADV-NGGC-Def); [Mesa MR 13086](https://gitlab.freedesktop.org/mesa/mesa/-/merge_requests/13086); [Phoronix NGG VanGogh](https://www.phoronix.com/news/RADV-NGG-Enable-VanGogh-APUs)
- Inference from that page: Steam Deck (RDNA2) and ROG Ally Z1/Z1E (RDNA3, GFX11) already get NGG culling by default. The perftest `nggc` text "for GFX11+" suggests it may not be default on all GFX11, but the docs do not state defaults. — [Mesa docs envvars](https://docs.mesa3d.org/envvars.html)
- RADV gained fragment shader interlock (2023, Mesa MR 22250), explicitly citing Xenia as a user. — [Mesa MR 22250](https://gitlab.freedesktop.org/mesa/mesa/-/merge_requests/22250); [GamingOnLinux 2023-06](https://www.gamingonlinux.com/2023/06/amd-radv-gets-feature-to-help-emulation-and-translation-layers/page=1/)
- `vk_xwayland_wait_ready` (driconf, Mesa 21.3): when false, X11/XWayland WSI does not wait on buffer fences before submitting. Valve pushed for this for Steam Deck latency and pipelining. MR 27074 (Hans-Kristian Arntzen) disables the wait by default. I could not confirm which release shipped it. Gamescope WSI bypasses XWayland anyway. — [Phoronix](https://www.phoronix.com/news/Mesa-Vulkan-WSI-No-Wait-XWay); [Mesa MR 11290](https://gitlab.freedesktop.org/mesa/mesa/-/merge_requests/11290); [Mesa MR 27074](https://gitlab.freedesktop.org/mesa/mesa/-/merge_requests/27074); [Mesa MR 21602](https://gitlab.freedesktop.org/mesa/mesa/-/merge_requests/21602)
- **Steam Deck frame limiter**: the cap options are tied to refresh (1/4, 1/2, 1x, off). Setting 40 Hz unlocks a 40 FPS cap. A cap reduces GPU load/power, but the panel still refreshes at 60 Hz, so lowering refresh too saves more. Digital Trends: limiter plus lower refresh together can add "an extra hour or more" of play. — [SDHQ settings encyclopedia](https://steamdeckhq.com/tips-and-guides/the-sdhq-performance-settings-encyclopedia/); [Steam community](https://steamcommunity.com/app/1675200/discussions/0/3877092663697984244); [Digital Trends](https://digitaltrends.com/?p=3102464)
- The SteamOS system-level frame limiter was measured to add significant input latency (Rogue Legacy 2). The workaround cited is matching refresh to the cap. — [HotHardware](https://hothardware.com/news/steam-deck-fps-limit-adds-input-latency)
- Half-rate shading saves power but halves shading quality and harms fine text. That breaks our "no fidelity loss" constraint and would hurt the HUD numbers. — [Evetech Deck tricks](https://evezone.evetech.co.za/build-lab/steam-deck-advanced-battery-tricks-half-rate-shading)
- TDP practice: watch the performance overlay's live wattage and set TDP/GPU clock just above what holds the target FPS. Example: a 2D game held 60 FPS at 5 W. — [SDHQ](https://steamdeckhq.com/tips-and-guides/the-sdhq-performance-settings-encyclopedia/)
- Xenia on Deck: Canary/Edge have native Linux builds; Vulkan is recommended. Older guides (How-To Geek, 2023) described Proton-run Xenia on SteamOS as "awful", with glitches. A Deck guide estimates ~60% of tested games playable at 15–60 FPS, but the figure is unsourced. — [GameTechWiki XeniOS/Xenia](https://emulation.gametechwiki.com/index.php/XeniOS); [How-To Geek](https://www.howtogeek.com/882131/how-to-emulate-xbox-360-games-on-your-steam-deck/); [boundbyflame](https://boundbyflame.com/?p=11922)
- Valve shipped Mesa changes to Deck that reduced the shader cache size and enabled a pipeline feature by default. Snippet only. — [SteamDeckHQ](https://steamdeckhq.com/news/mesa-driver-pipeline-and-reduced-shader-cache-size/)

### Inferences
- For our Linux build, no RADV env var is a clear "free win". Leave RADV defaults and do not ship `RADV_PERFTEST`/`RADV_DEBUG` presets. Possibly document `RADV_PERFTEST=sam` / `nosam` and `dmashaders` as experiments only.
- On the handhelds, the greatest power saving without fidelity loss is a 60 FPS cap at 60 Hz (or 40/40 or 45/90 where the game logic allows). Our "Sem limite" mode is the worst case for battery and thermals. Our own frame limiter (engine-level) is preferable to the SteamOS limiter for latency.
- `MESA_VK_WSI_PRESENT_MODE=fifo` can be a user-side fallback if our app picks mailbox/immediate under Gamescope.

### Gaps
- No RADV/Steam Deck/Ally benchmarks of Xenia's FSI vs host-RT paths were found.
- No authoritative Xenia Canary Linux recommended-settings page for RADV; the mintlify docs are unofficial mirrors.
- No measured watts for 40 Hz vs 60 Hz on Deck from a primary source.
- Phoronix FSI article returned 403; RADV FSI performance characteristics remain unverified.

## Presentation: present modes, frame limiting, uncapped FPS

### Takeaway
FIFO with a frame cap at or below refresh is the power-friendly configuration: the GPU idles between vsyncs and DVFS lowers clocks. Mailbox and immediate with no throttle render frames that are never displayed. This is pure wasted GPU work on a 60 Hz display. An engine-level limiter has ~0.5–1 frame less latency than external ones (NVCP/RTSS). Keeping GPU load below ~99% avoids render-queue latency without needing Reflex.

### Cited Findings
- FIFO: when the GPU is fast, the queue fills and both CPU and GPU idle until vsync. "No unnecessary work is performed". DVFS detects the slack and lowers frequencies "at no performance cost". FIFO is recommended unless low latency is needed. — [Samsung/Arm Vulkan best practice (2019, still valid)](https://developer.samsung.com/sdp/blog/en/2019/07/26/vulkan-mobile-best-practice-how-to-configure-your-vulkan-swapchain)
- Mailbox: a newly rendered frame replaces the queued one, and the discarded frames are wasted work. Unthrottled, one processor may be fully utilized, resulting in higher power consumption. — [Samsung/Arm](https://developer.samsung.com/sdp/blog/en/2019/07/26/vulkan-mobile-best-practice-how-to-configure-your-vulkan-swapchain)
- A wgpu-based project switched its default to Mailbox for lower input lag, acknowledging "useful-but-discarded work" each redundant frame, and kept FIFO for battery/heat-conscious users. — [IfcOpenShell commit](https://dev.glassvision.dedyn.io/Glassvision/IfcOpenShell/src/commit/edc598357bf7071e7d4061baa20a8d596a3021ad)
- Non-FIFO modes can keep presenting at high rates even when the window is hidden, which wastes GPU and power. The fix is to throttle when minimized or occluded. — [Dear ImGui issue #9078](https://upd.dev/ocornut/imgui/issues/9078)
- Engine-level limiters are typically "1/2 to 1 frame lower latency" than external limiters (RTSS, NVCP Max Frame Rate). RTSS gives slightly steadier frametimes. A limit that keeps GPU below 99% makes Reflex "largely unnecessary". Reflex itself acts as a dynamic engine-level cap slightly below the sustainable rate. Forum-grade (Blur Busters, jorimt). — [Blur Busters](https://forums.blurbusters.com/viewtopic.php?p=97250); [Blur Busters](https://forums.blurbusters.com/viewtopic.php?p=85949); [Guru3D](https://forums.guru3d.com/goto/post?id=6084839)
- Xenia Canary defaults: `vsync=true`; `framerate_limit=60` (0 = unlimited). Unofficial mirror of the docs. — [mintlify mirror of Xenia config reference](https://www.mintlify.com/emoose/xenia/reference/configuration)

### Inferences
- Uncapped on a 60 Hz display with mailbox/immediate at 187 FPS (our rev8 proof) means roughly 2 of every 3 rendered frames are never shown. GPU power then scales with FPS, not with visible output. "Sem limite" should be described as a high-power/experimental mode. It is useful only on high-refresh/VRR displays or for frame generation input.
- For the lowest GPU at a given smoothness: FIFO (or FIFO-relaxed to avoid a hard drop under misses) plus our in-app limiter at the display refresh. On a 120/144 Hz monitor, capping at 120 with FIFO is still far cheaper than uncapped.
- Prefer our own (engine-level) limiter over the NVCP Max Frame Rate or SteamOS limiter for latency. Driver caps are a reasonable fallback but add ~0.5–1 frame of latency.
- Add a minimized/occluded throttle if not already present.

### Gaps
- No vendor-official numbers quantifying watts saved by FIFO vs mailbox on NVIDIA desktop.
- I did not verify whether our swapchain currently picks mailbox, immediate or FIFO under `vsync=true` + `fps_limit=0` (the rev8 notes say vsync keeps a virtual clock, not the present mode). This needs a code check.

## Upscaler cost: FSR1 / FSR 3.1 / NIS / CAS / FXAA / integer scaling

### Takeaway
FSR1 is a cheap single-pass spatial upscaler (EASU, plus optional RCAS or a fused EASU_RCAS pass). AMD gives no per-pass ms table on the pages I fetched. FSR 3.1 is costlier than 3.0 and allocates tens of MB at 1440p; without motion vectors it gives no temporal benefit. So FSR1 (ideally the fused EASU+RCAS pass) is the efficient default. The GPU savings from changing the upscaler are small compared with internal render cost.

### Cited Findings
- FSR1 has three passes: EASU, a fused EASU_RCAS pass, and RCAS. Quality modes are 1.3x/1.5x/1.7x/2.0x per dimension. — [GPUOpen FSR1](https://gpuopen.com/?p=61889); [GPUOpen FidelityFX FSR1](https://gpuopen.com/?p=58244)
- FSR1 should be applied after anti-aliasing and tone mapping, before noise-introducing post (film grain, chromatic aberration) and before UI. Input should be anti-aliased, noise-free and perceptual [0–1]. Negative input to RCAS gives NaN. — [GPUOpen FSR1 manual](https://gpuopen.com/manuals/fidelityfx_sdk/fidelityfx_sdk-page_techniques_super-resolution-spatial/)
- FSR 3.1 upscaler: "performance is slightly lower than FSR3.0 upscaler, in exchange for higher upscaling quality". Memory at 2560x1440: Quality 106 MB working set (89 persistent), Performance 61 MB. At 1920x1080: Quality 61 MB, Performance 40 MB. These were measured on an RX 6700 XT in DX12. Quality improves "when more objects provide their motion vectors". Without a reactive mask, a 1x1 cleared texture is used. — [GPUOpen FSR 3.1 upscaler manual](https://gpuopen.com/manuals/fidelityfx_sdk/techniques/super-resolution-upscaler/)
- FSR1 on an RX 6800 at QHD: native 93 FPS average, with up to +74% from FSR modes. That is whole-game gain, not upscaler cost (HEXUS, June 2021, **outdated**). — [HEXUS](https://hexus.net/tech/features/graphics/147984-amds-fidelityfx-super-resolution-fsr-tested/?page=3)

### Inferences
- If our FSR1 path runs EASU and RCAS as two dispatches, the fused EASU_RCAS pass saves one full-resolution write+read at 1440p. Bandwidth matters on the Deck/Ally APUs. No visual change.
- FSR 3.1 without motion vectors costs more GPU and memory than FSR1 for the same (spatial-like) result. This matches the project's own finding that they "tie". Keeping FSR1 as the default is correct for power.
- Rendering internally at 720p and presenting with plain bilinear, or integer scaling where the output is 1440p (exact 2x), saves only the upscaler pass. It is likely a small fraction of a millisecond on the 5060 Ti, at a fidelity cost (softer/blockier). Integer 2x at 1440p is the one fidelity-neutral-ish option worth an A/B, since it keeps the HUD pixels crisp with no ringing.
- FXAA at 720p is cheap but blurs the HUD (already found by the project). Keeping it off is both cheaper and sharper.
- NIS/CAS: no sourced cost numbers were found in this pass. They are comparable single-pass spatial filters, so no meaningful saving over FSR1 is expected.

### Gaps
- No AMD/GPUOpen per-pass ms table for EASU/RCAS at 1440p, and no FSR 3.1 ms table (the performance section of the manual was beyond the fetched portion; offset 100000 not read).
- No sourced NIS/CAS cost or FXAA cost figures.
- We should profile our own dispatch timings (e.g. Vulkan timestamp queries around the guest-output passes) rather than rely on vendor tables.

## Hydro Thunder Hurricane (5841096A) on Xenia

### Takeaway
The single Xenia Canary compatibility issue (#738) marks the game **Playable** (as of 2026-09-19) on D3D12. It notes stutters, lights visible through walls, which are fixed by `occlusion_query = "strict"`, and Vulkan-specific missing geometry and black voids (2025 report). No FPS/hardware data, water/reflection notes, or patches are listed.

### Cited Findings
- Issue #738 "5841096A - Hydro Thunder Hurricane", opened 2025-07-09 (Canary d3fb398, Windows, D3D12). Labels: `gpu-occlusion-query`, `state-playable` (raised from gameplay on 2026-09-19 after a tester got gold in every event). — [xenia-canary/game-compatibility #738](https://github.com/xenia-canary/game-compatibility/issues/738)
- Reported D3D12 issues: "noticeable stutters" and lights showing through walls, attributed to a missing feature. Vulkan at the time had missing geometry and black voids. — [#738](https://github.com/xenia-canary/game-compatibility/issues/738)
- The fix suggested on 2026-08-18 is `occlusion_query = "strict"` for lights through walls. The new audio decoder prevented audio crashes (audio label later removed). — [#738](https://github.com/xenia-canary/game-compatibility/issues/738)
- The game uses ZPD (Z-pass-done) occlusion queries (label `gpu-occlusion-query`). — [#738](https://github.com/xenia-canary/game-compatibility/issues/738)

### Inferences
- Occlusion queries matter for this title. "Strict" occlusion queries are more accurate but may add GPU sync and cost. If our port fakes queries (always-visible), the game may draw more lights/flares than needed, increasing GPU load. Strict mode might reduce overdraw but add stalls. Worth checking which mode our SDK uses and A/B testing GPU watts in Asgard.
- The "stutters" reported in Xenia are likely shader compilation. Our port already restores a ~9 MB Vulkan pipeline cache, which is consistent with mitigating this.

### Gaps
- No reports were found on heavy effects (water, reflections, particles), resolution-scale behavior, or FPS for this game on any hardware.
- No game patches (60 FPS / effect toggles) were found for 5841096A in this pass. The game-patches repo was not searched.
- No Steam Deck/Linux reports specific to Hydro Thunder Hurricane.
