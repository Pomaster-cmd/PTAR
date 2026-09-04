using System;
using System.Collections.Generic;
using System.Globalization;
using System.IO;
using System.Runtime.InteropServices;
using System.Threading;

public static class PTARVisiblePacingVerifier
{
    const int MARKER_X = 176;
    const int MARKER_Y = 160;
    const int MARKER_W = 158;
    const int MARKER_H = 10;
    const int CELL_COUNT = 16;
    const int CELL_STEP = 10;
    const int CELL_W = 8;
    const int SRCCOPY = 0x00CC0020;
    const int VREFRESH = 116;
    const uint WAIT_OBJECT_0 = 0;
    const int SM_CXSCREEN = 0;
    const int SM_CYSCREEN = 1;
    static bool gProcessDpiAware = false;

    [StructLayout(LayoutKind.Sequential)]
    struct BITMAPINFOHEADER
    {
        public uint biSize;
        public int biWidth;
        public int biHeight;
        public ushort biPlanes;
        public ushort biBitCount;
        public uint biCompression;
        public uint biSizeImage;
        public int biXPelsPerMeter;
        public int biYPelsPerMeter;
        public uint biClrUsed;
        public uint biClrImportant;
    }

    [StructLayout(LayoutKind.Sequential)]
    struct RGBQUAD
    {
        public byte rgbBlue;
        public byte rgbGreen;
        public byte rgbRed;
        public byte rgbReserved;
    }

    [StructLayout(LayoutKind.Sequential)]
    struct BITMAPINFO
    {
        public BITMAPINFOHEADER bmiHeader;
        public RGBQUAD bmiColors;
    }

    sealed class UniqueFrame
    {
        public ushort Sig;
        public int Serial;
        public bool Generated;
        public double StartMs;
        public UniqueFrame(ushort sig, int serial, bool generated, double startMs)
        {
            Sig = sig; Serial = serial; Generated = generated; StartMs = startMs;
        }
    }

    sealed class SecondStats
    {
        public int Cap, Valid, Unique, G, R, Dup, Gaps, Bad;
    }

    [DllImport("user32.dll")]
    static extern IntPtr GetDC(IntPtr hWnd);
    [DllImport("user32.dll")]
    static extern int ReleaseDC(IntPtr hWnd, IntPtr hDC);
    [DllImport("user32.dll")]
    static extern short GetAsyncKeyState(int vKey);
    [DllImport("user32.dll")]
    static extern bool SetProcessDPIAware();
    [DllImport("user32.dll")]
    static extern bool IsProcessDPIAware();
    [DllImport("user32.dll")]
    static extern int GetSystemMetrics(int nIndex);
    [DllImport("gdi32.dll")]
    static extern IntPtr CreateCompatibleDC(IntPtr hdc);
    [DllImport("gdi32.dll")]
    static extern bool DeleteDC(IntPtr hdc);
    [DllImport("gdi32.dll")]
    static extern bool DeleteObject(IntPtr hObject);
    [DllImport("gdi32.dll")]
    static extern IntPtr SelectObject(IntPtr hdc, IntPtr hgdiobj);
    [DllImport("gdi32.dll")]
    static extern bool BitBlt(IntPtr hdcDest, int nXDest, int nYDest, int nWidth, int nHeight,
                              IntPtr hdcSrc, int nXSrc, int nYSrc, int dwRop);
    [DllImport("gdi32.dll")]
    static extern IntPtr CreateDIBSection(IntPtr hdc, ref BITMAPINFO pbmi, uint iUsage,
                                           out IntPtr ppvBits, IntPtr hSection, uint dwOffset);
    [DllImport("gdi32.dll")]
    static extern int GetDeviceCaps(IntPtr hdc, int nIndex);
    [DllImport("kernel32.dll", SetLastError=true)]
    static extern IntPtr CreateWaitableTimer(IntPtr lpTimerAttributes, bool bManualReset, string lpTimerName);
    [DllImport("kernel32.dll", SetLastError=true)]
    static extern bool SetWaitableTimer(IntPtr hTimer, ref long pDueTime, int lPeriod,
                                        IntPtr pfnCompletionRoutine, IntPtr lpArgToCompletionRoutine, bool fResume);
    [DllImport("kernel32.dll")]
    static extern uint WaitForSingleObject(IntPtr hHandle, uint dwMilliseconds);
    [DllImport("kernel32.dll")]
    static extern bool CloseHandle(IntPtr hObject);
    [DllImport("kernel32.dll")]
    static extern bool QueryPerformanceCounter(out long lpPerformanceCount);
    [DllImport("kernel32.dll")]
    static extern bool QueryPerformanceFrequency(out long lpFrequency);
    [DllImport("kernel32.dll")]
    static extern bool Beep(uint dwFreq, uint dwDuration);
    [DllImport("winmm.dll")]
    static extern uint timeBeginPeriod(uint uPeriod);
    [DllImport("winmm.dll")]
    static extern uint timeEndPeriod(uint uPeriod);

    static void SafeBeep(uint frequency, uint durationMs)
    {
        try { Beep(frequency, durationMs); } catch { }
    }

    static bool EnsureDpiAware()
    {
        try { SetProcessDPIAware(); } catch { }
        try { gProcessDpiAware = IsProcessDPIAware(); } catch { gProcessDpiAware = false; }
        return gProcessDpiAware;
    }

    static ushort EncodeMarkerForSelfTest(int serial, bool generated)
    {
        int b = serial & 0x0FFF;
        int gray = (b ^ (b >> 1)) & 0x0FFF;
        int sig = gray | (generated ? 0x1000 : 0) | 0x4000;
        if ((PopCount(sig & 0x1FFF) & 1) != 0) sig |= 0x2000;
        return (ushort)sig;
    }

    public static int SelfTest()
    {
        if (!EnsureDpiAware())
        {
            Console.WriteLine("SELFTEST=FAIL DPI_AWARE=NO");
            return 3;
        }
        int[] serials = new int[] { 0, 1, 2, 17, 1234, 2047, 2048, 4094, 4095 };
        for (int i = 0; i < serials.Length; i++)
        {
            for (int t = 0; t < 2; t++)
            {
                bool gen = t != 0;
                ushort sig = EncodeMarkerForSelfTest(serials[i], gen);
                int dec; bool dgen, sync, parity;
                if (!DecodeMarker(sig, out dec, out dgen, out sync, out parity) ||
                    dec != serials[i] || dgen != gen || !sync || !parity)
                {
                    Console.WriteLine("SELFTEST=FAIL MARKER_ROUNDTRIP");
                    return 4;
                }
            }
        }

        IntPtr dc = IntPtr.Zero;
        try
        {
            dc = GetDC(IntPtr.Zero);
            if (dc == IntPtr.Zero)
            {
                Console.WriteLine("SELFTEST=FAIL GETDC");
                return 5;
            }
            int sw = GetSystemMetrics(SM_CXSCREEN);
            int sh = GetSystemMetrics(SM_CYSCREEN);
            if (sw <= 0 || sh <= 0)
            {
                Console.WriteLine("SELFTEST=FAIL SCREEN_METRICS");
                return 6;
            }
            Console.WriteLine("SELFTEST=PASS DPI_AWARE=YES SCREEN={0}x{1}", sw, sh);
            return 0;
        }
        finally
        {
            if (dc != IntPtr.Zero) ReleaseDC(IntPtr.Zero, dc);
        }
    }

    public static int Main(string[] args)
    {
        if (args != null && args.Length > 0 && string.Equals(args[0], "--selftest", StringComparison.OrdinalIgnoreCase))
            return SelfTest();

        if (!EnsureDpiAware())
        {
            Console.Error.WriteLine("PTAR VBLANK3: impossible d'activer le mode DPI-aware.");
            return 41;
        }

        string gameRoot = Environment.GetEnvironmentVariable("PTAR_GAME_ROOT");
        if (string.IsNullOrWhiteSpace(gameRoot))
        {
            Console.Error.WriteLine("PTAR VBLANK3: PTAR_GAME_ROOT absent.");
            return 42;
        }

        int duration = 20;
        if (args != null && args.Length > 0)
        {
            int parsed;
            if (int.TryParse(args[0], NumberStyles.Integer, CultureInfo.InvariantCulture, out parsed) && parsed >= 5 && parsed <= 120)
                duration = parsed;
        }
        return Run(gameRoot, duration);
    }

    static string F(double v) { return v.ToString("0.000", CultureInfo.InvariantCulture); }

    static int PopCount(int x)
    {
        int n = 0;
        uint u = (uint)x;
        while (u != 0) { n += (int)(u & 1U); u >>= 1; }
        return n;
    }

    static int GrayDecode12(int g)
    {
        int b = 0;
        int x = g & 0x0FFF;
        while (x != 0) { b ^= x; x >>= 1; }
        return b & 0x0FFF;
    }

    static bool DecodeMarker(ushort sig, out int serial, out bool generated, out bool syncOk, out bool parityOk)
    {
        // 16 cells: Gray12 serial [0..11], G/R bit [12], parity [13], guards [14..15].
        // Guard contract observed/validated by the historical VBLANK2 verifier: bit14=1, bit15=0.
        syncOk = (sig & 0xC000) == 0x4000;
        parityOk = (PopCount(sig & 0x3FFF) & 1) == 0;
        serial = GrayDecode12(sig & 0x0FFF);
        generated = (sig & 0x1000) != 0;
        return syncOk && parityOk;
    }

    static bool CaptureSignature(IntPtr screenDC, IntPtr memDC, IntPtr bits, int[] pixels,
                                 out ushort signature, out int contrast)
    {
        signature = 0;
        contrast = 0;
        if (!BitBlt(memDC, 0, 0, MARKER_W, MARKER_H, screenDC, MARKER_X, MARKER_Y, SRCCOPY)) return false;
        Marshal.Copy(bits, pixels, 0, pixels.Length);
        int minMean = 1000000;
        int maxMean = -1;
        for (int cell = 0; cell < CELL_COUNT; cell++)
        {
            long sum = 0;
            int n = 0;
            int x0 = cell * CELL_STEP;
            for (int y = 0; y < MARKER_H; y++)
            {
                int row = y * MARKER_W;
                for (int x = x0; x < x0 + CELL_W; x++)
                {
                    int px = pixels[row + x];
                    int b = px & 255;
                    int g = (px >> 8) & 255;
                    int r = (px >> 16) & 255;
                    sum += (r + g + b) / 3;
                    n++;
                }
            }
            int mean = (int)(sum / n);
            if (mean < minMean) minMean = mean;
            if (mean > maxMean) maxMean = mean;
            if (mean >= 128) signature = (ushort)(signature | (1 << cell));
        }
        contrast = maxMean - minMean;
        return contrast >= 80;
    }

    static double Mean(List<double> values)
    {
        if (values == null || values.Count == 0) return 0.0;
        double s = 0.0;
        for (int i = 0; i < values.Count; i++) s += values[i];
        return s / values.Count;
    }

    static double Percentile(List<double> values, double p)
    {
        if (values == null || values.Count == 0) return 0.0;
        double[] a = values.ToArray();
        Array.Sort(a);
        if (a.Length == 1) return a[0];
        double pos = (a.Length - 1) * p;
        int lo = (int)Math.Floor(pos), hi = (int)Math.Ceiling(pos);
        if (lo == hi) return a[lo];
        double f = pos - lo;
        return a[lo] + (a[hi] - a[lo]) * f;
    }

    static double StdDev(List<double> values, double mean)
    {
        if (values == null || values.Count < 2) return 0.0;
        double s = 0.0;
        for (int i = 0; i < values.Count; i++)
        {
            double d = values[i] - mean;
            s += d * d;
        }
        return Math.Sqrt(s / values.Count);
    }

    static int ModalSpan(List<double> intervals, double refreshMs)
    {
        int[] bins = new int[9];
        for (int i = 0; i < intervals.Count; i++)
        {
            int span = (int)Math.Round(intervals[i] / refreshMs);
            if (span < 1) span = 1;
            if (span > 8) span = 8;
            bins[span]++;
        }
        int best = 1;
        for (int i = 2; i < bins.Length; i++) if (bins[i] > bins[best]) best = i;
        return best;
    }

    public static int Run(string gameRoot, int durationSeconds)
    {
        gameRoot = Path.GetFullPath(gameRoot.Trim().Trim('"'));
        string outPath = Path.Combine(gameRoot, "PTAR_VISIBLE_VERIFIER_LAST_OUTPUT.txt");
        string csvPath = Path.Combine(gameRoot, "PTAR_VISIBLE_VERIFIER_LAST_SAMPLES.csv");
        string statusPath = Path.Combine(gameRoot, "PTAR_VISIBLE_VERIFIER_LAST_STATUS.txt");
        string errPath = Path.Combine(gameRoot, "PTAR_VISIBLE_VERIFIER_LAST_ERROR.txt");
        try { if (File.Exists(outPath)) File.Delete(outPath); } catch { }
        try { if (File.Exists(csvPath)) File.Delete(csvPath); } catch { }
        try { if (File.Exists(errPath)) File.Delete(errPath); } catch { }

        long qpf;
        if (!QueryPerformanceFrequency(out qpf) || qpf <= 0)
        {
            File.WriteAllText(errPath, "QPC frequency unavailable\r\n");
            return 20;
        }

        IntPtr screenDC = IntPtr.Zero, memDC = IntPtr.Zero, dib = IntPtr.Zero, oldObj = IntPtr.Zero, bits = IntPtr.Zero, timer = IntPtr.Zero;
        uint timerRc = 999;
        try
        {
            screenDC = GetDC(IntPtr.Zero);
            if (screenDC == IntPtr.Zero) throw new InvalidOperationException("GetDC failed");
            memDC = CreateCompatibleDC(screenDC);
            if (memDC == IntPtr.Zero) throw new InvalidOperationException("CreateCompatibleDC failed");
            BITMAPINFO bmi = new BITMAPINFO();
            bmi.bmiHeader.biSize = (uint)Marshal.SizeOf(typeof(BITMAPINFOHEADER));
            bmi.bmiHeader.biWidth = MARKER_W;
            bmi.bmiHeader.biHeight = -MARKER_H;
            bmi.bmiHeader.biPlanes = 1;
            bmi.bmiHeader.biBitCount = 32;
            bmi.bmiHeader.biCompression = 0;
            dib = CreateDIBSection(screenDC, ref bmi, 0, out bits, IntPtr.Zero, 0);
            if (dib == IntPtr.Zero || bits == IntPtr.Zero) throw new InvalidOperationException("CreateDIBSection failed");
            oldObj = SelectObject(memDC, dib);
            if (oldObj == IntPtr.Zero) throw new InvalidOperationException("SelectObject failed");

            timerRc = timeBeginPeriod(1);
            timer = CreateWaitableTimer(IntPtr.Zero, false, null);
            if (timer == IntPtr.Zero) throw new InvalidOperationException("CreateWaitableTimer failed");
            long due = -10000;
            if (!SetWaitableTimer(timer, ref due, 1, IntPtr.Zero, IntPtr.Zero, false)) throw new InvalidOperationException("SetWaitableTimer failed");

            int refreshHz = GetDeviceCaps(screenDC, VREFRESH);
            if (refreshHz < 30 || refreshHz > 360) refreshHz = 60;
            double refreshMs = 1000.0 / refreshHz;
            int[] pixels = new int[MARKER_W * MARKER_H];

            Console.WriteLine("P1FG7N-VBLANK3 SINGLE-ENGINE VISIBLE FRAME + PACING VERIFIER");
            Console.WriteLine("Windows 8.1 x64 / ONE GDI capture loop / Gray12 + G/R + parity + guards + QPC pacing");
            int screenWidth = GetSystemMetrics(SM_CXSCREEN);
            int screenHeight = GetSystemMetrics(SM_CYSCREEN);
            Console.WriteLine("SCREEN WIDTH {0}", screenWidth);
            Console.WriteLine("SCREEN HEIGHT {0}", screenHeight);
            Console.WriteLine("PROCESS DPI AWARE {0}", gProcessDpiAware ? "YES" : "NO");
            Console.WriteLine("MARKER X {0}", MARKER_X);
            Console.WriteLine("MARKER Y {0}", MARKER_Y);
            Console.WriteLine("MARKER W {0}", MARKER_W);
            Console.WriteLine("MARKER H {0}", MARKER_H);
            Console.WriteLine("DISPLAY REFRESH HZ {0}", refreshHz);
            Console.WriteLine("ARMED: enable FG, then F5 starts one 20s measurement for counts + pacing.");
            Console.WriteLine("TIMER RESOLUTION 1MS REQUEST ACTIVE");

            bool wasDown = (GetAsyncKeyState(0x74) & 0x8000) != 0;
            while (true)
            {
                bool down = (GetAsyncKeyState(0x74) & 0x8000) != 0;
                if (down && !wasDown) break;
                wasDown = down;
                Thread.Sleep(5);
            }

            // Fast field gate: do not waste a 20-second run if the physical marker is not
            // actually visible at the expected coordinates. The historical native verifier
            // uses physical desktop coordinates; this dedicated EXE is DPI-aware for parity.
            bool markerSeen = false;
            int gateBestContrast = 0;
            for (int probe = 0; probe < 40; probe++)
            {
                ushort gateSig;
                int gateContrast;
                if (CaptureSignature(screenDC, memDC, bits, pixels, out gateSig, out gateContrast))
                {
                    int gateSerial; bool gateGenerated, gateSync, gateParity;
                    DecodeMarker(gateSig, out gateSerial, out gateGenerated, out gateSync, out gateParity);
                    if (gateSync && gateParity) { markerSeen = true; break; }
                }
                if (gateContrast > gateBestContrast) gateBestContrast = gateContrast;
                Thread.Sleep(5);
            }
            if (!markerSeen)
            {
                string markerMsg = string.Format(CultureInfo.InvariantCulture,
                    "Marker not captured at physical coordinates X={0} Y={1}. Best contrast={2}. DPI_AWARE={3}.",
                    MARKER_X, MARKER_Y, gateBestContrast, gProcessDpiAware ? "YES" : "NO");
                File.WriteAllText(errPath, markerMsg + "\r\n");
                Console.WriteLine("[FAIL] {0}", markerMsg);
                SafeBeep(320, 250);
                return 43;
            }

            SafeBeep(900, 140);
            long startQpc;
            QueryPerformanceCounter(out startQpc);
            long endTarget = startQpc + (long)(durationSeconds * (double)qpf);
            long lastCaptureQpc = startQpc;
            Console.WriteLine("MEASUREMENT STARTED: {0} seconds", durationSeconds);

            int attempts = 0, captureFailures = 0, lowContrast = 0, valid = 0, syncFailures = 0, parityFailures = 0;
            int unique = 0, gCount = 0, rCount = 0, duplicates = 0, gaps = 0, badSteps = 0, sameType = 0, maxContrast = 0;
            List<double> captureIntervals = new List<double>();
            List<UniqueFrame> frames = new List<UniqueFrame>();
            SecondStats[] secs = new SecondStats[durationSeconds];
            for (int i = 0; i < secs.Length; i++) secs[i] = new SecondStats();

            bool haveValid = false;
            ushort lastSig = 0;
            int lastSerial = 0;
            bool lastGenerated = false;
            int lastPrintedSec = 0;

            while (true)
            {
                uint wr = WaitForSingleObject(timer, 20);
                long now;
                QueryPerformanceCounter(out now);
                if (now >= endTarget) break;
                if (wr != WAIT_OBJECT_0) continue;
                double ms = (now - startQpc) * 1000.0 / qpf;
                int secIndex = (int)(ms / 1000.0);
                if (secIndex < 0) secIndex = 0;
                if (secIndex >= durationSeconds) secIndex = durationSeconds - 1;

                attempts++;
                secs[secIndex].Cap++;
                double capDt = (now - lastCaptureQpc) * 1000.0 / qpf;
                if (attempts > 1) captureIntervals.Add(capDt);
                lastCaptureQpc = now;

                ushort sig;
                int contrast;
                if (!CaptureSignature(screenDC, memDC, bits, pixels, out sig, out contrast))
                {
                    captureFailures++;
                    if (contrast < 80) lowContrast++;
                    continue;
                }
                if (contrast > maxContrast) maxContrast = contrast;

                int serial;
                bool generated, syncOk, parityOk;
                DecodeMarker(sig, out serial, out generated, out syncOk, out parityOk);
                if (!syncOk) { syncFailures++; secs[secIndex].Bad++; continue; }
                if (!parityOk) { parityFailures++; secs[secIndex].Bad++; continue; }
                valid++;
                secs[secIndex].Valid++;

                if (!haveValid || sig != lastSig)
                {
                    unique++;
                    secs[secIndex].Unique++;
                    if (generated) { gCount++; secs[secIndex].G++; }
                    else { rCount++; secs[secIndex].R++; }

                    if (haveValid)
                    {
                        int delta = (serial - lastSerial) & 0x0FFF;
                        if (delta >= 1 && delta <= 2048)
                        {
                            if (delta > 1) { int add = delta - 1; gaps += add; secs[secIndex].Gaps += add; }
                        }
                        else { badSteps++; secs[secIndex].Bad++; }
                        if (generated == lastGenerated) sameType++;
                    }
                    frames.Add(new UniqueFrame(sig, serial, generated, ms));
                    lastSig = sig;
                    lastSerial = serial;
                    lastGenerated = generated;
                    haveValid = true;
                }
                else
                {
                    duplicates++;
                    secs[secIndex].Dup++;
                }

                int completeSec = (int)(ms / 1000.0);
                while (lastPrintedSec < completeSec && lastPrintedSec < durationSeconds)
                {
                    SecondStats s = secs[lastPrintedSec];
                    Console.WriteLine("SECOND {0} CAP {1} VALID {2} UNIQUE {3} G {4} R {5} DUP {6} GAPS {7} BAD {8}",
                        lastPrintedSec + 1, s.Cap, s.Valid, s.Unique, s.G, s.R, s.Dup, s.Gaps, s.Bad);
                    lastPrintedSec++;
                }
            }

            long endQpc;
            QueryPerformanceCounter(out endQpc);
            double durationMs = (endQpc - startQpc) * 1000.0 / qpf;
            while (lastPrintedSec < durationSeconds)
            {
                SecondStats s = secs[lastPrintedSec];
                Console.WriteLine("SECOND {0} CAP {1} VALID {2} UNIQUE {3} G {4} R {5} DUP {6} GAPS {7} BAD {8}",
                    lastPrintedSec + 1, s.Cap, s.Valid, s.Unique, s.G, s.R, s.Dup, s.Gaps, s.Bad);
                lastPrintedSec++;
            }
            Console.WriteLine("MEASUREMENT COMPLETE");
            SafeBeep(1400, 180);

            List<double> intervals = new List<double>();
            List<double> gDwell = new List<double>();
            List<double> rDwell = new List<double>();
            List<double> gToR = new List<double>();
            List<double> rToG = new List<double>();
            List<double> pairPeriods = new List<double>();
            List<double> pairImbalance = new List<double>();
            int span1=0, span2=0, span3=0, span4=0, span5plus=0;
            int cleanAlternatingTransitions=0;

            for (int i = 1; i < frames.Count; i++)
            {
                double dt = frames[i].StartMs - frames[i-1].StartMs;
                if (dt <= 0.0) continue;
                // Exclude only the first partial dwell from aggregate pacing, like PACINGDIAG1.
                if (i >= 2) intervals.Add(dt);
                if (frames[i-1].Generated) gDwell.Add(dt); else rDwell.Add(dt);
                if (frames[i-1].Generated && !frames[i].Generated) { gToR.Add(dt); cleanAlternatingTransitions++; }
                else if (!frames[i-1].Generated && frames[i].Generated) { rToG.Add(dt); cleanAlternatingTransitions++; }
                int span = (int)Math.Round(dt / refreshMs);
                if (span <= 1) span1++; else if (span == 2) span2++; else if (span == 3) span3++; else if (span == 4) span4++; else span5plus++;
            }

            // Pair only truly alternating adjacent dwell intervals.
            for (int i = 1; i + 1 < frames.Count; i++)
            {
                bool aAlt = frames[i-1].Generated != frames[i].Generated;
                bool bAlt = frames[i].Generated != frames[i+1].Generated;
                if (!aAlt || !bAlt) continue;
                double a = frames[i].StartMs - frames[i-1].StartMs;
                double b = frames[i+1].StartMs - frames[i].StartMs;
                if (a <= 0.0 || b <= 0.0) continue;
                pairPeriods.Add(a+b);
                pairImbalance.Add(100.0 * Math.Abs(a-b)/(a+b));
            }

            double mean = Mean(intervals), median = Percentile(intervals,0.50), p05=Percentile(intervals,0.05), p95=Percentile(intervals,0.95), p99=Percentile(intervals,0.99);
            double min=Percentile(intervals,0.00), max=Percentile(intervals,1.00), std=StdDev(intervals,mean), cv=mean>0?100.0*std/mean:0.0;
            double gMed=Percentile(gDwell,0.50), rMed=Percentile(rDwell,0.50), gP95=Percentile(gDwell,0.95), rP95=Percentile(rDwell,0.95);
            double gtrMed=Percentile(gToR,0.50), rtgMed=Percentile(rToG,0.50);
            double phaseDelta=Math.Abs(gtrMed-rtgMed), phaseAvg=(gtrMed+rtgMed)*0.5;
            double midpointBalance=phaseAvg>0?100.0*(1.0-phaseDelta/phaseAvg):0.0;
            if(midpointBalance<0)midpointBalance=0;if(midpointBalance>100)midpointBalance=100;
            double pairMed=Percentile(pairPeriods,0.50), pairP95=Percentile(pairPeriods,0.95), pairImbMed=Percentile(pairImbalance,0.50), pairImbP95=Percentile(pairImbalance,0.95);
            int modalSpan=ModalSpan(intervals,refreshMs), cadenceMismatch=0, long15=0, long20=0, micro05=0;
            for(int i=0;i<intervals.Count;i++)
            {
                double dt=intervals[i];
                int span=(int)Math.Round(dt/refreshMs); if(span<1)span=1;
                if(span!=modalSpan)cadenceMismatch++;
                if(median>0 && dt>1.5*median)long15++;
                if(median>0 && dt>2.0*median)long20++;
                if(median>0 && dt<0.5*median)micro05++;
            }

            double captureMean=Mean(captureIntervals), captureP95=Percentile(captureIntervals,0.95), captureMax=Percentile(captureIntervals,1.0);
            double captureHz=durationMs>0?attempts*1000.0/durationMs:0.0;
            double samplingRatio=refreshHz>0?captureHz/refreshHz:0.0;
            double visibleFps=durationMs>0?unique*1000.0/durationMs:0.0;
            double gFps=durationMs>0?gCount*1000.0/durationMs:0.0;
            double rFps=durationMs>0?rCount*1000.0/durationMs:0.0;
            double longRatio=intervals.Count>0?100.0*long15/intervals.Count:0.0;
            double mismatchRatio=intervals.Count>0?100.0*cadenceMismatch/intervals.Count:0.0;
            bool samplingGood = samplingRatio >= 0.90;
            bool alternationGood = badSteps==0 && sameType==0 && gaps==0;

            string pacingVerdict;
            if(valid == 0 || frames.Count < 3) pacingVerdict="INVALID - MARKER NOT CAPTURED";
            else if(!samplingGood) pacingVerdict="SAMPLING LIMITED - PACING VERDICT NOT RELIABLE";
            else if(!alternationGood) pacingVerdict="VISIBLE CADENCE HAS DROPS/TYPE BREAKS";
            else if(mismatchRatio <= 5.0 && pairImbP95 <= 10.0 && cv <= 12.0) pacingVerdict="VERY EVEN";
            else if(mismatchRatio <= 12.0 && pairImbP95 <= 20.0 && cv <= 20.0) pacingVerdict="MOSTLY EVEN";
            else if(mismatchRatio <= 25.0 && pairImbP95 <= 35.0) pacingVerdict="UNEVENTFUL FPS AVERAGE BUT NOTICEABLE PACING VARIANCE";
            else pacingVerdict="STRONGLY UNEVEN / JUDDER-LIKE PACING";

            using(StreamWriter csv=new StreamWriter(csvPath,false))
            {
                csv.WriteLine("index,start_ms,dwell_ms,signature_hex,serial,type,vblank_span,next_serial_delta");
                for(int i=0;i<frames.Count;i++)
                {
                    double next=(i+1<frames.Count)?frames[i+1].StartMs:durationMs;
                    double dwell=next-frames[i].StartMs;
                    int span=(int)Math.Round(dwell/refreshMs);
                    int delta=(i+1<frames.Count)?((frames[i+1].Serial-frames[i].Serial)&0x0FFF):0;
                    csv.WriteLine(string.Format(CultureInfo.InvariantCulture,"{0},{1:0.000},{2:0.000},{3:X4},{4},{5},{6},{7}",
                        i,frames[i].StartMs,dwell,frames[i].Sig,frames[i].Serial,frames[i].Generated?"G":"R",span,delta));
                }
            }

            using(StreamWriter sw=new StreamWriter(outPath,false))
            {
                sw.WriteLine("P1FG7N-VBLANK3 SINGLE-ENGINE VISIBLE FRAME + FLUIDITY VERIFIER");
                sw.WriteLine("Windows 8.1 x64 / one GDI marker capture loop / Gray12 + G/R + parity + guards + QPC pacing / no injection");
                sw.WriteLine("SCREEN WIDTH {0}",screenWidth);
                sw.WriteLine("SCREEN HEIGHT {0}",screenHeight);
                sw.WriteLine("PROCESS DPI AWARE {0}",gProcessDpiAware ? "YES" : "NO");
                sw.WriteLine("MARKER X {0}",MARKER_X); sw.WriteLine("MARKER Y {0}",MARKER_Y); sw.WriteLine("MARKER W {0}",MARKER_W); sw.WriteLine("MARKER H {0}",MARKER_H);
                sw.WriteLine("DISPLAY REFRESH HZ {0}",refreshHz);
                sw.WriteLine("DURATION US {0}",(long)(durationMs*1000.0));
                sw.WriteLine("CAPTURE ATTEMPTS {0}",attempts);
                sw.WriteLine("CAPTURE FAILURES {0}",captureFailures);
                sw.WriteLine("LOW CONTRAST SAMPLES {0}",lowContrast);
                sw.WriteLine("CAPTURE RATE MILLI-FPS {0}",(long)(captureHz*1000.0));
                sw.WriteLine("CAPTURE INTERVAL MEAN MS {0}",F(captureMean));
                sw.WriteLine("CAPTURE INTERVAL P95 MS {0}",F(captureP95));
                sw.WriteLine("CAPTURE INTERVAL MAX MS {0}",F(captureMax));
                sw.WriteLine("SAMPLING / REFRESH RATIO {0}x",F(samplingRatio));
                sw.WriteLine("MARKER VALID SAMPLES {0}",valid);
                sw.WriteLine("MARKER SYNC FAILURES {0}",syncFailures);
                sw.WriteLine("MARKER PARITY FAILURES {0}",parityFailures);
                sw.WriteLine("MARKER UNIQUE CONTENTS {0}",unique);
                sw.WriteLine("VISIBLE UNIQUE MILLI-FPS {0}",(long)(visibleFps*1000.0));
                sw.WriteLine("VISIBLE GENERATED CONTENTS {0}",gCount);
                sw.WriteLine("VISIBLE GENERATED MILLI-FPS {0}",(long)(gFps*1000.0));
                sw.WriteLine("VISIBLE REAL CONTENTS {0}",rCount);
                sw.WriteLine("VISIBLE REAL MILLI-FPS {0}",(long)(rFps*1000.0));
                sw.WriteLine("VISIBLE DUPLICATE SAMPLES {0}",duplicates);
                sw.WriteLine("VISIBLE MARKER GAPS {0}",gaps);
                sw.WriteLine("VISIBLE BACKWARD/AMBIGUOUS STEPS {0}",badSteps);
                sw.WriteLine("VISIBLE SAME-TYPE TRANSITIONS {0}",sameType);
                sw.WriteLine("MAX SYNC CONTRAST {0}",maxContrast);
                sw.WriteLine();
                sw.WriteLine("========== VISIBLE FLUIDITY / FRAME PACING ==========");
                sw.WriteLine("PACING INTERVALS {0}",intervals.Count);
                sw.WriteLine("FRAME INTERVAL MEAN MS {0}",F(mean));
                sw.WriteLine("FRAME INTERVAL MEDIAN MS {0}",F(median));
                sw.WriteLine("FRAME INTERVAL P05 MS {0}",F(p05));
                sw.WriteLine("FRAME INTERVAL P95 MS {0}",F(p95));
                sw.WriteLine("FRAME INTERVAL P99 MS {0}",F(p99));
                sw.WriteLine("FRAME INTERVAL MIN MS {0}",F(min));
                sw.WriteLine("FRAME INTERVAL MAX MS {0}",F(max));
                sw.WriteLine("FRAME INTERVAL STDDEV MS {0}",F(std));
                sw.WriteLine("FRAME INTERVAL CV PCT {0}",F(cv));
                sw.WriteLine("GENERATED DWELL MEDIAN MS {0}",F(gMed));
                sw.WriteLine("GENERATED DWELL P95 MS {0}",F(gP95));
                sw.WriteLine("REAL DWELL MEDIAN MS {0}",F(rMed));
                sw.WriteLine("REAL DWELL P95 MS {0}",F(rP95));
                sw.WriteLine("G_TO_R MEDIAN MS {0}",F(gtrMed));
                sw.WriteLine("R_TO_G MEDIAN MS {0}",F(rtgMed));
                sw.WriteLine("PHASE MEDIAN DELTA MS {0}",F(phaseDelta));
                sw.WriteLine("MIDPOINT BALANCE PCT {0}",F(midpointBalance));
                sw.WriteLine("PAIR PERIOD MEDIAN MS {0}",F(pairMed));
                sw.WriteLine("PAIR PERIOD P95 MS {0}",F(pairP95));
                sw.WriteLine("PAIR IMBALANCE MEDIAN PCT {0}",F(pairImbMed));
                sw.WriteLine("PAIR IMBALANCE P95 PCT {0}",F(pairImbP95));
                sw.WriteLine("MODAL DWELL VBLANKS {0}",modalSpan);
                sw.WriteLine("CADENCE MISMATCH EVENTS {0}",cadenceMismatch);
                sw.WriteLine("CADENCE MISMATCH RATIO PCT {0}",F(mismatchRatio));
                sw.WriteLine("LONG HOLDS GT 1.5X MEDIAN {0}",long15);
                sw.WriteLine("LONG HOLD RATIO PCT {0}",F(longRatio));
                sw.WriteLine("LONG HOLDS GT 2.0X MEDIAN {0}",long20);
                sw.WriteLine("MICRO BURSTS LT 0.5X MEDIAN {0}",micro05);
                sw.WriteLine("DWELL APPROX 1 VBLANK {0}",span1);
                sw.WriteLine("DWELL APPROX 2 VBLANK {0}",span2);
                sw.WriteLine("DWELL APPROX 3 VBLANK {0}",span3);
                sw.WriteLine("DWELL APPROX 4 VBLANK {0}",span4);
                sw.WriteLine("DWELL APPROX 5PLUS VBLANK {0}",span5plus);
                sw.WriteLine("CLEAN ALTERNATING TRANSITIONS {0}",cleanAlternatingTransitions);
                sw.WriteLine("PACING VERDICT {0}",pacingVerdict);
                sw.WriteLine();
                sw.WriteLine("NOTE: counts and pacing come from the SAME capture stream; no second screen-capture process is running.");
                sw.WriteLine("NOTE: pacing verdict is diagnostic, not an industry standard. Sampling ratio <0.90 makes timing conclusions unreliable.");
                sw.WriteLine("RAW PACING CSV {0}",csvPath);
            }

            using(StreamWriter st=new StreamWriter(statusPath,false))
            {
                st.WriteLine("PTAR GW15 VBLANK3 SINGLE ENGINE / PACINGVERIFIER2");
                st.WriteLine("RESULT=SINGLE_ENGINE_MEASUREMENT_COMPLETED");
                st.WriteLine("CAPTURE_RATE_HZ="+F(captureHz));
                st.WriteLine("DISPLAY_REFRESH_HZ="+refreshHz.ToString(CultureInfo.InvariantCulture));
                st.WriteLine("SAMPLING_RATIO="+F(samplingRatio));
                st.WriteLine("VISIBLE_FPS="+F(visibleFps));
                st.WriteLine("GENERATED_FPS="+F(gFps));
                st.WriteLine("REAL_FPS="+F(rFps));
                st.WriteLine("PACING_VERDICT="+pacingVerdict);
            }

            Console.WriteLine("TIMER RESOLUTION 1MS REQUEST RELEASED");
            Console.WriteLine("========== VBLANK3 SINGLE-ENGINE SUMMARY ==========");
            Console.WriteLine("CAPTURE RATE {0} Hz / REFRESH {1} Hz / RATIO {2}x",F(captureHz),refreshHz,F(samplingRatio));
            Console.WriteLine("VISIBLE UNIQUE {0} FPS | GENERATED {1} FPS | REAL {2} FPS",F(visibleFps),F(gFps),F(rFps));
            Console.WriteLine("GAPS {0} | SAME-TYPE {1} | BAD {2}",gaps,sameType,badSteps);
            Console.WriteLine("FRAME MEDIAN {0} ms | P95 {1} ms | P99 {2} ms | CV {3}%",F(median),F(p95),F(p99),F(cv));
            Console.WriteLine("G DWELL {0} ms | R DWELL {1} ms | MIDPOINT BALANCE {2}%",F(gMed),F(rMed),F(midpointBalance));
            Console.WriteLine("PAIR IMBALANCE P95 {0}% | CADENCE MISMATCH {1}%",F(pairImbP95),F(mismatchRatio));
            Console.WriteLine("PACING VERDICT: {0}",pacingVerdict);
            Console.WriteLine("=====================================================");
            return 0;
        }
        catch(Exception ex)
        {
            try { File.WriteAllText(errPath,ex.ToString()+"\r\n"); } catch { }
            return 30;
        }
        finally
        {
            if(timerRc==0) timeEndPeriod(1);
            if(timer!=IntPtr.Zero) CloseHandle(timer);
            if(memDC!=IntPtr.Zero && oldObj!=IntPtr.Zero) SelectObject(memDC,oldObj);
            if(dib!=IntPtr.Zero) DeleteObject(dib);
            if(memDC!=IntPtr.Zero) DeleteDC(memDC);
            if(screenDC!=IntPtr.Zero) ReleaseDC(IntPtr.Zero,screenDC);
        }
    }
}
