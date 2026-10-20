# CachyOS / Intel OXP3 preview

This branch keeps the fork's network-aware bitrate controls and adds display-following resolution/FPS options. It also contains an opt-in Intel HEVC 4:4:4 import prototype. The normal installed Moonlight Flatpak remains available for comparison and rollback.

## Display and bitrate behavior

In Settings, choose **Native (Follow display)** independently for resolution and FPS. Each stream start resolves the active display again. Reconnect after docking or undocking; an existing stream is not renegotiated automatically.

- Resolution uses the current display's pixel dimensions. Under Gamescope, a read-only DRM query identifies the single active physical output, so a game-sized virtual desktop does not hide the TV's mode. A connected, disabled built-in panel is ignored.
- Multiple active physical outputs, unavailable DRM permissions, or incomplete output information use the selected desktop display as the fallback. Missing/invalid discovery retains the saved manual resolution or FPS.
- FPS uses the current configured refresh rate. The streaming protocol accepts integer FPS, so fractional rates use their nearest nominal integer: 59.94 → 60, 119.88 → 120, and 143.99 → 144. Native does not change the client's display mode.
- Fixed resolutions and custom FPS remain available. Their saved values are retained when Native is selected.
- The host must accept the requested codec, resolution and frame rate. These client settings do not create or configure a host virtual display.

**Use a separate bitrate for Wi-Fi** selects the bitrate using the actual route to the streaming host at connection time. A positively identified, non-VPN Ethernet route uses the primary bitrate. Wi-Fi, VPN and unknown routes use the Wi-Fi preset. This is a connection-start preset selection, not continuous network adaptation.

Moving a bitrate slider makes it manual. With automatic bitrate enabled, Native settings calculate the primary default from the resolved display mode. Automatic rates respect the normal 150 Mbps limit, or 500 Mbps when unlocked. Explicit primary, CLI and Wi-Fi preset values are preserved. If the host falls back from 4:4:4 to 4:2:0, only an automatic primary rate is recalculated.

Equivalent CLI options:

```sh
scripts/run-cachyos-oxp3.sh stream --native-resolution --native-fps HOST APP
scripts/run-cachyos-oxp3.sh stream --native-resolution --native-fps \
  --bitrate 120000 --wifi-bitrate 35000 HOST APP
```

Use a host/app name already known to Moonlight. `--bitrate` is an explicit primary rate; `--wifi-bitrate` also enables the network-aware preset. Fixed resolution options and `--fps` override their respective saved Native setting. When conflicting display options are supplied, the last option of each type wins. Run `scripts/run-cachyos-oxp3.sh stream --help` for the remaining options.

## Build on CachyOS

Run as the normal desktop user. The helper checks prerequisites and builds into `build/oxp3`; it performs no package installation or system library replacement.

```sh
git submodule update --init --recursive
bash scripts/build-cachyos-oxp3.sh --check
bash scripts/build-cachyos-oxp3.sh
```

Typical CachyOS packages, if the prerequisite check reports missing components:

```sh
sudo pacman -S --needed base-devel git qt6-base qt6-declarative qt6-svg \
  qt6-wayland sdl2-compat sdl2_ttf ffmpeg opus openssl libva libdrm \
  libplacebo mesa wayland-protocols
```

The helper uses `qmake6`, `make qmake_all`, and a parallel native build. `MOONLIGHT_BUILD_JOBS` selects a positive job count; the default uses up to eight jobs. An optional final argument selects another build directory:

```sh
MOONLIGHT_BUILD_JOBS=4 bash scripts/build-cachyos-oxp3.sh build/oxp3-standard
```

### Experimental HDR / 4:4:4 build

For Intel HEVC 4:4:4, including 10-bit HDR surfaces, see the [prototype README](../contrib/intel-packed-yuv444/README.md) and [technical writeup](../contrib/intel-packed-yuv444/WRITEUP.md). The import patch passed offscreen real Intel decode/import/color checks. Actual HDR presentation through the compositor/display and full-resolution streaming performance need separate verification.

The helper requires **exactly libplacebo 7.360.1** for this option. Its preparation script also checks the hashes of the two unmodified input headers. A different or distribution-patched header must be reviewed/rebased before use.

```sh
bash scripts/build-cachyos-oxp3.sh --check --hdr444
bash scripts/build-cachyos-oxp3.sh --hdr444
```

This prepares a new private include directory inside the build directory and exports `MOONLIGHT_INTEL_PACKED_YUV444_HEADERS` only to the build. Moonlight's header-only libplacebo adapter is rebuilt with that override; the installed libplacebo shared library is unchanged. The adapter object is rebuilt on each HDR/4:4:4 invocation. Switching build modes cleans previous objects first.

Use a separate build directory to keep both executables:

```sh
bash scripts/build-cachyos-oxp3.sh --hdr444 build/oxp3-hdr444
MOONLIGHT_OXP3_BINARY="$PWD/build/oxp3-hdr444/app/moonlight" \
  bash scripts/run-cachyos-oxp3.sh
```

Choose HEVC for Intel 4:4:4 testing. The OXP3's driver advertises the relevant HEVC 4:4:4 profiles. HDR still requires successful compositor/swapchain and display signaling. The prototype refuses unsupported surface layouts rather than introducing a runtime software-decoding fallback.

## Run with a private profile

```sh
bash scripts/run-cachyos-oxp3.sh
```

The launcher defaults to `build/oxp3/app/moonlight`. Set `MOONLIGHT_OXP3_BINARY` to select another executable. It creates a private directory under `$XDG_DATA_HOME/moonlight-oxp3-preview` (normally `~/.local/share/moonlight-oxp3-preview`) with separate configuration, cache, data and temporary directories.

On the first launch only, it copies the existing Moonlight Flatpak `Moonlight.conf`, including its pairing identity and host list, into the private profile. It preserves the original Qt settings bytes and key casing, changing only the two Native preference values for the initial preview. If no Flatpak configuration exists, the preview creates its own identity and needs normal host pairing. Native is enabled initially; subsequent launches retain changes made in the preview.

The copied profile contains private pairing credentials. Directories use mode 0700 and the configuration uses 0600; keep the preview state outside the source repository. No configuration or cache is copied back to the Flatpak.

Optional environment variables:

| Variable | Purpose |
| --- | --- |
| `MOONLIGHT_OXP3_BINARY` | Preview executable path |
| `MOONLIGHT_OXP3_STATE_DIR` | Dedicated private profile directory |
| `MOONLIGHT_OXP3_CONFIG_SOURCE` | Explicit first-run `Moonlight.conf` import source |

The import source is considered only when the preview configuration is absent. The launcher refuses linked/unowned private profile paths and a state directory inside the installed Flatpak profile. It forwards Moonlight arguments individually. Run one Moonlight client at a time while comparing versions.

## Checks and validation limits

```sh
bash tests/run-native-display-tests.sh
bash tests/run-bitrate-tests.sh
PYTHONDONTWRITEBYTECODE=1 python3 tests/test-preview-launcher.py -v
```

The Native suite exercises the runtime selection helpers with ASan/UBSan: dock/undock snapshots, disabled panels, ambiguous outputs, invalid modes, independent Native preferences, fractional rates and timing flags. The bitrate suite exercises route/preset and automatic-limit helpers using Qt Core/Network. The launcher suite uses a harmless stand-in executable to check profile copying, identity/key preservation, private permissions, first-run-only defaults, argument forwarding and refusal paths.

These host tests do not establish real dock hotplug, host resolution switching, stream throughput, input pacing or HDR TV output. The [Intel candidate tests](../contrib/intel-packed-yuv444/README.md#reproduce-off-device-checks) separately cover packed-format import and GPU color arithmetic; its optional hardware probe covers real offscreen VAAPI/DMA-BUF/Vulkan interoperability.

### Production display-detector check

After building, check the current docked 4K/144 Hz mode with:

```sh
bash tests/run-native-display-probe.sh 3840 2160 144
```

By default this links the production `build/oxp3/app/release/streamutils.o`, including its actual qmake feature definitions. It refuses an object lacking the physical DRM detector. Select another build with `MOONLIGHT_OXP3_BUILD_DIR`, or pass `--object PATH/streamutils.o`.

The probe uses offscreen/dummy GUI backends and a read-only physical display query; it does not create a visible window or change the display mode. The actual production-object check passed on the docked OXP3 at 3840×2160 and approximately 143.988 Hz, resolved to 144 FPS. Physical undock/reconnect behavior remains unverified. Use the expected dimensions/FPS for the connected display when running the command elsewhere.

## Source and licensing

- Upstream Moonlight baseline: [`dc92b6d2f361064d3e9e53ce5de92f611db77220`](https://github.com/moonlight-stream/moonlight-qt/commit/dc92b6d2f361064d3e9e53ce5de92f611db77220).
- Existing fork bitrate feature retained from [`340c6fb3de5a2eb0843a3fcd51c3a30e5bce36b6`](https://github.com/Grippy98/moonlight-qt/commit/340c6fb3de5a2eb0843a3fcd51c3a30e5bce36b6).
- Submodules remain pinned by this repository's gitlinks; initialize them from this checkout rather than substituting unreviewed versions.
- The private adapter patch targets libplacebo v7.360.1, commit `cee9b076f2c63104ccfd497fa79c39a867293ec4`. Its provenance, scope and limitations are documented beside the patch.

Moonlight remains under its original [GPL-3.0 license](../LICENSE). libplacebo's adapter and candidate patch retain [LGPL-2.1-or-later licensing](../contrib/intel-packed-yuv444/LICENSE.libplacebo). Other bundled/submodule sources retain their existing licenses. No device captures, pairing profiles or compiled executables belong in this source change.

## Rollback

Quit the preview and launch the original Moonlight Flatpak or its existing Steam shortcut. The build/launcher scripts do not replace that application, modify its profile, install services, or alter the bootloader.

Rebuild without `--hdr444` to compare the ordinary import path, or select a separately built standard binary through `MOONLIGHT_OXP3_BINARY`. Build output and the dedicated private preview directory can be removed after the preview is closed. Removing private state also removes its copied identity and preferences; the original Flatpak profile remains the first-run import source if a new preview profile is created later.
