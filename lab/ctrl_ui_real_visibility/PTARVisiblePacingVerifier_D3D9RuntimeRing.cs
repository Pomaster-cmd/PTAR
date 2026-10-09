using System;
using System.IO;

public static class PTARVisiblePacingVerifier
{
    public static int Main(string[] args)
    {
        if(args!=null && args.Length>0 && string.Equals(args[0],"--selftest",StringComparison.OrdinalIgnoreCase))
            return PTARD3D9PresentTelemetryReader.SelfTest();
        int sec=20;
        if(args!=null) foreach(string a in args){int n;if(int.TryParse(a,out n)&&n>=3&&n<=120)sec=n;}
        string root=Environment.GetEnvironmentVariable("PTAR_GAME_ROOT")??"";
        if(string.IsNullOrWhiteSpace(root)||!Directory.Exists(root)) return 40;
        string outPath=Path.Combine(root,"PTAR_VISIBLE_VERIFIER_LAST_OUTPUT.txt");
        string csvPath=Path.Combine(root,"PTAR_VISIBLE_VERIFIER_LAST_SAMPLES.csv");
        string statusPath=Path.Combine(root,"PTAR_VISIBLE_VERIFIER_LAST_STATUS.txt");
        string errPath=Path.Combine(root,"PTAR_VISIBLE_VERIFIER_LAST_ERROR.txt");
        try{if(File.Exists(outPath))File.Delete(outPath);}catch{} try{if(File.Exists(csvPath))File.Delete(csvPath);}catch{} try{if(File.Exists(statusPath))File.Delete(statusPath);}catch{} try{if(File.Exists(errPath))File.Delete(errPath);}catch{}
        try
        {
            PTARD3D9PresentCapture c=PTARD3D9PresentTelemetryReader.Collect(root,sec);
            int real,gen;PTARD3D9PresentReport.Counts(c,out real,out gen);
            PTARD3D9PresentReport.WriteVisibleHeader(outPath,c,"FG_ACTIVE");
            PTARD3D9PresentReport.WriteVisibleCsv(csvPath,c);
            bool valid=c.Events.Count>10 && real>0 && gen>0 && c.RingOverflows==0;
            File.WriteAllText(statusPath,"COMPLETED="+(valid?"YES":"NO")+"\r\nRESULT="+(valid?"PASS":"FAIL")+"\r\nMODE=D3D9_RUNTIME_PRESENT_RING\r\n");
            if(!valid)File.WriteAllText(errPath,"D3D9 runtime-present telemetry incomplete: events="+c.Events.Count+" real="+real+" generated="+gen+" overflow="+c.RingOverflows+"\r\n");
            return valid?0:43;
        }
        catch(Exception ex){try{File.WriteAllText(errPath,ex.ToString()+"\r\n");}catch{}return 50;}
    }
}
