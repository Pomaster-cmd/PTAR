using System;
using System.Collections.Generic;
using System.Globalization;
using System.IO;

public static class PTARRawCadenceVerifier
{
    static readonly CultureInfo Inv=CultureInfo.InvariantCulture;
    static string F(double v){return v.ToString("0.000",Inv);}
    static int ModalSpan(List<double> v,double refreshMs){int[] bins=new int[13];foreach(double dt in v){int s=(int)Math.Round(dt/refreshMs);if(s<1)s=1;if(s>12)s=12;bins[s]++;}int best=1;for(int i=2;i<bins.Length;i++)if(bins[i]>bins[best])best=i;return best;}

    public static int Main(string[] args)
    {
        if(args!=null&&args.Length>0&&string.Equals(args[0],"--selftest",StringComparison.OrdinalIgnoreCase))return PTARD3D9PresentTelemetryReader.SelfTest();
        int sec=20;if(args!=null)foreach(string a in args){int n;if(int.TryParse(a,out n)&&n>=3&&n<=120)sec=n;}
        string root=Environment.GetEnvironmentVariable("PTAR_GAME_ROOT")??"";if(string.IsNullOrWhiteSpace(root)||!Directory.Exists(root))return 40;
        string outPath=Path.Combine(root,"PTAR_RAWCADENCE_LAST_OUTPUT.txt"),csvPath=Path.Combine(root,"PTAR_RAWCADENCE_LAST_SAMPLES.csv"),statusPath=Path.Combine(root,"PTAR_RAWCADENCE_LAST_STATUS.txt"),errPath=Path.Combine(root,"PTAR_RAWCADENCE_LAST_ERROR.txt");
        try{if(File.Exists(outPath))File.Delete(outPath);}catch{}try{if(File.Exists(csvPath))File.Delete(csvPath);}catch{}try{if(File.Exists(statusPath))File.Delete(statusPath);}catch{}try{if(File.Exists(errPath))File.Delete(errPath);}catch{}
        try
        {
            PTARD3D9PresentCapture c=PTARD3D9PresentTelemetryReader.Collect(root,sec);int real,gen;PTARD3D9PresentReport.Counts(c,out real,out gen);List<double> iv=PTARD3D9PresentReport.Intervals(c);
            double mean=PTARD3D9PresentReport.Mean(iv),med=PTARD3D9PresentReport.Percentile(iv,.5),p95=PTARD3D9PresentReport.Percentile(iv,.95),p99=PTARD3D9PresentReport.Percentile(iv,.99),mn=PTARD3D9PresentReport.Percentile(iv,0),mx=PTARD3D9PresentReport.Percentile(iv,1),std=PTARD3D9PresentReport.Std(iv,mean),cv=mean>0?100*std/mean:0,low=p99>0?1000/p99:0;
            double durationMs=c.Events.Count>=2?(c.Events[c.Events.Count-1].Qpc-c.Events[0].Qpc)*1000.0/c.QpcFrequency:0;double fps=durationMs>0?(c.Events.Count-1)*1000.0/durationMs:0;double refreshMs=1000.0/c.RefreshHz;int modal=ModalSpan(iv,refreshMs),mismatch=0,long15=0,long20=0,micro05=0;foreach(double dt in iv){int s=(int)Math.Round(dt/refreshMs);if(s<1)s=1;if(s>12)s=12;if(s!=modal)mismatch++;if(med>0&&dt>1.5*med)long15++;if(med>0&&dt>2*med)long20++;if(med>0&&dt<.5*med)micro05++;}
            double mismatchPct=iv.Count>0?100.0*mismatch/iv.Count:0;bool valid=c.Events.Count>10&&real>0&&gen==0&&c.RingOverflows==0&&iv.Count>5;
            List<string> o=new List<string>();o.Add("PTAR_RAWCOMPARE3_D3D11_PORT=1");o.Add("MEASURE_MODE=FG_OFF_RUNTIME_PRESENT");o.Add("RAW_MODE=RUNTIME_PRESENT");o.Add("RAW_RESULT="+(valid?"RAW_REAL_ONLY_VALID":"RAW_INVALID"));o.Add("VISIBLE_FPS="+F(fps));o.Add("ONE_PERCENT_LOW_EST_FPS="+F(low));o.Add("FRAME_INTERVAL_MEAN_MS="+F(mean));o.Add("FRAME_INTERVAL_MEDIAN_MS="+F(med));o.Add("FRAME_INTERVAL_P95_MS="+F(p95));o.Add("FRAME_INTERVAL_P99_MS="+F(p99));o.Add("FRAME_INTERVAL_MIN_MS="+F(mn));o.Add("FRAME_INTERVAL_MAX_MS="+F(mx));o.Add("FRAME_INTERVAL_STDDEV_MS="+F(std));o.Add("FRAME_INTERVAL_CV_PCT="+F(cv));o.Add("MODAL_DWELL_VBLANKS="+modal.ToString(Inv));o.Add("CADENCE_MISMATCH_EVENTS="+mismatch.ToString(Inv));o.Add("CADENCE_MISMATCH_RATIO_PCT="+F(mismatchPct));o.Add("LONG_HOLDS_GT_1_5X_MEDIAN="+long15.ToString(Inv));o.Add("LONG_HOLDS_GT_2X_MEDIAN="+long20.ToString(Inv));o.Add("MICRO_BURSTS_LT_0_5X_MEDIAN="+micro05.ToString(Inv));o.Add("UNIQUE_CONTENTS="+c.Events.Count.ToString(Inv));o.Add("REAL_CONTENTS="+real.ToString(Inv));o.Add("GENERATED_CONTENTS="+gen.ToString(Inv));o.Add("DISPLAY_REFRESH_HZ="+F(c.RefreshHz));o.Add("OBSERVER_STALLS=0");o.Add("OBSERVER_EXCLUDED_TRANSITIONS=0");o.Add("RING_OVERFLOWS="+c.RingOverflows.ToString(Inv));o.Add("CAPTURE_SOURCE=D3D9_RUNTIME_PRESENT_RING");o.Add("GDI_CAPTURE=NO");File.WriteAllLines(outPath,o.ToArray());
            using(StreamWriter w=new StreamWriter(csvPath,false)){w.WriteLine("index,start_ms,interval_ms,serial,type,generated_presents,real_presents");for(int i=0;i<c.Events.Count;i++){double st=PTARD3D9PresentReport.StartMs(c,i),dt=i==0?0:st-PTARD3D9PresentReport.StartMs(c,i-1);PTARD3D9PresentEvent e=c.Events[i];w.WriteLine(string.Format(Inv,"{0},{1:0.000},{2:0.000},{3},{4},{5},{6}",i,st,dt,e.MarkerSerial,e.Generated?"G":"R",e.GeneratedPresents,e.RealPresents));}}
            File.WriteAllText(statusPath,"COMPLETED="+(valid?"YES":"NO")+"\r\nRESULT="+(valid?"PASS":"FAIL")+"\r\nMODE=D3D9_RUNTIME_PRESENT_RING\r\n");if(!valid)File.WriteAllText(errPath,"Runtime-present cadence invalid: events="+c.Events.Count+" real="+real+" generated="+gen+" overflow="+c.RingOverflows+"\r\n");return valid?0:43;
        }
        catch(Exception ex){try{File.WriteAllText(errPath,ex.ToString()+"\r\n");}catch{}return 50;}
    }
}
