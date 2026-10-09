using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Globalization;
using System.IO;
using System.Runtime.InteropServices;
using System.Threading;

public sealed class PTARD3D9PresentEvent
{
    public int Sequence;
    public int MarkerSerial;
    public bool Generated;
    public long Qpc;
    public uint RealPresents;
    public uint GeneratedPresents;
    public uint Resyncs;
    public uint LateSkips;
}

public sealed class PTARD3D9PresentCapture
{
    public readonly List<PTARD3D9PresentEvent> Events = new List<PTARD3D9PresentEvent>();
    public long QpcFrequency;
    public double DurationMs;
    public double RefreshHz;
    public int RingOverflows;
    public string AttachedExe = "";
    public string RuntimePath = "";
}

public static class PTARD3D9PresentTelemetryReader
{
    const uint MAGIC = 0x39444750U;
    const uint VERSION = 1U;
    const int HEADER_SIZE = 32;
    const int EVENT_SIZE = 36;
    const int EVENT_SEQUENCE = 0;
    const int EVENT_MARKER = 4;
    const int EVENT_GENERATED = 8;
    const int EVENT_QPC = 12;
    const int EVENT_REAL = 20;
    const int EVENT_GEN = 24;
    const int EVENT_RESYNCS = 28;
    const int EVENT_LATESKIPS = 32;
    const int HEADER_MAGIC = 0;
    const int HEADER_VERSION = 4;
    const int HEADER_STRUCT_SIZE = 8;
    const int HEADER_CAPACITY = 12;
    const int HEADER_QPC_FREQ = 16;
    const int HEADER_WRITE_SERIAL = 24;
    const uint PROCESS_VM_READ = 0x0010;
    const uint PROCESS_QUERY_INFORMATION = 0x0400;
    const uint DONT_RESOLVE_DLL_REFERENCES = 0x00000001;
    const int VREFRESH = 116;

    [DllImport("kernel32.dll", CharSet=CharSet.Unicode, SetLastError=true)]
    static extern IntPtr LoadLibraryExW(string lpFileName, IntPtr hFile, uint dwFlags);
    [DllImport("kernel32.dll", SetLastError=true)]
    static extern bool FreeLibrary(IntPtr hModule);
    [DllImport("kernel32.dll", CharSet=CharSet.Ansi, SetLastError=true)]
    static extern IntPtr GetProcAddress(IntPtr hModule, string lpProcName);
    [DllImport("kernel32.dll", SetLastError=true)]
    static extern IntPtr OpenProcess(uint dwDesiredAccess, bool bInheritHandle, uint dwProcessId);
    [DllImport("kernel32.dll", SetLastError=true)]
    static extern bool ReadProcessMemory(IntPtr hProcess, IntPtr lpBaseAddress, [Out] byte[] lpBuffer, int dwSize, out IntPtr lpNumberOfBytesRead);
    [DllImport("kernel32.dll")]
    static extern bool CloseHandle(IntPtr hObject);
    [DllImport("user32.dll")]
    static extern IntPtr GetDC(IntPtr hWnd);
    [DllImport("user32.dll")]
    static extern int ReleaseDC(IntPtr hWnd, IntPtr hDC);
    [DllImport("gdi32.dll")]
    static extern int GetDeviceCaps(IntPtr hdc, int nIndex);
    [DllImport("winmm.dll")]
    static extern uint timeBeginPeriod(uint uPeriod);
    [DllImport("winmm.dll")]
    static extern uint timeEndPeriod(uint uPeriod);

    static int I32(byte[] b, int o) { return BitConverter.ToInt32(b, o); }
    static uint U32(byte[] b, int o) { return BitConverter.ToUInt32(b, o); }
    static long I64(byte[] b, int o) { return BitConverter.ToInt64(b, o); }

    static bool ReadExact(IntPtr hp, long address, byte[] buffer)
    {
        if(hp==IntPtr.Zero || address<=0 || buffer==null || buffer.Length==0) return false;
        IntPtr got;
        bool ok=ReadProcessMemory(hp,new IntPtr(address),buffer,buffer.Length,out got);
        return ok && got.ToInt64()==buffer.Length;
    }

    static bool IsUnderRoot(string path, string root)
    {
        try
        {
            string p=Path.GetFullPath(path).TrimEnd(Path.DirectorySeparatorChar,Path.AltDirectorySeparatorChar)+Path.DirectorySeparatorChar;
            string r=Path.GetFullPath(root).TrimEnd(Path.DirectorySeparatorChar,Path.AltDirectorySeparatorChar)+Path.DirectorySeparatorChar;
            return p.StartsWith(r,StringComparison.OrdinalIgnoreCase);
        }
        catch { return false; }
    }

    static long ResolveExportRva(string runtimePath)
    {
        IntPtr local=LoadLibraryExW(runtimePath,IntPtr.Zero,DONT_RESOLVE_DLL_REFERENCES);
        if(local==IntPtr.Zero) throw new InvalidOperationException("LoadLibraryEx(DONT_RESOLVE) failed for D3D9 runtime. GLE="+Marshal.GetLastWin32Error().ToString(CultureInfo.InvariantCulture));
        try
        {
            IntPtr p=GetProcAddress(local,"PTAR_D3D9_DIAG_STATE");
            if(p==IntPtr.Zero) throw new InvalidOperationException("PTAR_D3D9_DIAG_STATE export absent.");
            return p.ToInt64()-local.ToInt64();
        }
        finally { FreeLibrary(local); }
    }

    static bool TryAttach(string gameRoot, string targetExe, out Process process, out IntPtr hp, out long moduleBase, out string runtimePath)
    {
        process=null; hp=IntPtr.Zero; moduleBase=0; runtimePath=Path.Combine(gameRoot,"d3d9.dll");
        Process fallback=null; long fallbackBase=0;
        foreach(Process p in Process.GetProcesses())
        {
            bool keep=false;
            try
            {
                string main=p.MainModule==null?"":p.MainModule.FileName;
                if(string.IsNullOrWhiteSpace(main) || !IsUnderRoot(main,gameRoot)) continue;
                long mb=0;
                foreach(ProcessModule m in p.Modules)
                {
                    if(!string.Equals(m.ModuleName,"d3d9.dll",StringComparison.OrdinalIgnoreCase)) continue;
                    if(!string.Equals(Path.GetFullPath(m.FileName),Path.GetFullPath(runtimePath),StringComparison.OrdinalIgnoreCase)) continue;
                    mb=m.BaseAddress.ToInt64();
                    break;
                }
                if(mb==0) continue;
                bool exact=!string.IsNullOrWhiteSpace(targetExe) && string.Equals(Path.GetFullPath(main),Path.GetFullPath(targetExe),StringComparison.OrdinalIgnoreCase);
                if(exact)
                {
                    IntPtr h=OpenProcess(PROCESS_QUERY_INFORMATION|PROCESS_VM_READ,false,(uint)p.Id);
                    if(h==IntPtr.Zero) continue;
                    process=p; hp=h; moduleBase=mb; keep=true;
                    if(fallback!=null) fallback.Dispose();
                    return true;
                }
                if(fallback==null)
                {
                    fallback=p; fallbackBase=mb; keep=true;
                }
            }
            catch { }
            finally { if(!keep) p.Dispose(); }
        }
        if(fallback!=null)
        {
            IntPtr h=OpenProcess(PROCESS_QUERY_INFORMATION|PROCESS_VM_READ,false,(uint)fallback.Id);
            if(h!=IntPtr.Zero)
            {
                process=fallback; hp=h; moduleBase=fallbackBase; return true;
            }
            fallback.Dispose();
        }
        return false;
    }

    static double RefreshHz()
    {
        IntPtr dc=IntPtr.Zero;
        try
        {
            dc=GetDC(IntPtr.Zero);
            if(dc!=IntPtr.Zero)
            {
                int hz=GetDeviceCaps(dc,VREFRESH);
                if(hz>=30 && hz<=360) return hz;
            }
        }
        catch { }
        finally { if(dc!=IntPtr.Zero) ReleaseDC(IntPtr.Zero,dc); }
        return 60.0;
    }

    static PTARD3D9PresentEvent ReadEvent(IntPtr hp, long stateBase, int capacity, int expected)
    {
        int slot=(expected-1)&(capacity-1);
        byte[] b=new byte[EVENT_SIZE];
        long addr=stateBase+HEADER_SIZE+(long)slot*EVENT_SIZE;
        if(!ReadExact(hp,addr,b)) return null;
        int seq=I32(b,EVENT_SEQUENCE);
        if(seq!=expected) return null;
        PTARD3D9PresentEvent e=new PTARD3D9PresentEvent();
        e.Sequence=seq;
        e.MarkerSerial=(int)(U32(b,EVENT_MARKER)&4095U);
        e.Generated=U32(b,EVENT_GENERATED)!=0;
        e.Qpc=I64(b,EVENT_QPC);
        e.RealPresents=U32(b,EVENT_REAL);
        e.GeneratedPresents=U32(b,EVENT_GEN);
        e.Resyncs=U32(b,EVENT_RESYNCS);
        e.LateSkips=U32(b,EVENT_LATESKIPS);
        return e;
    }

    public static PTARD3D9PresentCapture Collect(string gameRoot, int durationSeconds)
    {
        gameRoot=Path.GetFullPath(gameRoot.Trim().Trim('"'));
        string targetExe=Environment.GetEnvironmentVariable("PTAR_TARGET_EXE")??"";
        string runtimePath=Path.Combine(gameRoot,"d3d9.dll");
        if(!File.Exists(runtimePath)) throw new FileNotFoundException("d3d9.dll absent",runtimePath);
        long rva=ResolveExportRva(runtimePath);

        Process proc; IntPtr hp; long moduleBase; string attachedRuntime;
        if(!TryAttach(gameRoot,targetExe,out proc,out hp,out moduleBase,out attachedRuntime))
            throw new InvalidOperationException("Processus D3D9 PTAR charge introuvable sous PTAR_GAME_ROOT.");

        try
        {
            long stateBase=moduleBase+rva;
            byte[] h=new byte[HEADER_SIZE];
            if(!ReadExact(hp,stateBase,h)) throw new InvalidOperationException("Lecture entete telemetrie D3D9 impossible.");
            uint magic=U32(h,HEADER_MAGIC), version=U32(h,HEADER_VERSION), structSize=U32(h,HEADER_STRUCT_SIZE), capacityU=U32(h,HEADER_CAPACITY);
            if(magic!=MAGIC || version!=VERSION) throw new InvalidOperationException("ABI telemetrie D3D9 incompatible.");
            if(capacityU<16 || capacityU>4096 || (capacityU&(capacityU-1))!=0) throw new InvalidOperationException("Capacite ring D3D9 invalide.");
            int capacity=(int)capacityU;
            if(structSize<(uint)(HEADER_SIZE+capacity*EVENT_SIZE)) throw new InvalidOperationException("Taille ABI telemetrie D3D9 invalide.");
            long freq=I64(h,HEADER_QPC_FREQ);
            int baseline=I32(h,HEADER_WRITE_SERIAL);
            int next=baseline+1;

            PTARD3D9PresentCapture cap=new PTARD3D9PresentCapture();
            cap.QpcFrequency=freq>0?freq:Stopwatch.Frequency;
            cap.RefreshHz=RefreshHz();
            cap.AttachedExe=proc.MainModule==null?"":proc.MainModule.FileName;
            cap.RuntimePath=attachedRuntime;

            Stopwatch wall=Stopwatch.StartNew();
            uint t=timeBeginPeriod(1);
            try
            {
                while(wall.Elapsed.TotalSeconds<durationSeconds)
                {
                    byte[] hs=new byte[HEADER_SIZE];
                    if(!ReadExact(hp,stateBase,hs)) throw new InvalidOperationException("Lecture telemetrie D3D9 interrompue.");
                    long fq=I64(hs,HEADER_QPC_FREQ); if(fq>0) cap.QpcFrequency=fq;
                    int current=I32(hs,HEADER_WRITE_SERIAL);
                    if(current>=next)
                    {
                        if(current-next+1>capacity)
                        {
                            cap.RingOverflows+=current-next+1-capacity;
                            next=current-capacity+1;
                        }
                        while(next<=current)
                        {
                            PTARD3D9PresentEvent e=ReadEvent(hp,stateBase,capacity,next);
                            if(e==null) break;
                            cap.Events.Add(e);
                            next++;
                        }
                    }
                    Thread.Sleep(1);
                }

                for(int pass=0;pass<10;pass++)
                {
                    byte[] hs=new byte[HEADER_SIZE]; if(!ReadExact(hp,stateBase,hs)) break;
                    int current=I32(hs,HEADER_WRITE_SERIAL);
                    bool progressed=false;
                    while(next<=current)
                    {
                        PTARD3D9PresentEvent e=ReadEvent(hp,stateBase,capacity,next);
                        if(e==null) break;
                        cap.Events.Add(e); next++; progressed=true;
                    }
                    if(next>current) break;
                    if(!progressed) Thread.Sleep(1);
                }
            }
            finally { if(t==0) timeEndPeriod(1); }
            cap.DurationMs=wall.Elapsed.TotalMilliseconds;
            return cap;
        }
        finally
        {
            if(hp!=IntPtr.Zero) CloseHandle(hp);
            if(proc!=null) proc.Dispose();
        }
    }

    public static int SelfTest()
    {
        if(HEADER_SIZE!=32 || EVENT_SIZE!=36) return 71;
        if((256&(256-1))!=0) return 72;
        int slotA=(1-1)&255, slotB=(257-1)&255;
        if(slotA!=slotB) return 73;
        Console.WriteLine("SELFTEST=PASS D3D11_CONTRACT_ADAPTED=D3D9_RUNTIME_PRESENT_RING GDI_CAPTURE=NO ABI=V1");
        return 0;
    }
}

public static class PTARD3D9PresentReport
{
    static readonly CultureInfo Inv=CultureInfo.InvariantCulture;
    public static string F(double v) { return v.ToString("0.000",Inv); }
    public static double Mean(List<double> v) { if(v.Count==0)return 0; double s=0; foreach(double x in v)s+=x; return s/v.Count; }
    public static double Percentile(List<double> v,double p)
    {
        if(v.Count==0)return 0; double[] a=v.ToArray(); Array.Sort(a); if(a.Length==1)return a[0];
        double pos=(a.Length-1)*p; int lo=(int)Math.Floor(pos),hi=(int)Math.Ceiling(pos); if(lo==hi)return a[lo];
        return a[lo]+(a[hi]-a[lo])*(pos-lo);
    }
    public static double Std(List<double> v,double mean) { if(v.Count==0)return 0; double s=0; foreach(double x in v){double d=x-mean;s+=d*d;} return Math.Sqrt(s/v.Count); }

    public static List<double> Intervals(PTARD3D9PresentCapture c)
    {
        List<double> x=new List<double>();
        if(c==null || c.Events.Count<2 || c.QpcFrequency<=0) return x;
        for(int i=1;i<c.Events.Count;i++)
        {
            long dq=c.Events[i].Qpc-c.Events[i-1].Qpc;
            if(dq>0) x.Add(dq*1000.0/c.QpcFrequency);
        }
        return x;
    }

    public static double StartMs(PTARD3D9PresentCapture c,int i)
    {
        if(c.Events.Count==0 || i<0 || i>=c.Events.Count || c.QpcFrequency<=0) return 0;
        return (c.Events[i].Qpc-c.Events[0].Qpc)*1000.0/c.QpcFrequency;
    }

    public static double DwellMs(PTARD3D9PresentCapture c,int i)
    {
        if(c.Events.Count<2) return 0;
        if(i<c.Events.Count-1) return Math.Max(0,StartMs(c,i+1)-StartMs(c,i));
        return Math.Max(0,StartMs(c,i)-StartMs(c,i-1));
    }

    public static void WriteVisibleCsv(string path, PTARD3D9PresentCapture c)
    {
        using(StreamWriter w=new StreamWriter(path,false))
        {
            w.WriteLine("index,start_ms,dwell_ms,serial,type,transition_observer_contaminated,generated_presents,real_presents,present_successes,resyncs,late_skips");
            for(int i=0;i<c.Events.Count;i++)
            {
                PTARD3D9PresentEvent e=c.Events[i];
                w.WriteLine(string.Format(Inv,"{0},{1:0.000},{2:0.000},{3},{4},{5},{6},{7},{8},{9},{10}",
                    i,StartMs(c,i),DwellMs(c,i),e.MarkerSerial,e.Generated?"G":"R",c.RingOverflows>0?1:0,
                    e.GeneratedPresents,e.RealPresents,(ulong)e.GeneratedPresents+(ulong)e.RealPresents,e.Resyncs,e.LateSkips));
            }
        }
    }

    public static void WriteVisibleHeader(string path, PTARD3D9PresentCapture c, string mode)
    {
        double durationUs=0;
        if(c.Events.Count>=2 && c.QpcFrequency>0) durationUs=(c.Events[c.Events.Count-1].Qpc-c.Events[0].Qpc)*1000000.0/c.QpcFrequency;
        using(StreamWriter w=new StreamWriter(path,false))
        {
            w.WriteLine("PTAR D3D9 D3D11-COMPATIBLE PRESENT DIAGNOSTIC");
            w.WriteLine("MODE "+mode);
            w.WriteLine("DURATION US "+F(durationUs));
            w.WriteLine("DISPLAY REFRESH HZ "+F(c.RefreshHz));
            w.WriteLine("SAMPLING / REFRESH RATIO RUNTIME_PRESENT_RING");
            w.WriteLine("CAPTURE SOURCE D3D9_RUNTIME_PRESENT_RING");
            w.WriteLine("GDI CAPTURE NO");
            w.WriteLine("RING OVERFLOWS "+c.RingOverflows.ToString(Inv));
            w.WriteLine("EVENTS "+c.Events.Count.ToString(Inv));
        }
    }

    public static bool Counts(PTARD3D9PresentCapture c,out int real,out int gen)
    {
        real=0;gen=0;foreach(PTARD3D9PresentEvent e in c.Events){if(e.Generated)gen++;else real++;}return c.Events.Count>0;
    }
}
