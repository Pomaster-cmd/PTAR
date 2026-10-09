using System;
using System.Globalization;
using System.IO;

public static class PTARVisiblePacingVerifierPresentShed1
{
    static readonly CultureInfo Inv=CultureInfo.InvariantCulture;
    public static int Main(string[] args)
    {
        if(args!=null&&args.Length>0&&string.Equals(args[0],"--selftest",StringComparison.OrdinalIgnoreCase))return PTARD3D9PresentTelemetryReader.SelfTest();
        int sec=60;if(args!=null)foreach(string a in args){int n;if(int.TryParse(a,out n)&&n>=5&&n<=120)sec=n;}
        string root=Environment.GetEnvironmentVariable("PTAR_GAME_ROOT")??"";if(string.IsNullOrWhiteSpace(root)||!Directory.Exists(root))return 40;
        string prefix=Environment.GetEnvironmentVariable("PTAR_FG_PRESENTSHED1_OUTPUT_PREFIX")??"";if(string.IsNullOrWhiteSpace(prefix))return 42;
        string outPath=prefix+"_OUTPUT.txt",csvPath=prefix+"_SAMPLES.csv",statusPath=prefix+"_STATUS.txt",rrPath=prefix+"_RR_FOCUS.txt",lhPath=prefix+"_LONG_HOLD_FOCUS.txt",gpPath=prefix+"_GEN_PRESSURE_FOCUS.txt",errPath=prefix+"_ERROR.txt";
        try
        {
            Directory.CreateDirectory(Path.GetDirectoryName(prefix));
            PTARD3D9PresentCapture c=PTARD3D9PresentTelemetryReader.Collect(root,sec);int real,gen;PTARD3D9PresentReport.Counts(c,out real,out gen);PTARD3D9PresentReport.WriteVisibleHeader(outPath,c,"PRESENTSHED1");PTARD3D9PresentReport.WriteVisibleCsv(csvPath,c);
            int rrEpisodes=0,rrTransitions=0,maxR=0,runR=0;int clean50=0,clean66=0,clean100=0,clean150=0,clean200=0,real50=0,gen50=0;double maxHold=0;
            for(int i=0;i<c.Events.Count;i++)
            {
                double dw=PTARD3D9PresentReport.DwellMs(c,i);if(dw>maxHold)maxHold=dw;if(dw>=50){clean50++;if(c.Events[i].Generated)gen50++;else real50++;}if(dw>=66)clean66++;if(dw>=100)clean100++;if(dw>=150)clean150++;if(dw>=200)clean200++;
                if(!c.Events[i].Generated){runR++;if(runR>maxR)maxR=runR;if(i>0&&!c.Events[i-1].Generated){rrTransitions++;if(i==1||c.Events[i-2].Generated)rrEpisodes++;}}else runR=0;
            }
            File.WriteAllLines(rrPath,new string[]{"PTAR_PRESENTSHED1_RR_FOCUS=1","PRESENTSHED1_RUNTIME_ONLY=YES","RR_EPISODES="+rrEpisodes.ToString(Inv),"R_TO_R="+rrTransitions.ToString(Inv),"MAX_R_STREAK="+maxR.ToString(Inv),"RR_WITH_G_READY_AFTER=0","RR_WITHOUT_G_READY_AFTER=0","RR_SNAPSHOT_UNAVAILABLE="+rrEpisodes.ToString(Inv),"SOURCE=D3D9_RUNTIME_PRESENT_RING"});
            File.WriteAllLines(lhPath,new string[]{"PTAR_PRESENTSHED1_LONG_HOLD=1","CLEAN_HOLDS_GE_50MS="+clean50.ToString(Inv),"CLEAN_HOLDS_GE_66MS="+clean66.ToString(Inv),"CLEAN_HOLDS_GE_100MS="+clean100.ToString(Inv),"CLEAN_HOLDS_GE_150MS="+clean150.ToString(Inv),"CLEAN_HOLDS_GE_200MS="+clean200.ToString(Inv),"REAL_HOLDS_GE_50MS="+real50.ToString(Inv),"GENERATED_HOLDS_GE_50MS="+gen50.ToString(Inv),"MAX_CLEAN_HOLD_MS="+maxHold.ToString("0.000",Inv),"SOURCE=D3D9_RUNTIME_PRESENT_RING"});
            File.WriteAllLines(gpPath,new string[]{"PTAR_PRESENTSHED1_GEN_PRESSURE=1","GENERATED_EVENTS="+gen.ToString(Inv),"REAL_EVENTS="+real.ToString(Inv),"RING_OVERFLOWS="+c.RingOverflows.ToString(Inv),"SOURCE=D3D9_RUNTIME_PRESENT_RING"});
            bool valid=c.Events.Count>20&&real>0&&gen>0&&c.RingOverflows==0;File.WriteAllText(statusPath,"COMPLETED="+(valid?"YES":"NO")+"\r\nRESULT="+(valid?"PASS":"FAIL")+"\r\nMODE=D3D9_RUNTIME_PRESENT_RING\r\n");if(!valid)File.WriteAllText(errPath,"PRESENTSHED1 runtime-present telemetry incomplete: events="+c.Events.Count+" real="+real+" generated="+gen+" overflow="+c.RingOverflows+"\r\n");return valid?0:43;
        }
        catch(Exception ex){try{File.WriteAllText(errPath,ex.ToString()+"\r\n");}catch{}return 50;}
    }
}
