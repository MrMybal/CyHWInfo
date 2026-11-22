---
id: integration
title: Integrate the C++ library
description: Build, link and sample independent CyHWInfo components.
section: CyHWInfo
parent: overview
order: 20
revision: 1
---

## Build a component {#build}

Use CMake 3.22 or later, Git and a C++23 compiler. For the validated Windows configuration, use Visual Studio 2022 17.10 or later and a Windows SDK with the WDDM 2.4 and NVMe headers. CMake fetches the pinned `lfreist/hwinfo` revision. Use `FETCHCONTENT_SOURCE_DIR_LFREIST_HWINFO` to supply a local copy of that revision.

Run these commands from the CyHWInfo checkout to build GPU inventory and sampled telemetry.

```powershell
cmake -S . -B out/gpu "-DCYHWINFO_COMPONENTS=Gpu;GpuTelemetry"
cmake --build out/gpu --config Release
cmake --install out/gpu --config Release --prefix out/install-gpu
```

Add the installation prefix to `CMAKE_PREFIX_PATH` in the consuming project.

```cmake
find_package(CyHWInfo CONFIG REQUIRED COMPONENTS Gpu GpuTelemetry)
target_link_libraries(my_app PRIVATE CyHWInfo::Gpu CyHWInfo::GpuTelemetry)
```

The selectable collectors are `Cpu`, `Gpu`, `GpuTelemetry`, `GpuProcesses`, `Memory`, `Storage`, `StorageHealth`, `Motherboard`, `Battery`, `Network` and the optional Windows `LibreHardwareMonitor`. The package also exports `Core` and `All`. GPU telemetry and process counters enable `Gpu` automatically, and `StorageHealth` enables `Storage`.

## Read hardware values {#readings}

Include the corresponding public header under `<cyhwinfo/>`. Call `gpu::enumerate()` to discover adapters and pass their IDs to `gpu::Sampler::open()` from `<cyhwinfo/gpu_telemetry.hpp>`. Keep one sampler per adapter and allow time between polls for rate counters. Inspect `Result` before reading its value. Each `Reading` carries an optional value, a status and its source.

WDDM adapter IDs are scoped to the current boot. Native D3DKMT power percentages and vendor-backed watt readings are separate. Windows storage health covers direct NVMe and SATA when the driver exposes SMART passthrough. Refresh the inventory after reconnecting a drive.

## Select advanced groups {#advanced-groups}

Link `CyHWInfo::LibreHardwareMonitor` and call `cyhwinfo_deploy_libre_hardware_monitor(my_app)` for the executable target. The build needs the .NET SDK and the deployed helper uses .NET 8. See [advanced sensors](doc:monitoring#advanced-sensors) for driver access.

```cpp
#include <cyhwinfo/sensors.hpp>

auto sampler = cyhwinfo::sensors::Sampler::open({
    .cpu = false, .motherboard = false, .memory = true
});
if (sampler) {
    auto sample = sampler->sample();
    if (sample && sample->memory) {
        const auto& readings = sample->memory->sensors;
        // Render the values available in readings.
    }
}
```

The sample also reports whether PawnIO is installed and accessible. A group is absent if its option is disabled. Keep the sampler alive between subsequent polls.

## Embed the dashboard {#dashboard}

Enable `CYHWINFO_BUILD_UI` and provide `CyUI::Core` or an accessible CyImgui_Interface checkout. Link `CyHWInfo::UI`. For an installed UI archive, use a matching CyUI and ImGui build. Set `CYHWINFO_BUILD_APP` to build the Win32/Direct3D 11 monitor.

After creating the host ImGui context and initializing CyUI, call `cyhwinfo::ui::register_translations()`. Keep a `Monitor` and a `Dashboard`. Call `dashboard.draw(monitor)` in the host window, or use `dashboard.draw_panel(Page::sensors, *monitor.snapshot())` to embed only the sensor table. The monitor polls on a worker and publishes immutable snapshots.
