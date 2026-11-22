---
id: overview
title: Getting started
description: Choose between the Windows monitor and the C++ library.
section: CyHWInfo
order: 0
revision: 1
---

## What you can build {#what-you-can-build}

CyHWInfo is a modular C++23 library for hardware inventory and sensor readings. Link the components your application needs, for example GPU inventory and telemetry. The optional CyUI module provides a dashboard that can be embedded in another application. The standalone Windows monitor uses the same dashboard and collectors.

## Open the monitor {#open-the-monitor}

In a Windows app build, launch `CyHWInfo.exe`. The app opens the **Sensors** page. Expand a component to see the devices and the available readings. Use **Settings → Language** to switch between English and French.

The base collectors work without PawnIO. The optional advanced sensors can use PawnIO to read more CPU, motherboard and memory data on supported hardware. See [advanced sensors](doc:monitoring#advanced-sensors) for setup and hardware coverage.

## Where to go next {#next}

- [Read sensor values and session statistics](doc:monitoring#sensor-table).
- [Build the library and select CMake components](doc:integration#build).
- [Embed the dashboard](doc:integration#dashboard).
