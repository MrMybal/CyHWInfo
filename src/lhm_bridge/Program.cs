using System.Runtime.InteropServices;
using System.Security.Principal;
using System.Text;
using LibreHardwareMonitor.Hardware;
using LibreHardwareMonitor.PawnIo;
using Microsoft.Win32.SafeHandles;

namespace CyHWInfo.SensorBridge;

internal static class Program
{
    private const uint ProtocolVersion = 6;
    private const byte Ready = 1;
    private const byte Sample = 2;
    private const byte Error = 3;
    private const int MaximumFrameLength = 1024 * 1024;

    // Must match the wire values of cyhwinfo::SensorMetric.
    internal enum Metric : byte
    {
        Temperature, Power, Voltage, FanSpeed, Current, Clock, Load, Frequency, Flow, Control, Level, Factor,
        Data, SmallData, Throughput, TimeSpan, Timing, Energy, Noise, Conductivity, Humidity,
        TimingCycles, TransferRate, Flag, BandwidthGibPerSecond, CStateResidency, AverageActiveCoreCount
    }

    internal enum Provider : byte { LibreHardwareMonitor, AmdSmu, AmdUmc, AmdThermalController }

    internal sealed record Sensor(
        Metric Metric, string DeviceId, string DeviceName, string Id, string Name, double? Value,
        Provider Provider = Provider.LibreHardwareMonitor);

    private static int Main(string[] args)
    {
        if (args.Length != 3 || args.Any(arg => arg != "0" && arg != "1") || args.All(arg => arg == "0"))
            return 2;

        using var output = new BinaryWriter(Console.OpenStandardOutput());
        using var input = Console.OpenStandardInput();
        Console.SetOut(TextWriter.Null);

        bool cpu = args[0] == "1";
        bool motherboard = args[1] == "1";
        bool memory = args[2] == "1";
        var computer = new Computer { IsCpuEnabled = cpu, IsMotherboardEnabled = motherboard };
        try
        {
            computer.Open();
            using var identity = WindowsIdentity.GetCurrent();
            bool elevated = new WindowsPrincipal(identity).IsInRole(WindowsBuiltInRole.Administrator);
            bool pawnIoInstalled = PawnIo.IsInstalled;
            bool pawnIoAccessible = pawnIoInstalled && CanOpenPawnIo();
            using var raphael = pawnIoAccessible && (cpu || memory) && RaphaelSensors.IsSupportedProcessor()
                ? new RaphaelSensors()
                : null;
            Send(output, Ready, writer => writer.Write(ProtocolVersion));

            while (input.ReadByte() == 'S')
            {
                try
                {
                    Send(output, Sample, writer =>
                    {
                        writer.Write(elevated);
                        writer.Write(pawnIoInstalled);
                        writer.Write(pawnIoAccessible);
                        string? tableError = null;
                        float[]? table = raphael?.ReadPmTable(out tableError);
                        WriteGroup(writer, cpu, (sensors, errors) =>
                        {
                            if (tableError is not null)
                                errors.Add(tableError);
                            foreach (var hardware in computer.Hardware.Where(h => h.HardwareType == HardwareType.Cpu))
                            {
                                var baseSensors = new List<Sensor>();
                                Collect(hardware, HardwareType.Cpu, pawnIoAccessible, baseSensors, errors);
                                raphael?.ExtendCpu(hardware, table, sensors, baseSensors, errors);
                                sensors.AddRange(baseSensors);
                            }
                        });
                        WriteGroup(writer, motherboard, (sensors, errors) =>
                        {
                            foreach (var hardware in computer.Hardware.Where(h => h.HardwareType == HardwareType.Motherboard))
                                Collect(hardware, HardwareType.Motherboard, pawnIoAccessible, sensors, errors);
                        });
                        WriteGroup(writer, memory, (sensors, errors) =>
                        {
                            if (tableError is not null && !cpu)
                                errors.Add(tableError);
                            raphael?.CollectMemory(table, sensors, errors);
                        });
                    });
                }
                catch (Exception exception) when (exception is not IOException)
                {
                    Send(output, Error, writer => WriteString(writer, exception.Message));
                }
            }
            return 0;
        }
        catch (Exception exception)
        {
            try
            {
                Send(output, Error, writer => WriteString(writer, exception.Message));
            }
            catch (IOException)
            {
                // The native caller may have timed out or closed the pipe.
            }
            return 1;
        }
        finally
        {
            computer.Close();
        }
    }

    private static bool CanOpenPawnIo()
    {
        const uint readWriteAccess = 0xc0000000;
        const uint shareReadWrite = 3;
        const uint openExisting = 3;
        using SafeFileHandle handle = CreateFileW(@"\\?\GLOBALROOT\Device\PawnIO", readWriteAccess,
            shareReadWrite, IntPtr.Zero, openExisting, 0, IntPtr.Zero);
        return !handle.IsInvalid;
    }

    [DllImport("kernel32.dll", CharSet = CharSet.Unicode, ExactSpelling = true)]
    private static extern SafeFileHandle CreateFileW(string path, uint access, uint share, IntPtr attributes,
        uint disposition, uint flags, IntPtr template);

    private static void WriteGroup(BinaryWriter writer, bool enabled, Action<List<Sensor>, List<string>> collect)
    {
        writer.Write(enabled);
        if (!enabled)
            return;

        var sensors = new List<Sensor>();
        var errors = new List<string>();
        collect(sensors, errors);

        WriteString(writer, string.Join("\n", errors));
        writer.Write((uint)sensors.Count);
        foreach (var sensor in sensors)
        {
            writer.Write((byte)sensor.Metric);
            writer.Write((byte)sensor.Provider);
            WriteString(writer, sensor.DeviceId);
            WriteString(writer, sensor.DeviceName);
            WriteString(writer, sensor.Id);
            WriteString(writer, sensor.Name);
            writer.Write(sensor.Value.HasValue);
            if (sensor.Value is { } value)
                writer.Write(value);
        }
    }

    private static void Collect(IHardware hardware, HardwareType rootType, bool pawnIoAccessible,
        List<Sensor> sensors, List<string> errors)
    {
        try
        {
            string deviceId = hardware.Identifier.ToString();
            // CPU and LPC/EC reads in LHM can return zero-filled data if PawnIO is inaccessible.
            bool requiresPawnIo = rootType == HardwareType.Cpu ||
                hardware.HardwareType == HardwareType.EmbeddedController ||
                (hardware.HardwareType == HardwareType.SuperIO &&
                    !deviceId.StartsWith("/lpc/ipmi/", StringComparison.Ordinal));
            bool canRead = pawnIoAccessible || !requiresPawnIo;
            if (canRead)
                hardware.Update();
            foreach (var sensor in hardware.Sensors)
            {
                Metric? metric = sensor.SensorType switch
                {
                    SensorType.Temperature => Metric.Temperature,
                    SensorType.Power => Metric.Power,
                    SensorType.Voltage => Metric.Voltage,
                    SensorType.Fan => Metric.FanSpeed,
                    SensorType.Current => Metric.Current,
                    SensorType.Clock => Metric.Clock,
                    SensorType.Load => Metric.Load,
                    SensorType.Frequency => Metric.Frequency,
                    SensorType.Flow => Metric.Flow,
                    SensorType.Control => Metric.Control,
                    SensorType.Level => Metric.Level,
                    SensorType.Factor => Metric.Factor,
                    SensorType.Data => Metric.Data,
                    SensorType.SmallData => Metric.SmallData,
                    SensorType.Throughput => Metric.Throughput,
                    SensorType.TimeSpan => Metric.TimeSpan,
                    SensorType.Timing => Metric.Timing,
                    SensorType.Energy => Metric.Energy,
                    SensorType.Noise => Metric.Noise,
                    SensorType.Conductivity => Metric.Conductivity,
                    SensorType.Humidity => Metric.Humidity,
                    _ => null,
                };
                if (metric is { } kind)
                {
                    var value = canRead ? sensor.Value : null;
                    sensors.Add(new Sensor(kind, deviceId, hardware.Name,
                        sensor.Identifier.ToString(), sensor.Name,
                        value is { } reading && float.IsFinite(reading) ? reading : null));
                }
            }
        }
        catch (Exception exception)
        {
            errors.Add($"{hardware.Name}: {exception.Message}");
        }

        foreach (var child in hardware.SubHardware)
            Collect(child, rootType, pawnIoAccessible, sensors, errors);
    }

    private static void Send(BinaryWriter output, byte type, Action<BinaryWriter> write)
    {
        using var frame = new MemoryStream();
        using var writer = new BinaryWriter(frame);
        writer.Write(type);
        write(writer);
        if (frame.Length > MaximumFrameLength)
            throw new InvalidDataException("The sensor frame exceeds the protocol limit.");

        output.Write((uint)frame.Length);
        output.Write(frame.GetBuffer(), 0, (int)frame.Length);
        output.Flush();
    }

    private static void WriteString(BinaryWriter writer, string text)
    {
        byte[] bytes = Encoding.UTF8.GetBytes(text);
        writer.Write((uint)bytes.Length);
        writer.Write(bytes);
    }
}
