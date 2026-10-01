# Monado Simulated OpenXR Device

Monado can expose a simulated headset while using its real Vulkan compositor. This exercises OpenXR session timing, eye swapchains and Vultra's desktop mirror without physical hardware. It does not validate tracking hardware, controllers, headset display quality or motion-to-photon latency.

## System Default Installation

The local Linux development machine now uses a pacman-managed `monado-simulated` package (`25.1.0.r3022-1`), built from the version and upstream patch below. Binaries live in `/usr/bin`, runtime libraries in `/usr/lib`, and the manifest in `/usr/share/openxr/1/openxr_monado.json`. `/etc/xdg/openxr/1/active_runtime.json` points to that manifest.

The upstream systemd **user** socket is enabled at login. It starts Monado on demand in the user's graphical session, with the simulated driver and Wayland graphics compositor enabled. `IPC_EXIT_ON_DISCONNECT=1` stops the compositor after the client exits, while the socket stays ready for the next application. This follows Monado's [documented socket activation and runtime selection](https://monado.freedesktop.org/getting-started.html#monado-service).

No environment override is required:

```sh
unset XR_RUNTIME_JSON
xmake run example-xr sponza
```

Useful service commands:

```sh
systemctl --user status monado.socket
journalctl --user -u monado.service
systemctl --user stop monado.service
```

An inactive `monado.service` between applications is normal; `monado.socket` should be active/listening. The current Hyprland session imports its display environment into the user service manager at login. A Monado-specific window rule sends its compositor to Workspace 2 without changing the focused workspace.

Both XR examples were verified with `XR_RUNTIME_JSON` explicitly removed: each automatically activated the system runtime, completed 10 application frames / 9 eye frames, captured its mirror and exited without Vulkan validation diagnostics. The compositor exited with status 0 after each client.

### Switching to Another Runtime

When another headset/runtime is installed, stop automatic Monado activation first:

```sh
systemctl --user disable --now monado.socket
systemctl --user stop monado.service
unset XR_RUNTIME_JSON
```

Use the new runtime's registration mechanism, or point `~/.config/openxr/1/active_runtime.json` at its verified installed manifest. The user manifest takes precedence over the system default; `XR_RUNTIME_JSON` overrides both and should not remain pinned to Monado. The system package can be removed with `sudo pacman -R monado-simulated` when no longer needed. Compatibility of future hardware with its chosen Linux runtime must be checked at that time.

## Runtime Version

The Linux validation used [Monado v25.1.0](https://gitlab.freedesktop.org/monado/monado/-/tree/v25.1.0), commit `3b450e37b624f5edea366492ee2d267d673a35aa`, under its [Boost Software License 1.0](https://gitlab.freedesktop.org/monado/monado/-/blob/v25.1.0/LICENSE). One official upstream fix was applied: [merge request 3022](https://gitlab.freedesktop.org/monado/monado/-/merge_requests/3022), implementation commit `9d03a77b39602af9897ba9e27223d6c6d811dd7a` (plus its documentation commit `e936b6a53a5a4c0c58eba385593482b3643062b1`). There are no additional local Monado source changes.

The fix prevents Monado from inserting `VkPhysicalDeviceTimelineSemaphoreFeatures` beside an application's `VkPhysicalDeviceVulkan12Features`. Unpatched 25.1.0 renders but reports `VUID-VkDeviceCreateInfo-pNext-02830`; that run is not a clean validation result. Vultra retains its normal Vulkan feature chain and validation diagnostics.

## Alternative User-local Build and Install

Install Monado's [documented build dependencies](https://monado.freedesktop.org/getting-started.html), including CMake/Ninja, Vulkan, Wayland and XCB development files. The following installs a simulated-device runtime into a user-owned prefix, without changing the system's active OpenXR runtime:

```sh
mkdir -p build/.tmp/monado
cd build/.tmp/monado
git clone --branch v25.1.0 --depth 1 https://gitlab.freedesktop.org/monado/monado.git source
curl -fL https://gitlab.freedesktop.org/monado/monado/-/commit/9d03a77b39602af9897ba9e27223d6c6d811dd7a.patch -o timeline.patch
git -C source apply --check ../timeline.patch
git -C source apply ../timeline.patch

# Keep only the simulated driver in this development runtime.
set --
for driver in ANDROID ARDUINO BLUBUR_S1 DAYDREAM DEPTHAI EUROC HANDTRACKING HDK HYDRA \
    ILLIXR NS OHMD OPENGLOVES PSMV PSSENSE PSVR QWERTY REALSENSE REMOTE RIFT RIFT_S \
    ROKID SIMULAVR SOLARXR STEAMVR_LIGHTHOUSE SURVIVE TWRAP ULV2 ULV5 VF VIVE WMR XREAL_AIR
do
    set -- "$@" "-DXRT_BUILD_DRIVER_${driver}=OFF"
done
cmake -S source -B build -G Ninja -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX="$HOME/.local/opt/monado-25.1.0" \
    -DBUILD_TESTING=OFF -DXRT_BUILD_SAMPLES=OFF \
    -DXRT_FEATURE_STEAMVR_PLUGIN=OFF -DXRT_FEATURE_SLAM=OFF \
    -DXRT_MODULE_MERCURY_HANDTRACKING=OFF -DXRT_INSTALL_SYSTEMD_UNIT_FILES=OFF \
    -DXRT_BUILD_DRIVER_SIMULATED=ON "$@"
cmake --build build --parallel
cmake --install build
```

Stop this runtime's service before replacing an existing installation. Retain the upstream source/license and patch alongside the local build for provenance. Monado is a development runtime, not a dependency of `vultra` or its ordinary desktop tests.

## Manual User-local Run

For the alternative user-local installation, stop/disable the system socket as described above before starting a manual service on the workspace intended for test windows:

```sh
SIMULATED_ENABLE=1 XRT_COMPOSITOR_FORCE_WAYLAND=1 \
XRT_COMPOSITOR_COMPUTE=0 XRT_DEBUG_GUI=0 \
    "$HOME/.local/opt/monado-25.1.0/bin/monado-service"
```

`XRT_COMPOSITOR_COMPUTE=0` selects Monado's graphics compositor. On the tested NVIDIA driver, the default compute queue could not present to the Wayland surface and compositor initialization failed. Keep this setting for that configuration. Native Wayland selection applies to the compositor independently of Vultra's mirror window backend.

From another terminal at the repository root:

```sh
XR_RUNTIME_JSON="$HOME/.local/opt/monado-25.1.0/share/openxr/1/openxr_monado.json" \
VULTRA_WINDOW_SYSTEM=wayland \
    xmake run example-xr triangle --frames 60 --capture build/.tmp/xr-triangle.png
```

The same runtime works with GLFW or SDL3 and with `VULTRA_WINDOW_SYSTEM=x11`. Set these variables per process; no global runtime registration is required. Press Enter in the service terminal to stop it after testing.

Inspect both application and compositor logs. Finite-frame success requires actual eye frames and a nonblank stereo mirror, not merely application loop iterations. The local compositor reported missed-frame timing warnings during capture/load and an unavailable peek-window warning; these results are functional rendering checks, not a frame-pacing benchmark. Physical-headset testing and the separate historical Meta XR Simulator pipeline-cache issue remain unverified by this Monado run.
