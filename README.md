# Hydro Thunder Hurricane PC Port — Windows & Linux | HydroThunderPORT

**HydroThunderPORT** is an experimental, unofficial **Hydro Thunder Hurricane native PC port for Windows and Linux**, built using **static recompilation with ReXGlue**, a **Vulkan graphics backend**, and Xbox 360 compatibility work informed by **Xenia**.

Play Hydro Thunder Hurricane on PC with a configuration launcher, controller and keyboard support, graphics scaling options, persistent shader caches, and support for your own legally obtained game files. The Windows version uses a native executable; the Linux version is distributed as an AppImage.

> **Current preview: Revision 9.1.0.** This project is experimental and is not affiliated with the original game's creators. **Game files, DLC, and title updates are not provided.** You must supply your own legally obtained copies.

[**Download the latest release**](https://github.com/monscriser/HydroThunderPORT/releases) · [**Official project website**](https://monscriser.github.io/HydroThunderPORT/) · [**Report an issue**](https://github.com/monscriser/HydroThunderPORT/issues)

## Hydro Thunder Hurricane on PC: Quick Overview

| Feature | Status / details |
| --- | --- |
| Windows PC port | Native executable and configuration launcher |
| Linux PC port | AppImage with integrated configuration launcher |
| Graphics API | Vulkan |
| Input | Game controller and remappable keyboard controls |
| Graphics options | FSR 1, experimental FSR 3.1, FXAA, resolution and display settings |
| Performance options | Shader/pipeline caches and configurable FPS limits |
| Handheld targets | ROG Ally, CachyOS and Steam Deck; validation is ongoing |
| Original game content | Required; not included in the download |

## Release Highlights — Revision 9.1.0

- **CPU/GPU performance:** configurable engine optimizations target improved frame rates without reducing resolution, MSAA, effects, or precision; each has a switch in `hydrothunder.toml`.
- **ROG Ally (Linux, 25 W TDP):** first four tracks were play-tested at around 120 FPS most of the time and rarely below 100 FPS. These are informal observations, **not controlled benchmarks or guarantees**.
- **Stability:** fixes for a crash when leaving through the in-game menu and lighting showing through walls.
- **Controls:** the launcher includes a Controls tab with remappable keys.

Check the [GitHub Releases page](https://github.com/monscriser/HydroThunderPORT/releases) for downloads, release notes, measurements and checksums.

## Features

### Game Content and Launcher

- Guided setup for Game, DLC and Title Update files, including supported extracted content and Xbox 360 content packages.
- Game directory selection persists between launcher updates; save data lives alongside your selected game installation.
- Runtime handling of supported content and translations without modifying original game source files.
- Support for the 0.0.2.1 title update configuration.
- Configuration launcher with display mode, resolution, language, scaling, FPS and installation details.
- Save backup and restore, optional FPS overlay, and Steam shortcut export with artwork.
- Launcher languages: Portuguese, English and Spanish.

### Controllers and Keyboard

- Controller support, remappable keyboard controls and local profiles for Player 1 and Player 2.
- Local split-screen is supported, but still needs validation across configurations.

| Action | Default key |
| --- | --- |
| Start / Pause | Enter |
| Confirm / A | Space |
| Accelerate / RT | E |
| Brake / LT | Q |
| Steering | A/D or arrow keys |

### Vulkan Graphics, Upscaling and Performance

- Vulkan graphics backend, FSR 1 upscaling and sharpening, experimental FSR 3.1 reconstruction, optional FXAA.
- Windowed, borderless and fullscreen modes; resolution and image-quality adjustments.
- Parallel shader compilation, persistent shader/Vulkan pipeline caches and background cache saving.
- FPS limits: 30, 40, 60, 90 and 120; experimental unlimited presentation maintains a 60 Hz virtual clock.

**Note:** FSR 3.1 currently lacks complete in-game motion-vector inputs. FSR scaling is not the same as frame generation; selecting FSR 3.1 **does not** enable generated frames. Shader cache compatibility varies with drivers, hardware and builds.

### Optional Lossless Scaling Integration

- Discovers a separately purchased Steam installation of Lossless Scaling in supported locations.
- Offers frame-generation multiplier and Windows game-profile configuration with backups.
- Provides Linux setup guidance for the external `lsfg-vk` Vulkan layer and an overlay reporting requested settings.

Lossless Scaling is **not bundled**. On Linux, the Steam application alone is insufficient; a compatible `lsfg-vk` Vulkan layer is also required. Frame generation still requires further end-to-end testing.

## How to Install Hydro Thunder Hurricane PC Port

### Windows

1. Download and extract the Windows package from [Releases](https://github.com/monscriser/HydroThunderPORT/releases).
2. Launch `Launcher-Config.exe`.
3. Select your own Game, DLC and update files.
4. Configure the video and input settings, then click **Play**.

You can subsequently run `HydroThunder.exe` directly. Use the launcher for settings, save management and Steam shortcut export.

### Linux / AppImage

1. Download the Linux `.AppImage` from [Releases](https://github.com/monscriser/HydroThunderPORT/releases).
2. Grant execution permission if needed: `chmod +x HydroThunder*.AppImage`.
3. Open the AppImage, select your legally obtained game files and finish configuration.
4. Click **Play**. Exporting a Steam shortcut through the launcher enables direct game startup through Steam.

### Optional Frame Generation Setup

**Windows:** Install and open your purchased copy of Lossless Scaling in Steam, close it, and enable integration in the launcher. Start with a 2× multiplier. Close Lossless Scaling before changing settings that update its profile.

**Linux:** Install the purchased Steam app, select an appropriate `lsfg-vk` Steam branch, install a compatible `lsfg-vk` 2.x Vulkan layer for your distribution, run the layer's health check, then rescan and enable integration in the launcher. Follow the layer's own installation documentation.

## Saves, Updates and Compatibility

The launcher remembers your selected game directory. Before upgrading, export a save backup, extract the new release into a different application folder, verify that the game directory is recognized, and retain the previous version until you have tested the update. Do not delete your actual game directory when removing an older launcher.

Actual performance depends on hardware, drivers, settings, track and shader-cache state. Static recompilation does not guarantee higher performance than Xenia. Frame generation can change visual smoothness but cannot raise the underlying simulation rate or eliminate input latency.

## Known Limitations

- Revision 9.1.0 is a **preview**, not a fully validated stable release.
- ROG Ally testing is informal; broader Linux, CachyOS, Steam Deck and handheld testing is still needed.
- Lossless Scaling frame generation needs further end-to-end validation; the in-game overlay does not independently verify generated frames.
- Linux generated-frame counters depend on external overlays and Vulkan-layer ordering.
- Above-refresh-rate presentation and unlimited FPS may cause uneven pacing, tearing or regressions; unlimited mode has not undergone exhaustive physics/audio checks.
- Shader compilation can still stutter; graphics issues may remain, including colored gate lights appearing through scenery.
- Steam shortcut export needs testing across real Steam installations; some errors remain Portuguese-only.
- FSR 3.1 does not have the complete temporal data needed for full reconstruction quality.

## Building from Source

The repository does not ship the recompiled game code (`app/generated-updated/`). Building from source requires generating that code from your **own** game copy. Build scripts are under [`tools/`](tools/) and [`tools/wsl/`](tools/wsl/); development notes are in [`AGENTS.md`](AGENTS.md) and [`CLAUDE.md`](CLAUDE.md).

The project uses ReXGlue for static recompilation/runtime support, Xenia-derived compatibility components, Vulkan for rendering, and Dear ImGui for configuration and overlays. Third-party software retains its own licenses; see [`licenses/`](licenses/).

## Bug Reports and Contributions

Please open an [issue](https://github.com/monscriser/HydroThunderPORT/issues) and include the release revision, operating system, driver, CPU, GPU, RAM, display/FPS settings, whether frame generation is enabled, track and game mode, reproduction steps, relevant logs, and screenshots or video where helpful. Xenia comparisons are useful when made in the same scene and settings.

**Do not upload copyrighted game files, DLC, updates or personal save data in public issues.**

## Credits and Legal Notice

Thanks to contributors to ReXGlue, Xenia, Dear ImGui, FidelityFX, lsfg-vk and the wider graphics/emulation community. Steam artwork belongs to its respective SteamGridDB contributors, including Monscriser.

**HydroThunderPORT is an unofficial community project.** It is not affiliated with or endorsed by Microsoft, Xbox or the game's original developers/publishers. Hydro Thunder Hurricane names, artwork and assets remain the property of their respective rights holders. No original game files or proprietary Lossless Scaling files are distributed. Consult the relevant third-party licenses before reuse or redistribution.
