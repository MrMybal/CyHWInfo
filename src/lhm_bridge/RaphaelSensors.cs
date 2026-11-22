using LibreHardwareMonitor.Hardware;
using LibreHardwareMonitor.Hardware.Cpu;
using LibreHardwareMonitor.PawnIo;
using static CyHWInfo.SensorBridge.Program;

namespace CyHWInfo.SensorBridge;

internal sealed class RaphaelSensors : IDisposable
{
    // See licenses/SOURCES.txt for the layout and register references.
    private const uint DualCcdPmTableVersion = 0x00540004;
    private const int CoresPerCcd = 8;
    private const int CoreVoltage = 309;
    private const int CoreTemperature = 325;
    private const int CoreClock = 341;
    private const int CoreC0 = 373;
    private const int CoreC1 = 389;
    private const int CoreC6 = 405;
    private const int L3Clock = 541;
    private const int DramReadBandwidth = 95;
    private const int DramWriteBandwidth = 96;
    private const uint HtcRegister = 0x59804;
    private const uint ExternalProchot = 1u << 2;
    private const uint InternalProchot = 1u << 3;
    private const uint HtcActive = 1u << 4;

    private static readonly (string Name, uint Address, int Shift, uint Mask)[] Timings =
    [
        ("tCL", 0x50204, 0, 0x3f), ("tRCDRD", 0x50204, 16, 0x3f), ("tRCDWR", 0x50204, 24, 0x3f),
        ("tRP", 0x50208, 16, 0x3f), ("tRAS", 0x50204, 8, 0x7f), ("tRC", 0x50208, 0, 0xff),
        ("tRRD_S", 0x5020c, 0, 0x1f), ("tRRD_L", 0x5020c, 8, 0x1f), ("tRTP", 0x5020c, 24, 0x1f),
        ("tFAW", 0x50210, 0, 0xff), ("tCWL", 0x50214, 0, 0x3f), ("tWTR_S", 0x50214, 8, 0x1f),
        ("tWTR_L", 0x50214, 16, 0x7f), ("tWR", 0x50218, 0, 0xff), ("tREFI", 0x50230, 0, 0xffff)
    ];

    private RyzenSmu? _smu;
    private AmdFamily17? _amd;
    private uint? _version;
    private int[]? _coresByCcd;
    private (int Slot, int Core, int Ccd)[]? _coreMap;

    internal static bool IsSupportedProcessor() =>
        CpuId.Get(0, 0) is { Vendor: Vendor.AMD, Family: 0x19, Model: 0x61 };

    internal float[]? ReadPmTable(out string? error)
    {
        error = null;
        try
        {
            if (_version is null)
            {
                _smu ??= new RyzenSmu();
                _smu.ResolvePmTable(out uint version, out _);
                _version = version;
            }
            if (_version != DualCcdPmTableVersion)
                return null;

            // The SMU updates its telemetry buffer without changing processor settings.
            _smu!.UpdatePmTable();
            const int tableCells = 559;
            long[] packed = _smu.ReadPmTable((tableCells + 1) / 2);
            var table = new float[tableCells];
            Buffer.BlockCopy(packed, 0, table, 0, table.Length * sizeof(float));
            return table;
        }
        catch (Exception exception)
        {
            error = $"AMD SMU: {exception.Message}";
            return null;
        }
    }

    internal void ExtendCpu(IHardware hardware, float[]? table, List<Sensor> sensors, List<Sensor> baseSensors,
        List<string> errors)
    {
        string hardwareId = hardware.Identifier.ToString();
        CollectThermalStatus(hardwareId, sensors, errors);
        if (table is null)
            return;
        try
        {
            string id = hardwareId + "/smu";
            string limitsId = id + "/limits";
            const string limitsName = "CPU limits (SMU)";
            AddLimit("PPT", Metric.Power, 2, 3);
            AddLimit("TDC", Metric.Current, 8, 9);
            AddLimit("EDC", Metric.Current, 61, 62);
            AddLimit("Thermal", Metric.Temperature, 10, 11, 150);

            // The LHM 0.9.6 fields at 48 and 49 are the core rail's current and power.
            // Keep the existing IDs when correcting those two rows.
            for (int i = 0; i < baseSensors.Count; i++)
            {
                Sensor sensor = baseSensors[i];
                if (sensor.Metric == Metric.Current && sensor.Name == "TDC")
                    baseSensors[i] = sensor with { Name = "CPU core rail current", Provider = Provider.AmdSmu,
                        Value = Read(table, 48, 0, 1500) };
                if (sensor.Metric == Metric.Current && sensor.Name == "EDC")
                    baseSensors[i] = sensor with { Name = "CPU core rail power", Metric = Metric.Power,
                        Provider = Provider.AmdSmu, Value = Read(table, 49, 0, 1500) };
            }
            Add(sensors, Provider.AmdSmu, id + "/rails", "CPU rails (SMU)", Metric.Current, "soc-current",
                "SoC rail current", Read(table, 53, 0, 1500));
            Add(sensors, Provider.AmdSmu, id + "/rails", "CPU rails (SMU)", Metric.Current, "misc-current",
                "Misc rail current", Read(table, 58, 0, 1500));

            _coreMap ??= MapCores(table);
            if (_coreMap is not null)
            {
                double? activeCores = 0;
                int activityIndex = sensors.Count;
                foreach (var (slot, core, ccd) in _coreMap)
                {
                    string ccdId = id + $"/ccd/{ccd}";
                    string ccdName = $"CCD {ccd + 1}";
                    Add(sensors, Provider.AmdSmu, ccdId, ccdName,
                        Metric.Temperature, $"temperature/{slot}", $"Core {core}",
                        Read(table, CoreTemperature + slot, 1, 150));
                    activeCores += AddCoreResidencies(table, slot, core, ccdId, ccdName, sensors);
                }
                string activityId = id + "/activity";
                sensors.Insert(activityIndex, new Sensor(Metric.AverageActiveCoreCount, activityId, "Core activity (SMU)",
                    activityId + "/average-active-cores", "Average active core count", activeCores, Provider.AmdSmu));
                foreach (int ccd in _coreMap.Select(core => core.Ccd).Distinct())
                {
                    Add(sensors, Provider.AmdSmu, id + $"/ccd/{ccd}", $"CCD {ccd + 1}",
                        Metric.Clock, "l3-clock", "L3 clock",
                        Read(table, L3Clock + ccd, 0.1, 10) * 1000);
                }
            }

            void AddLimit(string name, Metric metric, int limitCell, int currentCell, double upperBound = 1500)
            {
                double? limit = Read(table, limitCell, 1, upperBound);
                double? current = Read(table, currentCell, 0, upperBound);
                if (limit is null)
                    return;
                string key = name.ToLowerInvariant();
                if (name != "PPT" || !baseSensors.Any(sensor =>
                        sensor.Metric == Metric.Power && sensor.Name == "CPU PPT" && sensor.Value.HasValue))
                    Add(sensors, Provider.AmdSmu, limitsId, limitsName, metric, key + "-current", name, current);
                Add(sensors, Provider.AmdSmu, limitsId, limitsName, metric, key + "-limit", name + " limit", limit);
                Add(sensors, Provider.AmdSmu, limitsId, limitsName, Metric.Load, key + "-usage", name + " usage",
                    current / limit * 100);
            }
        }
        catch (Exception exception)
        {
            errors.Add($"AMD SMU: {exception.Message}");
        }
    }

    private static double? AddCoreResidencies(float[] table, int slot, int core, string deviceId, string deviceName,
        List<Sensor> sensors)
    {
        double? c0 = Read(table, CoreC0 + slot, 0, 100.05);
        double? c1 = Read(table, CoreC1 + slot, 0, 100.05);
        double? c6 = Read(table, CoreC6 + slot, 0, 100.05);
        double? total = c0 + c1 + c6;
        // The three states partition the same measurement window. Allow for telemetry rounding.
        bool valid = total is >= 99.0 and <= 101.0;
        AddState("C0", c0);
        AddState("C1", c1);
        AddState("C6", c6);
        return valid && c0.HasValue ? Math.Min(c0.Value, 100) / 100 : null;

        void AddState(string state, double? value) =>
            Add(sensors, Provider.AmdSmu, deviceId, deviceName, Metric.CStateResidency,
                $"residency/{slot}/{state}", $"Core {core} · {state}",
                valid && value.HasValue ? Math.Min(value.Value, 100) : null);
    }

    private (int Slot, int Core, int Ccd)[]? MapCores(float[] table)
    {
        // CPUID reports compact enabled core IDs, whereas each SMU CCD has eight physical
        // slots, including fused-off cores. Cross-check each CCD against the OS's core count.
        _coresByCcd ??= Enumerable.Range(0, Math.Min(Environment.ProcessorCount, 64))
            .Select(thread => CpuId.Get(0, thread))
            .Where(id => id is { ExtData.Length: > 0 } && id.ExtData.GetLength(0) > 0x1e)
            .Select(id => (int)(id!.ExtData[0x1e, 1] & 0xff))
            .Where(core => core < 2 * CoresPerCcd)
            .Distinct()
            .Order()
            .ToArray();

        var map = new List<(int, int, int)>();
        int coreNumber = 0;
        for (int ccd = 0; ccd < 2; ccd++)
        {
            int expected = _coresByCcd.Count(core => core / CoresPerCcd == ccd);
            var slots = Enumerable.Range(ccd * CoresPerCcd, CoresPerCcd)
                .Where(slot => Read(table, CoreVoltage + slot, 0.1, 2) is not null &&
                               Read(table, CoreClock + slot, 0.1, 10) is not null)
                .ToArray();
            if (slots.Length != expected)
                return null;
            foreach (int slot in slots)
                map.Add((slot, coreNumber++, ccd));
        }
        return map.Count == 0 ? null : map.ToArray();
    }

    private void CollectThermalStatus(string hardwareId, List<Sensor> sensors, List<string> errors)
    {
        uint? register = null;
        try
        {
            WithSmn(amd =>
            {
                uint status = amd.ReadSmn(HtcRegister);
                if (status != 0 && status != uint.MaxValue)
                    register = status;
            });
        }
        catch (Exception exception)
        {
            errors.Add($"AMD THM: {exception.Message}");
        }

        string id = hardwareId + "/thm";
        AddFlag("htc", "Thermal throttling (HTC)", HtcActive);
        AddFlag("prochot-cpu", "Thermal throttling (PROCHOT CPU)", InternalProchot);
        AddFlag("prochot-ext", "Thermal throttling (PROCHOT EXT)", ExternalProchot);

        void AddFlag(string key, string name, uint mask) =>
            Add(sensors, Provider.AmdThermalController, id, "Thermal throttling", Metric.Flag, key, name,
                register is { } value ? ((value & mask) != 0 ? 1.0 : 0.0) : null);
    }

    internal void CollectMemory(float[]? table, List<Sensor> sensors, List<string> errors)
    {
        if (table is not null)
        {
            const string id = "/amd/smu/dram";
            const string name = "DRAM bandwidth";
            // Validate the memory clock before accepting even a zero bandwidth sample.
            bool valid = Read(table, 78, 1000, 6000).HasValue;
            Add(sensors, Provider.AmdSmu, id, name, Metric.BandwidthGibPerSecond, "read", "DRAM read bandwidth",
                valid ? Read(table, DramReadBandwidth, 0, 256) : null);
            Add(sensors, Provider.AmdSmu, id, name, Metric.BandwidthGibPerSecond, "write", "DRAM write bandwidth",
                valid ? Read(table, DramWriteBandwidth, 0, 256) : null);
        }

        try
        {
            WithSmn(amd =>
            {
                for (uint channel = 0; channel < 2; channel++)
                    ReadMemoryChannel(amd, sensors, channel);
            });
        }
        catch (Exception exception)
        {
            errors.Add($"AMD UMC: {exception.Message}");
        }
    }

    private void WithSmn(Action<AmdFamily17> read)
    {
        _amd ??= new AmdFamily17();
        using var pci = Mutex.OpenExisting(@"Global\Access_PCI");
        try
        {
            if (!pci.WaitOne(300))
                throw new TimeoutException("Timed out waiting for the PCI access mutex.");
        }
        catch (AbandonedMutexException)
        {
            // The abandoned mutex is now owned by this thread.
        }
        try
        {
            read(_amd);
        }
        finally
        {
            pci.ReleaseMutex();
        }
    }

    private static void ReadMemoryChannel(AmdFamily17 amd, List<Sensor> sensors, uint channel)
    {
        uint ReadRegister(uint address) => amd.ReadSmn((channel << 20) | address);
        uint configuration = ReadRegister(0x50200);
        uint clock = configuration & 0xffff;
        if (clock is < 1000 or > 6000)
            return;
        var registers = new Dictionary<uint, uint>();
        foreach (var (_, address, _, _) in Timings)
        {
            if (!registers.ContainsKey(address))
                registers.Add(address, ReadRegister(address));
        }
        if (registers[0x50204] is 0 or uint.MaxValue || registers[0x50208] is 0 or uint.MaxValue)
            return;

        string id = $"/amd/umc/{channel}";
        string name = $"DDR5 · UMC {channel}";
        Add(sensors, Provider.AmdUmc, id, name, Metric.Clock, "mclk", "Memory clock", clock);
        Add(sensors, Provider.AmdUmc, id, name, Metric.TransferRate, "rate", "Memory transfer rate", 2 * clock);
        Timing("Command rate", 1 + ((configuration >> 17) & 1));
        foreach (var (timing, address, shift, mask) in Timings)
        {
            uint value = registers[address];
            if (value != uint.MaxValue)
                Timing(timing, (value >> shift) & mask);
        }

        for (uint rank = 0; rank < 4; rank++)
        {
            uint refresh = ReadRegister(0x50260 + 4 * rank);
            if (refresh is 0 or 0x00c00138 or uint.MaxValue)
                continue;
            Timing("tRFC1", refresh & 0xffff);
            Timing("tRFC2", refresh >> 16);
            uint sameBank = ReadRegister(0x502c0 + 4 * rank);
            if (sameBank != uint.MaxValue)
                Timing("tRFCsb", sameBank & 0x7ff);
            break;
        }

        void Timing(string timing, uint cycles)
        {
            if (cycles > 0)
                Add(sensors, Provider.AmdUmc, id, name, Metric.TimingCycles, timing, timing, cycles);
        }
    }

    private static double? Read(float[] table, int index, double minimum, double maximum)
    {
        double value = table[index];
        return value >= minimum && value <= maximum ? value : null;
    }

    private static void Add(List<Sensor> sensors, Provider provider, string device, string name, Metric metric,
        string id, string label, double? value) =>
        sensors.Add(new Sensor(metric, device, name, $"{device}/{id}", label, value, provider));

    public void Dispose()
    {
        _smu?.Close();
        _amd?.Close();
    }
}
