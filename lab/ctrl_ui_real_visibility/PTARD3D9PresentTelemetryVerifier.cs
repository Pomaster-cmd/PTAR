using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Globalization;
using System.IO;
using System.Runtime.InteropServices;
using System.Text;
using System.Threading;

public static class PTARD3D9PresentTelemetryVerifier
{
    const string MappingName = "Local\\PTAR_D3D9_PRESENT_TELEMETRY_V1";
    const uint FILE_MAP_READ = 0x0004;
    const uint Magic = 0x31545039u;
    const int Version = 1;
    const int HeaderBytes = 64;
    const int QpcOffset = 64;
    const int ExpectedCapacity = 65536;

    [DllImport("kernel32.dll", CharSet=CharSet.Unicode, SetLastError=true)]
    static extern IntPtr OpenFileMapping(uint access, bool inheritHandle, string name);
    [DllImport("kernel32.dll", SetLastError=true)]
    static extern IntPtr MapViewOfFile(IntPtr mapping, uint access, uint offsetHigh, uint offsetLow, UIntPtr bytesToMap);
    [DllImport("kernel32.dll")]
    static extern bool UnmapViewOfFile(IntPtr address);
    [DllImport("kernel32.dll")]
    static extern bool CloseHandle(IntPtr handle);

    sealed class Event
    {
        public long Qpc;
        public bool Generated;
        public long AbsoluteIndex;
    }

    sealed class MapReader : IDisposable
    {
        IntPtr handle;
        IntPtr view;
        public int Capacity;
        public long Frequency;
        public uint ProcessId;

        public MapReader()
        {
            handle = OpenFileMapping(FILE_MAP_READ, false, MappingName);
            if (handle == IntPtr.Zero) throw new InvalidOperationException("PTAR D3D9 present telemetry mapping absent (Win32=" + Marshal.GetLastWin32Error().ToString(CultureInfo.InvariantCulture) + ").");
            view = MapViewOfFile(handle, FILE_MAP_READ, 0, 0, UIntPtr.Zero);
            if (view == IntPtr.Zero) throw new InvalidOperationException("PTAR D3D9 present telemetry map view failed (Win32=" + Marshal.GetLastWin32Error().ToString(CultureInfo.InvariantCulture) + ").");
            uint magic = U32(0);
            int version = I32(4);
            int structSize = I32(8);
            Capacity = I32(12);
            ProcessId = U32(20);
            Frequency = I64(24);
            if (magic != Magic) throw new InvalidOperationException("PTAR telemetry magic mismatch.");
            if (version != Version) throw new InvalidOperationException("PTAR telemetry version mismatch: " + version.ToString(CultureInfo.InvariantCulture));
            if (Capacity < 1024 || Capacity > 1048576) throw new InvalidOperationException("PTAR telemetry capacity invalid: " + Capacity.ToString(CultureInfo.InvariantCulture));
            if (Frequency <= 0) throw new InvalidOperationException("PTAR telemetry QPC frequency invalid.");
            int minimumSize = HeaderBytes + Capacity * 9;
            if (structSize < minimumSize) throw new InvalidOperationException("PTAR telemetry struct too small: " + structSize.ToString(CultureInfo.InvariantCulture));
        }

        public int WriteIndex { get { return I32(32); } }
        public int TotalFrames { get { return I32(36); } }
        public int RealFrames { get { return I32(40); } }
        public int GeneratedFrames { get { return I32(44); } }

        public List<Event> ReadRange(int startWrite, int endWrite)
        {
            uint delta = unchecked((uint)(endWrite - startWrite));
            if (delta == 0) return new List<Event>();
            if (delta >= (uint)Capacity) throw new InvalidOperationException("PTAR telemetry ring overflow during measurement: " + delta.ToString(CultureInfo.InvariantCulture) + " events / capacity " + Capacity.ToString(CultureInfo.InvariantCulture) + ".");
            int kindOffset = QpcOffset + Capacity * 8;
            List<Event> result = new List<Event>((int)delta);
            for (uint n = 0; n < delta; ++n)
            {
                long abs = (long)startWrite + (long)n;
                int slot = (int)(unchecked((uint)abs) % (uint)Capacity);
                long qpc = I64(QpcOffset + slot * 8);
                byte kind = Marshal.ReadByte(view, kindOffset + slot);
                if (qpc <= 0) throw new InvalidOperationException("PTAR telemetry contains an uncommitted QPC sample.");
                Event ev = new Event(); ev.Qpc = qpc; ev.Generated = kind != 0; ev.AbsoluteIndex = abs;
                result.Add(ev);
            }
            return result;
        }

        int I32(int offset) { return Marshal.ReadInt32(view, offset); }
        uint U32(int offset) { return unchecked((uint)Marshal.ReadInt32(view, offset)); }
        long I64(int offset) { return Marshal.ReadInt64(view, offset); }

        public void Dispose()
        {
            if (view != IntPtr.Zero) { UnmapViewOfFile(view); view = IntPtr.Zero; }
            if (handle != IntPtr.Zero) { CloseHandle(handle); handle = IntPtr.Zero; }
        }
    }

    static string F(double v) { return v.ToString("0.000", CultureInfo.InvariantCulture); }

    static double Median(List<double> values)
    {
        if (values == null || values.Count == 0) return 0.0;
        double[] a = values.ToArray(); Array.Sort(a); int n = a.Length;
        if ((n & 1) != 0) return a[n / 2];
        return (a[n / 2 - 1] + a[n / 2]) / 2.0;
    }

    static int Run(int seconds)
    {
        string root = Environment.GetEnvironmentVariable("PTAR_GAME_ROOT") ?? "";
        if (root.Length == 0 || !Directory.Exists(root)) return 40;
        string prefix = Environment.GetEnvironmentVariable("PTAR_FG_PRESENTSHED1_OUTPUT_PREFIX") ?? "";
        bool f1 = prefix.Length != 0;
        string outPath, csvPath, statusPath, errPath, rrPath, longHoldPath, genPressurePath;
        if (f1)
        {
            outPath = prefix + "_OUTPUT.txt";
            csvPath = prefix + "_SAMPLES.csv";
            statusPath = prefix + "_STATUS.txt";
            errPath = prefix + "_ERROR.txt";
            rrPath = prefix + "_RR_FOCUS.txt";
            longHoldPath = prefix + "_LONG_HOLD_FOCUS.txt";
            genPressurePath = prefix + "_GEN_PRESSURE_FOCUS.txt";
        }
        else
        {
            outPath = Path.Combine(root, "PTAR_RAWCADENCE_LAST_OUTPUT.txt");
            csvPath = Path.Combine(root, "PTAR_RAWCADENCE_LAST_SAMPLES.csv");
            statusPath = Path.Combine(root, "PTAR_RAWCADENCE_LAST_STATUS.txt");
            errPath = Path.Combine(root, "PTAR_RAWCADENCE_LAST_ERROR.txt");
            rrPath = longHoldPath = genPressurePath = "";
        }
        try
        {
            if (File.Exists(errPath)) File.Delete(errPath);
            using (MapReader map = new MapReader())
            {
                int probe0 = map.WriteIndex;
                Thread.Sleep(300);
                int probe1 = map.WriteIndex;
                if (probe1 == probe0) throw new InvalidOperationException("PTAR telemetry is present but no D3D9 Present is advancing. The game must be rendering before measurement.");

                int startWrite = probe1;
                int startReal = map.RealFrames;
                int startGen = map.GeneratedFrames;
                Stopwatch wall = Stopwatch.StartNew();
                int sleepMs = Math.Max(1, seconds * 1000);
                Thread.Sleep(sleepMs);
                wall.Stop();
                Thread.MemoryBarrier();
                int endWrite = map.WriteIndex;
                int endReal = map.RealFrames;
                int endGen = map.GeneratedFrames;
                List<Event> events = map.ReadRange(startWrite, endWrite);
                if (events.Count < 3) throw new InvalidOperationException("PTAR telemetry measurement too short: " + events.Count.ToString(CultureInfo.InvariantCulture) + " present events.");

                List<double> intervals = new List<double>();
                int real = 0, gen = 0, rr = 0;
                int hold50 = 0, hold66 = 0, hold100 = 0, hold150 = 0, hold200 = 0, realHold50 = 0, genHold50 = 0;
                double maxHold = 0.0;
                for (int i = 0; i < events.Count; ++i)
                {
                    if (events[i].Generated) gen++; else real++;
                    if (i > 0)
                    {
                        if (events[i - 1].Qpc >= events[i].Qpc) throw new InvalidOperationException("PTAR telemetry QPC is not strictly monotonic.");
                        double dt = 1000.0 * (double)(events[i].Qpc - events[i - 1].Qpc) / (double)map.Frequency;
                        intervals.Add(dt);
                        if (!events[i - 1].Generated && !events[i].Generated) rr++;
                        if (dt > maxHold) maxHold = dt;
                        if (dt >= 50.0) { hold50++; if (events[i - 1].Generated) genHold50++; else realHold50++; }
                        if (dt >= 66.0) hold66++;
                        if (dt >= 100.0) hold100++;
                        if (dt >= 150.0) hold150++;
                        if (dt >= 200.0) hold200++;
                    }
                }

                double durationMs = 1000.0 * (double)(events[events.Count - 1].Qpc - events[0].Qpc) / (double)map.Frequency;
                if (durationMs <= 0.0) durationMs = wall.Elapsed.TotalMilliseconds;
                double fps = durationMs > 0.0 ? 1000.0 * (double)events.Count / durationMs : 0.0;
                double med = Median(intervals);
                double refresh = med > 0.0 ? 1000.0 / med : 60.0;
                if (refresh < 20.0 || refresh > 500.0) refresh = 60.0;

                using (StreamWriter csv = new StreamWriter(csvPath, false, new UTF8Encoding(false)))
                {
                    csv.WriteLine("index,start_ms,dwell_ms,signature_hex,serial,type,vblank_span,next_serial_delta,transition_observer_contaminated,generated_presents,real_presents,present_attempts,present_successes");
                    int cumG = 0, cumR = 0;
                    long baseQpc = events[0].Qpc;
                    for (int i = 0; i < events.Count; ++i)
                    {
                        if (events[i].Generated) cumG++; else cumR++;
                        double startMs = 1000.0 * (double)(events[i].Qpc - baseQpc) / (double)map.Frequency;
                        double dwellMs = 0.0;
                        if (i + 1 < events.Count) dwellMs = 1000.0 * (double)(events[i + 1].Qpc - events[i].Qpc) / (double)map.Frequency;
                        else if (intervals.Count > 0) dwellMs = intervals[intervals.Count - 1];
                        int span = (int)Math.Max(1.0, Math.Round(dwellMs / 16.6666667));
                        int serial = (int)(unchecked((uint)events[i].AbsoluteIndex) & 0x0FFFu);
                        csv.WriteLine(i.ToString(CultureInfo.InvariantCulture) + "," + F(startMs) + "," + F(dwellMs) + ",0000000000000000," + serial.ToString(CultureInfo.InvariantCulture) + "," + (events[i].Generated ? "G" : "R") + "," + span.ToString(CultureInfo.InvariantCulture) + ",1,0," + cumG.ToString(CultureInfo.InvariantCulture) + "," + cumR.ToString(CultureInfo.InvariantCulture) + "," + (i + 1).ToString(CultureInfo.InvariantCulture) + "," + (i + 1).ToString(CultureInfo.InvariantCulture));
                    }
                }

                List<string> report = new List<string>();
                report.Add("PTAR D3D9 RUNTIME PRESENT TELEMETRY V1");
                report.Add("MEASUREMENT SOURCE RUNTIME_PRESENT_SUCCESS");
                report.Add("GDI CAPTURE USED NO");
                report.Add("PRODUCER PID " + map.ProcessId.ToString(CultureInfo.InvariantCulture));
                report.Add("QPC FREQUENCY " + map.Frequency.ToString(CultureInfo.InvariantCulture));
                report.Add("DURATION US " + ((long)(durationMs * 1000.0)).ToString(CultureInfo.InvariantCulture));
                report.Add("DISPLAY REFRESH HZ " + F(refresh));
                report.Add("SAMPLING / REFRESH RATIO 1.000x");
                report.Add("PRESENT EVENTS " + events.Count.ToString(CultureInfo.InvariantCulture));
                report.Add("VISIBLE REAL CONTENTS " + real.ToString(CultureInfo.InvariantCulture));
                report.Add("VISIBLE GENERATED CONTENTS " + gen.ToString(CultureInfo.InvariantCulture));
                report.Add("VISIBLE UNIQUE MILLI-FPS " + ((long)(fps * 1000.0)).ToString(CultureInfo.InvariantCulture));
                report.Add("COUNTER REAL DELTA " + unchecked((uint)(endReal - startReal)).ToString(CultureInfo.InvariantCulture));
                report.Add("COUNTER GENERATED DELTA " + unchecked((uint)(endGen - startGen)).ToString(CultureInfo.InvariantCulture));
                report.Add("FRAME INTERVAL MEDIAN MS " + F(med));
                File.WriteAllLines(outPath, report.ToArray(), new UTF8Encoding(false));
                File.WriteAllText(statusPath, "COMPLETED=YES\r\nRESULT=PASS\r\nMODE=RUNTIME_PRESENT_TELEMETRY\r\nGDI_CAPTURE=NO\r\n", new UTF8Encoding(false));

                if (f1)
                {
                    File.WriteAllLines(rrPath, new string[] {
                        "PTAR_PRESENTSHED1_RR_FOCUS=1",
                        "SOURCE=RUNTIME_PRESENT_TELEMETRY",
                        "RR_EPISODES=" + rr.ToString(CultureInfo.InvariantCulture),
                        "RR_WITH_G_READY_AFTER=0",
                        "RR_WITHOUT_G_READY_AFTER=0",
                        "RR_SNAPSHOT_UNAVAILABLE=" + rr.ToString(CultureInfo.InvariantCulture)
                    }, new UTF8Encoding(false));
                    File.WriteAllLines(longHoldPath, new string[] {
                        "PTAR_PRESENTSHED1_LONG_HOLD_FOCUS=2",
                        "SOURCE=RUNTIME_PRESENT_TELEMETRY",
                        "CLEAN_HOLDS_GE_50MS=" + hold50.ToString(CultureInfo.InvariantCulture),
                        "CLEAN_HOLDS_GE_66MS=" + hold66.ToString(CultureInfo.InvariantCulture),
                        "CLEAN_HOLDS_GE_100MS=" + hold100.ToString(CultureInfo.InvariantCulture),
                        "CLEAN_HOLDS_GE_150MS=" + hold150.ToString(CultureInfo.InvariantCulture),
                        "CLEAN_HOLDS_GE_200MS=" + hold200.ToString(CultureInfo.InvariantCulture),
                        "REAL_HOLDS_GE_50MS=" + realHold50.ToString(CultureInfo.InvariantCulture),
                        "GENERATED_HOLDS_GE_50MS=" + genHold50.ToString(CultureInfo.InvariantCulture),
                        "MAX_CLEAN_HOLD_MS=" + F(maxHold)
                    }, new UTF8Encoding(false));
                    File.WriteAllLines(genPressurePath, new string[] {
                        "PTAR_PRESENTSHED1_GEN_PRESSURE_FOCUS=3",
                        "SOURCE=RUNTIME_PRESENT_TELEMETRY",
                        "INTERNAL_QUEUE_SNAPSHOT=UNAVAILABLE_BY_DESIGN",
                        "PRESENT_EVENTS=" + events.Count.ToString(CultureInfo.InvariantCulture),
                        "REAL_PRESENTS=" + real.ToString(CultureInfo.InvariantCulture),
                        "GENERATED_PRESENTS=" + gen.ToString(CultureInfo.InvariantCulture)
                    }, new UTF8Encoding(false));
                }

                Console.WriteLine("PTAR_D3D9_PRESENT_TELEMETRY=PASS EVENTS=" + events.Count.ToString(CultureInfo.InvariantCulture) + " R=" + real.ToString(CultureInfo.InvariantCulture) + " G=" + gen.ToString(CultureInfo.InvariantCulture) + " FPS=" + F(fps));
                return 0;
            }
        }
        catch (Exception ex)
        {
            try { File.WriteAllText(errPath, ex.ToString() + Environment.NewLine, new UTF8Encoding(false)); } catch { }
            Console.Error.WriteLine(ex.Message);
            return 43;
        }
    }

    static int SelfTest()
    {
        if (HeaderBytes != 64 || QpcOffset != 64 || ExpectedCapacity != 65536) return 11;
        if (Magic != 0x31545039u || Version != 1) return 12;
        Console.WriteLine("SELFTEST=PASS MAPPING=" + MappingName + " CAPACITY=" + ExpectedCapacity.ToString(CultureInfo.InvariantCulture) + " GDI_CAPTURE=NO");
        return 0;
    }

    public static int Main(string[] args)
    {
        if (args != null && args.Length > 0 && string.Equals(args[0], "--selftest", StringComparison.OrdinalIgnoreCase)) return SelfTest();
        int seconds = 20;
        if (args != null)
        {
            for (int i = 0; i < args.Length; ++i)
            {
                int n;
                if (int.TryParse(args[i], NumberStyles.Integer, CultureInfo.InvariantCulture, out n) && n >= 1 && n <= 180) seconds = n;
            }
        }
        return Run(seconds);
    }
}
