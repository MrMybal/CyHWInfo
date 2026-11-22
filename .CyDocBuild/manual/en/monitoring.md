---
id: monitoring
title: Monitor hardware
description: Read the Sensors table and enable optional Windows collectors.
section: CyHWInfo
parent: overview
order: 10
revision: 1
---

## Sensors and statistics {#sensor-table}

Expand a device or measurement group in **Sensors**. The table shows **Current**, **Minimum**, **Maximum** and **Average**. Use the search field to find a device or sensor. **Available only** hides rows without a current reading. Hover over a sensor to see its source.

```image
{"id":"sensor-table-example","src":"assets/sensors-en.png","alt":"Sensor table with core C-state percentages and session statistics using demo data","caption":"Sensor table with demonstration data","controls":"minimal"}
```

Statistics use valid readings collected since the start of the session or the last **Reset statistics** action. Cached SMART readings count once per hardware read. Pause freezes collection. The reset clears the statistics while retaining the current reading. A dash indicates an unavailable reading. Zero is valid, for example for a stopped fan. Boolean flags show Yes or No and record their observed range.

## Advanced sensors on Windows {#advanced-sensors}

1. Build the `LibreHardwareMonitor` component and deploy the `cyhwinfo-lhm` directory beside the executable. Install the .NET 8 Runtime for the helper.
2. For readings that need hardware access, install the separately distributed [signed PawnIO driver](https://pawnio.eu/). The Windows session must have access to its device. Administrator rights may be required.
3. In the app, enable **Settings → Advanced sensors**. Turn this option off to continue with the standard collectors.

Available readings depend on the hardware, firmware, drivers and access rights. LibreHardwareMonitor can provide CPU temperature and power and motherboard voltages, temperatures and fan speeds. On supported AMD Raphael systems, the optional provider adds live DDR5 controller timings and HTC, internal PROCHOT and external PROCHOT flags. The `0x00540004` SMU layout adds per-core temperatures, PPT/TDC/EDC limits and usage, aggregate DRAM read and write bandwidth, and per-core C0, C1 and C6 residency.

The current **Average active core count** is the sum of the physical cores’ C0 fractions over a single SMU measurement window. Two cores that each spend half the interval in C0 contribute one active core on average. The **Average** column tracks the session average of those readings.
