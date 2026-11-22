# CyHWInfo 0.1.0

[Français](VERSION.fr.md) · [README](README.md)

## In development

- Modular C++23 library with CMake targets for inventory and telemetry.
- Optional CyUI dashboard and Windows sensor monitor.
- Windows x64 installer and in-app updates from GitHub Releases with SHA-256 verification.
- Optional LibreHardwareMonitor and PawnIO integration for additional CPU, motherboard and memory readings.
- AMD Raphael monitoring for per-core temperatures, PPT/TDC/EDC, HTC and PROCHOT, active DDR5 timings, DRAM bandwidth and C-state residency on supported layouts.

## Compatibility limits

The current validation covers Windows x64 and SMU table `0x00540004` for the extended AMD Raphael readings. Other devices and firmware versions expose different readings. WDDM process telemetry and NVMe/SATA SMART depend on Windows and driver support. Hardware readings that require PawnIO also require access to its Windows device.
