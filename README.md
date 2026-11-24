# Hydro Thunder Hurricane PC Port — Windows & Linux | HydroThunderPORT

**English** · [Português (Brasil)](README.pt-BR.md) · [Español](README.es.md)

[Download](https://github.com/monscriser/HydroThunderPORT/releases) · [Project website](https://monscriser.github.io/HydroThunderPORT/) · [Issues](https://github.com/monscriser/HydroThunderPORT/issues)

## Project status — FINAL BUILD and the future of HydroThunderPORT

> [!IMPORTANT]
> The current **FINAL BUILD** is the final release of the 1.x generation, based on the current static recompilation and Xbox 360 graphics compatibility architecture. It remains the recommended stable release for Windows and Linux. **This is the end of the current technical cycle, not the end of HydroThunderPORT.**

The next major generation, **HydroThunderPORT 2.0**, is planned around a new native Vulkan renderer. Unlike the current Vulkan backend, the proposed architecture will translate higher-level game graphics operations directly into Vulkan wherever feasible, reducing reliance on the legacy Xenos GPU compatibility path. Static CPU recompilation will remain part of the approach.

The goal is to improve graphics scalability, efficiency and performance while preserving the original game's visual effects, boat physics and gameplay. These are development goals, not features or performance results available today.

Work on 2.0 is planned after the completion and validation of the current [UFC3MONS](https://github.com/monscriser/UFC3MONS) development cycle. **There is no release date or ETA.** No further routine 1.x releases are planned. The FINAL BUILD will remain available as the stable reference while the new architecture is developed and tested.

Thank you to everyone who has played, tested, reported issues and supported this project. Your feedback helped make this release possible.

## About the port

HydroThunderPORT is an unofficial, experimental PC port of Hydro Thunder Hurricane for Windows and Linux. It uses ReXGlue for static recompilation and a Vulkan backend with Xbox 360 compatibility components informed by Xenia. It builds on [htrecomp](https://github.com/visibou/htrecomp) 1.0 by visibou, including its application, launcher/overlay and recompilation foundations.

**Original game files, DLC and title updates are not included.** Use your own legally obtained copies.

| Feature | Current FINAL BUILD |
| --- | --- |
| Windows | Native executable and configuration launcher |
| Linux | AppImage with launcher |
| Graphics | Vulkan (current legacy architecture) |
| Input | Controllers, remappable keyboard, four named local player profiles |
| Image options | FSR 1, experimental FSR 3.1, FXAA, up to 16x anisotropic filtering, resolution and display controls |
| Performance | Persistent shader/pipeline caches and FPS limits |
| Handhelds | ROG Ally tested; broader CachyOS/Steam Deck validation remains limited |

## Release highlights

### FINAL BUILD (PGO)

- ROG Ally testing covered all 11 tracks. On Area 51, the reported minimum improved from 55 to 70 FPS or higher (about 27%); similar improvements were reported on the other tracks. Results depend on the test conditions and hardware.
- Before the main menu, the game prepares its known track shaders for the local GPU and driver, with progress shown on screen. The manifest covers 1,444 pipelines and 586 shaders. Subsequent launches reuse the cache; an unlisted shader may still cause a first-use hitch.
- Logs are disabled by default; enable them in Saves and logs → Write log files.
- Includes the improvements from 1.2.0. The final release is distributed with PGO for both platforms.

### BETA 1.2 (1.2.0)

- Telemetry gathered on a ROG Ally at 20 W across 8 base tracks and 3 DLC tracks included frame data, CPU threads/functions, GPU traces, memory, clocks and power.
- Texture descriptor sets are reused within a frame (approximately 96%). The measured step previously cost 3.17 ms per frame on the limiting thread in demanding tracks such as Area 51, Tsunami Cup and City; exactness verification reported zero differences.
- Linux audio waiting no longer busy-spins: CPU usage of the affected thread fell from 77% of a core to roughly 1% after correcting a sleep rounded to zero milliseconds.
- Fixed unintended RenderDoc loading and its capture overlay on Steam Deck/SteamOS.
- Launcher gained icon navigation, a What's new page, adaptive text sizing and controller/keyboard navigation. From this release onward only PGO packages are published. The 1.2.0 FPS estimate was based on telemetry, not a measured release benchmark at that time.

### BETA 1.1 (1.1.0)

- Save editor for player names 1–4 and credits, with backups and checksum recalculation. Players 3 and 4 gained separate saves.
- Anisotropic filtering choices: game default, 2x, 4x, 8x and 16x (default 16x); no GPU cost was measured in the reported test.
- Exact GPU/CPU verification reported zero differences with the release libraries on Windows and Linux.

### BETA 1.0 (1.0.0)

- Standard and PGO builds had equivalent features. Uncapped Windows tests reported PGO gains of 3.5% on Asgard Invasion and 9% on a canyon track outside the initial training profile. Linux uses its own recorded profile.
- Launcher based on the UFC3MONS layout: language selector, Home, Video, Controls, Saves and logs, Frame generation, Add to Steam and Play.
- Configurable `user_data` location, copying existing data and retaining the old directory as backup; selection persists across updates. Xenia import locates `ht2save.dat` and backs up the current save.
- Corrected oversized windowed mode with Windows display scaling at 125%/150%.
- Removed a global lock from memory-page checks on the video command thread; reported CPU cost per draw on that thread fell by 2.7%.

### Revision 9.1.0

- Switchable engine optimizations in `hydrothunder.toml` target CPU/GPU efficiency without reducing resolution, MSAA, effects or precision.
- Informal ROG Ally Linux tests at 25 W across the first four tracks reported about 120 FPS most of the time and rarely below 100 FPS; not a controlled benchmark or guarantee.
- Corrected an exit-menu crash and lighting visible through walls; remappable keyboard controls were added to the launcher.

See [Releases](https://github.com/monscriser/HydroThunderPORT/releases) and [FINAL BUILD notes](NOTAS-FINAL-BUILD.md) for historical measurements, downloads and checksums.

## Features and configuration

- Guided installation of supported extracted Xbox 360 content and content packages (game, DLC and Title Update), including the 0.0.2.1 update configuration. Supported content and translations are handled at runtime without modifying original game source files.
- Persistent game folder and configurable `user_data` (saves, DLC and achievements); Xenia save import, backup/restore, editor for four players' names and credits, optional FPS overlay, Steam shortcut export with artwork.
- Launcher in English, Brazilian Portuguese and Spanish, with display mode, resolution, language, scaling, FPS and installation settings.
- Controllers, remappable keyboard and up to four local profiles; split-screen exists but needs broader validation.

| Action | Default key |
| --- | --- |
| Start / Pause | Enter |
| Confirm / A | Space |
| Accelerate / RT | E |
| Brake / LT | Q |
| Steering | A/D or arrow keys |

### Graphics and performance

The current backend offers windowed, borderless and fullscreen modes, FSR 1 scaling/sharpening, experimental FSR 3.1 reconstruction, optional FXAA, anisotropic filtering up to 16x, parallel shader compilation, persistent caches and background cache saving. FPS limits: 30, 40, 60, 90 and 120; experimental unlimited presentation uses a 60 Hz virtual clock.

FSR 3.1 lacks full in-game motion-vector data. Upscaling is not frame generation; choosing FSR 3.1 does not generate frames. Cache compatibility depends on hardware, driver and build.

### Optional Lossless Scaling integration

The launcher can detect a separately purchased Steam copy, configure Windows profiles with backups and select a frame-generation multiplier. On Linux it provides setup guidance for the external `lsfg-vk` layer and reports requested settings in an overlay. Lossless Scaling is not included; Linux requires a compatible Vulkan layer in addition to the Steam app. End-to-end validation remains incomplete.

## Installation

### Windows

1. Extract `Hydro-Thunder-FINAL-BUILD-Windows-PGO.zip` from [Releases](https://github.com/monscriser/HydroThunderPORT/releases).
2. Open `Launcher-Config.exe` and select your game, DLC and update files.
3. Configure video and controls, then select Play.

After setup, `HydroThunder.exe` can be launched directly. Use the launcher for settings, saves and Steam shortcuts.

### Linux / AppImage

1. Download the Linux AppImage from [Releases](https://github.com/monscriser/HydroThunderPORT/releases).
2. If needed, make it executable: `chmod +x HydroThunder*.AppImage`.
3. Open it, select your legally obtained game files, configure and select Play.

Export a Steam shortcut from the launcher to start the game directly through Steam.

### Optional frame generation

- **Windows:** Install and launch your purchased Lossless Scaling Steam app, close it and enable integration in the launcher. Start with 2x. Close Lossless Scaling before changing profile settings.
- **Linux:** Install the purchased app, select the appropriate `lsfg-vk` Steam branch, install a compatible 2.x Vulkan layer for your distribution, run its health check, then rescan and enable integration. Follow the layer's own documentation.

## Saves, updates and compatibility

The game directory selection persists. Back up saves before changing builds, extract into a separate application folder, verify the game path and keep the previous build until testing is complete. Never delete the actual game directory when removing an old launcher.

Performance depends on hardware, drivers, settings, track and cache state. Static recompilation does not guarantee an advantage over Xenia. Frame generation cannot increase the underlying simulation rate or remove input latency.

## Known limitations

- FINAL BUILD closes the 1.x technical cycle; routine updates are not planned. The future 2.0 renderer is not included.
- Split-screen with three or four physical controllers remains untested, although all four profiles were checked. Close the game before editing save credits.
- Broader Linux/CachyOS/Steam Deck and handheld validation is still limited; ROG Ally observations are not universal guarantees.
- Lossless Scaling requires further end-to-end testing. The in-game overlay does not independently verify generated frames; Linux counters depend on external overlays and layer ordering.
- Above-refresh-rate or unlimited presentation can cause uneven pacing, tearing or regressions. Unlimited mode lacks exhaustive physics/audio validation.
- Unlisted shaders may still hitch once; graphical issues such as colored gate lights visible through scenery may remain.
- Steam shortcut export needs wider real-installation testing; some errors are Portuguese-only.
- FSR 3.1 lacks complete temporal inputs.

## Building from source

Generate the recompiled game code yourself from your own game copy: `app/generated-updated/` is not distributed here. Build scripts: [`tools/`](tools/) and [`tools/wsl/`](tools/wsl/). PGO uses CMake `-DHT_PGO=USE -DHT_PGO_PROFILE=<file>` and profiles in [`pgo/`](pgo/), with `build_pgo.sh` / `build_pgo_linux.sh`; Linux also needs the profile runtime from `build_profile_runtime.sh`.

The project uses ReXGlue, Xenia-derived compatibility components, Vulkan and Dear ImGui. See [`licenses/`](licenses/) and [`LICENSE-ReXGlue`](LICENSE-ReXGlue) for applicable third-party terms.

## Issues and contributions

When reporting an [issue](https://github.com/monscriser/HydroThunderPORT/issues), include the release, OS, driver, CPU/GPU/RAM, display and FPS settings, frame-generation status, track/mode, reproduction steps, relevant logs and screenshots/video when useful. Compare against Xenia under matching conditions. Reports may help inform 2.0; they do not imply a scheduled 1.x fix.

Never upload copyrighted game content, DLC, updates or personal save data to public issues.

## Credits and legal notice

Special thanks to visibou and [htrecomp](https://github.com/visibou/htrecomp), the foundation of this port and the original documentation of the save checksum format. Thanks to the contributors to ReXGlue, Xenia, Dear ImGui, FidelityFX, lsfg-vk and the graphics/emulation community. Steam artwork belongs to its SteamGridDB contributors, including Monscriser.

HydroThunderPORT is an unofficial community project, not affiliated with or endorsed by Microsoft, Xbox or the original developers/publishers. Original names, artwork and assets belong to their respective rights holders. No proprietary game or Lossless Scaling files are distributed. Respect third-party licenses when reusing or redistributing code.
