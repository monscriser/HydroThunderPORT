# Hydro Thunder Hurricane — Native PC Port

A community project bringing **Hydro Thunder Hurricane** to Windows and Linux through static recompilation with **ReXGlue**, using Xenia as a compatibility reference.

The project combines native execution of recompiled game code with a Vulkan graphics backend, a configuration launcher, keyboard support, and tools for managing your own game files.

> **Current release: Revision 8 Preview.** Some features remain experimental. Original game files are required and are not included.

## Supported Platforms

- **Windows:** native executable with a separate configuration launcher.
- **Linux:** AppImage with an integrated launcher.
- **Graphics:** Vulkan.

CachyOS, ROG Ally, and Steam Deck are target platforms. Performance and compatibility on each device still require hardware testing.

## Features

### Game and Content Setup

- Guided setup for **Game, DLC, and Title Update** files.
- Support for extracted content and supported Xbox 360 content packages.
- Persistent game-directory selection across launcher updates.
- Save data stored alongside the selected game installation.
- Runtime handling of supported content and translations without modifying the original source files.
- Configuration for the supported **0.0.2.1 title update**.

You must provide your own legally obtained game and additional content.

### Configuration Launcher

- Modern dark interface with game artwork.
- Separate layouts for first-time setup and recognized installations.
- Installation status and game/save directory information.
- Display mode, resolution, language, image scaling, and FPS settings.
- Save backup and restore tools.
- Steam shortcut export with artwork.
- Optional FPS overlay.
- Launcher interface available in Portuguese, English, and Spanish.

### Controls

- Keyboard support.
- Controller support.
- Local player profiles named **Player 1** and **Player 2**.
- Local split-screen support, with further validation needed across configurations.

Default keyboard controls include:

| Action | Key |
|---|---|
| Start / Pause | Enter |
| Confirm / A | Space |
| Accelerate / RT | E |
| Steering | WASD or arrow keys |

### Graphics

- Vulkan rendering backend.
- FSR 1 image upscaling and sharpening.
- Experimental FSR 3.1 image reconstruction.
- Adjustable sharpness.
- Optional FXAA.
- Windowed, borderless, and fullscreen settings.
- Output-resolution and image-quality controls.

**FSR image scaling and frame generation are separate features.** Selecting FSR 3.1 does not enable frame generation. The current FSR 3.1 integration does not receive complete game motion-vector data, so its reconstruction benefits are limited.

### Frame Pacing and Shader Cache

- Parallel shader-compilation support.
- Persistent shader and Vulkan pipeline caches.
- Background cache saving.
- FPS limits of **30, 40, 60, 90, and 120**.
- Experimental **Unlimited** mode.
- Unlimited presentation preserves the virtual clock at 60 Hz.

Cache reuse depends on compatibility with the build, graphics driver, and hardware. New shaders or incompatible caches can still cause stuttering.

### Optional Lossless Scaling Integration

Revision 8 Preview adds launcher integration with a separately purchased Steam installation of **Lossless Scaling**.

- Automatic discovery across supported Steam library locations.
- Purchase/install links when the application is missing.
- Frame-generation multiplier settings.
- Dedicated Windows game-profile configuration with backups.
- Linux setup guidance for **lsfg-vk**.
- Overlay information showing the requested backend and multiplier.

**Lossless Scaling and its proprietary files are not bundled.**

On Linux, installing the Steam application alone is insufficient: the compatible **lsfg-vk Vulkan layer** must also be installed.

## Installation

### Windows

1. Download and extract the Windows release into a new folder.
2. Open **Launcher-Config.exe**.
3. Select your game, DLC, and update files.
4. Complete setup and choose your video settings.
5. Click **Play**.

Use **HydroThunder.exe** to start the game directly. Reopen **Launcher-Config.exe** whenever you want to change settings, manage saves, or export a Steam shortcut.

### Linux

1. Download the Linux `.AppImage`.
2. Allow the file to run as an executable.
3. Open the AppImage.
4. Select your game, DLC, and update files.
5. Complete setup and click **Play**.

If needed, executable permission can be enabled with:

```bash
chmod +x HydroThunder*.AppImage
```

The AppImage opens the configuration launcher by default. A shortcut exported through the launcher starts the game directly through Steam.

### Optional Frame Generation

**Windows:**

1. Install your purchased copy of Lossless Scaling through Steam.
2. Open it once, then close it.
3. Open the launcher's frame-generation settings.
4. Scan for the installation and enable integration.
5. Start with a 2× multiplier.

Close Lossless Scaling before changing settings that require its profile file to be updated.

**Linux:**

1. Install Lossless Scaling through Steam.
2. Select the appropriate `lsfg-vk` Steam branch.
3. Install the compatible lsfg-vk 2.x Vulkan layer for your distribution.
4. Follow the launcher's setup guidance and run the layer's health check.
5. Scan again and enable integration.

See the [lsfg-vk installation documentation](https://lsfg-vk.dev/docs/installation/) for distribution-specific instructions.

## Saves and Updates

The launcher remembers the selected game directory. Updating the application should preserve that registration and the saves stored alongside the game.

Before replacing a build:

1. Export a save backup through the launcher.
2. Extract the new release into a separate application folder.
3. Open its launcher and check the recognized game directory.
4. Keep the previous release until the new one has been tested.

Avoid deleting your game directory when removing an older application build.

## Performance

Performance varies with hardware, resolution, image-quality settings, driver versions, and cache state.

Native recompilation does **not** guarantee a particular improvement over Xenia. GPU rendering and compatibility work remain significant parts of the workload.

Revision 8 Preview demonstrated uncapped presentation inside a race on a Windows test PC. That result is not a performance guarantee for ROG Ally, Steam Deck, or other systems.

Frame generation can increase presentation smoothness, but it does not increase the game's underlying simulation rate or eliminate input latency.

## Known Limitations

- Revision 8 is a **preview**, not a fully validated stable release.
- Lossless Scaling discovery and profile handling were tested separately; actual frame generation still requires end-to-end validation with an installed copy.
- The internal overlay reports the **requested** frame-generation configuration and measured game FPS. It does not independently verify generated frames.
- Linux generated-frame counters depend on external overlays and Vulkan-layer ordering.
- Above-refresh-rate presentation is experimental and may introduce discarded frames, uneven pacing, or tearing.
- A display cannot show more distinct full refreshes per second than its physical refresh capability.
- Unlimited FPS has not received exhaustive physics and audio regression testing.
- Linux gameplay and frame generation require further testing on CachyOS and handheld hardware.
- Some graphics defects may remain, including reported visibility of colored gate lights through scenery.
- Shader compilation can still cause stuttering.
- Steam shortcut export requires further testing against real Steam installations.
- Some error messages are currently available only in Portuguese.
- FSR 3.1 does not currently have complete temporal inputs from the game.

## Reporting Issues

Please include:

- Release revision.
- Operating system and graphics-driver version.
- CPU, GPU, and available RAM.
- Resolution, image-quality settings, and FPS limit.
- Whether frame generation is enabled.
- Track, game mode, and steps to reproduce.
- Screenshots or video when useful.
- Relevant logs.

Mention whether the issue also occurs in your Xenia installation. Comparisons are most useful when the same track, scene, and settings are used.

**Do not upload game files, DLC, title updates, or personal save data in public issue reports.**

## Source and Build Information

The project uses:

- **ReXGlue** for static recompilation and runtime support.
- **Xenia-derived components** for Xbox 360 compatibility.
- **Vulkan** for graphics.
- **Dear ImGui** for configuration and overlays.

Source releases include application code, build tools, SDK modification patches, and available project documentation.

Consult `AGENTS.md`, `CLAUDE.md`, and the build scripts for the current development environment and continuation notes.

## Credits

Thanks to the developers and contributors behind ReXGlue, Xenia, Dear ImGui, FidelityFX, lsfg-vk, and the wider emulation and graphics communities.

Steam artwork credits belong to the respective SteamGridDB contributors, including **Monscriser**.

## Legal and Licensing

This is an unofficial community project. It is not affiliated with or endorsed by Microsoft, Xbox, or the original game's developers or publishers.

Hydro Thunder Hurricane and associated names, artwork, and assets belong to their respective owners.

Game files and purchased third-party components are not distributed with this project. Third-party code and dependencies remain subject to their respective licenses; consult the included license files before redistributing builds or source.
