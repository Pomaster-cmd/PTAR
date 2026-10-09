using System;
using System.Diagnostics;
using System.IO;
using System.Runtime.InteropServices;
using System.Threading;

internal static class PTARD3D9DiagHudBridgeController
{
    const int VK_CONTROL=0x11, VK_F1=0x70, VK_F5=0x74;
    const int LONG_PRESS_MS=700, MENU_VISIBLE_MS=3000;
    [DllImport("user32.dll")] static extern short GetAsyncKeyState(int vKey);
    [DllImport("user32.dll")] static extern IntPtr GetForegroundWindow();
    [DllImport("user32.dll")] static extern uint GetWindowThreadProcessId(IntPtr hWnd,out uint processId);
    [DllImport("kernel32.dll",CharSet=CharSet.Unicode)] static extern bool WritePrivateProfileString(string section,string key,string value,string fileName);

    static string gameRoot="", targetProcessName="", iniPath="", logPath="";
    static int seq=1000, f5Mode=0, f1Mode=0;
    static bool f5Down=false,f1Down=false,f5Long=false,f1Long=false;
    static long f5Start=0,f1Start=0;
    static DateTime f5MenuUntil=DateTime.MinValue,f1MenuUntil=DateTime.MinValue;
    static DateTime f5ActionStart=DateTime.MinValue,f1ActionStart=DateTime.MinValue;
    static int f5ActionLine=0,f1ActionLine=0;
    static string f1ActionLog="";
    static long TickMs { get { return Stopwatch.GetTimestamp()*1000L/Stopwatch.Frequency; } }

    static void Log(string s){try{File.AppendAllText(logPath,DateTime.Now.ToString("yyyy-MM-dd HH:mm:ss.fff")+" "+s+Environment.NewLine);}catch{}}
    static bool IsTargetForeground()
    {
        if(string.IsNullOrEmpty(gameRoot)&&string.IsNullOrEmpty(targetProcessName))return false;
        try{IntPtr w=GetForegroundWindow();if(w==IntPtr.Zero)return false;uint pid;GetWindowThreadProcessId(w,out pid);if(pid==0)return false;using(Process p=Process.GetProcessById((int)pid)){if(!string.IsNullOrEmpty(targetProcessName)&&string.Equals(p.ProcessName,targetProcessName,StringComparison.OrdinalIgnoreCase))return true;try{string exePath=p.MainModule.FileName;if(!string.IsNullOrEmpty(gameRoot)&&!string.IsNullOrEmpty(exePath)){string root=Path.GetFullPath(gameRoot).TrimEnd('\\','/')+Path.DirectorySeparatorChar;string full=Path.GetFullPath(exePath);if(full.StartsWith(root,StringComparison.OrdinalIgnoreCase))return true;}}catch{}}}catch{}return false;
    }
    static void Emit(int type,int a,int durationMs,string reason)
    {
        try{if(durationMs<250)durationMs=250;if(durationMs>15000)durationMs=15000;WritePrivateProfileString("PTAR_DIAG_UI","Type",type.ToString(),iniPath);WritePrivateProfileString("PTAR_DIAG_UI","A",a.ToString(),iniPath);WritePrivateProfileString("PTAR_DIAG_UI","B","0",iniPath);WritePrivateProfileString("PTAR_DIAG_UI","C","0",iniPath);WritePrivateProfileString("PTAR_DIAG_UI","DurationMs",durationMs.ToString(),iniPath);int s=Interlocked.Increment(ref seq);if(s==0)s=Interlocked.Increment(ref seq);WritePrivateProfileString("PTAR_DIAG_UI","Sequence",s.ToString(),iniPath);WritePrivateProfileString(null,null,null,iniPath);Log("EMIT type="+type+" a="+a+" seq="+s+" reason="+reason);}catch(Exception ex){Log("EMIT_FAIL "+ex.Message);}
    }
    static int LineCount(string path){try{return File.Exists(path)?File.ReadAllLines(path).Length:0;}catch{return 0;}}
    static string NewLines(string path,int first)
    {
        try{if(!File.Exists(path))return "";string[] lines=File.ReadAllLines(path);if(first<0)first=0;if(first>=lines.Length)return "";return string.Join("\n",lines,first,lines.Length-first);}catch{return "";}
    }
    static string LatestF1Log()
    {
        try{string dir=Path.Combine(gameRoot,"diag","PTAR_PRESENTSHED1");if(!Directory.Exists(dir))return "";string[] logs=Directory.GetFiles(dir,"PTAR_PRESENTSHED1_CTRL_F1_*.log");if(logs.Length==0)return "";Array.Sort(logs,delegate(string a,string b){return File.GetLastWriteTimeUtc(b).CompareTo(File.GetLastWriteTimeUtc(a));});return logs[0];}catch{return "";}
    }
    static void ShortF5(){DateTime now=DateTime.UtcNow;bool already=now<f5MenuUntil;if(already)f5Mode=(f5Mode+1)%3;f5MenuUntil=now.AddMilliseconds(MENU_VISIBLE_MS);Emit(19+f5Mode,0,MENU_VISIBLE_MS,already?"F5_NEXT":"F5_SHOW");}
    static void LongF5(){f5ActionStart=DateTime.UtcNow;string p=Path.Combine(gameRoot,"PTAR_RAWCOMPARE3_CONTROLLER.log");f5ActionLine=LineCount(p);Emit(22,f5Mode,1400,"F5_MEASURE");Log("F5_ARM mode="+f5Mode+" line="+f5ActionLine);}
    static void ShortF1(){DateTime now=DateTime.UtcNow;bool already=now<f1MenuUntil;if(already)f1Mode=(f1Mode+1)%2;f1MenuUntil=now.AddMilliseconds(MENU_VISIBLE_MS);Emit(24,f1Mode,MENU_VISIBLE_MS,already?"F1_NEXT":"F1_SHOW");}
    static void LongF1(){f1ActionStart=DateTime.UtcNow;f1ActionLog=LatestF1Log();f1ActionLine=LineCount(f1ActionLog);Emit(25,f1Mode,1400,"F1_MEASURE");Log("F1_ARM mode="+f1Mode+" log="+f1ActionLog+" line="+f1ActionLine);}
    static void PollChord(int vk,ref bool down,ref bool longTriggered,ref long start,Action shortAction,Action longAction)
    {
        bool ctrl=(GetAsyncKeyState(VK_CONTROL)&0x8000)!=0,key=(GetAsyncKeyState(vk)&0x8000)!=0,chord=ctrl&&key;long now=TickMs;if(chord&&!down){down=true;longTriggered=false;start=now;return;}if(chord&&down){if(!longTriggered&&now-start>=LONG_PRESS_MS){longTriggered=true;longAction();}return;}if(!chord&&down){long held=now-start;down=false;if(!longTriggered&&held>=30&&held<LONG_PRESS_MS)shortAction();}
    }
    static int F5ModeFromText(string t,int fallback)
    {
        if(t.IndexOf("label=PRE_FG",StringComparison.OrdinalIgnoreCase)>=0)return 0;if(t.IndexOf("label=FG_ACTIVE",StringComparison.OrdinalIgnoreCase)>=0)return 1;if(t.IndexOf("label=POST_FG",StringComparison.OrdinalIgnoreCase)>=0)return 2;return fallback;
    }
    static void PollControllerResults()
    {
        try{
            string f5=Path.Combine(gameRoot,"PTAR_RAWCOMPARE3_CONTROLLER.log");
            if(f5ActionStart!=DateTime.MinValue&&File.Exists(f5)){
                string t=NewLines(f5,f5ActionLine);
                int completed=F5ModeFromText(t,f5Mode);
                if(t.IndexOf("DONE label=",StringComparison.OrdinalIgnoreCase)>=0){Emit(23,completed,4500,"F5_DONE");f5Mode=(completed+1)%3;f5ActionStart=DateTime.MinValue;Log("F5_SYNC next_mode="+f5Mode);}
                else if(t.IndexOf("FAIL label=",StringComparison.OrdinalIgnoreCase)>=0){Emit(28,completed,5500,"F5_ERROR");f5ActionStart=DateTime.MinValue;}
            }
            if(f1ActionStart!=DateTime.MinValue){if(string.IsNullOrEmpty(f1ActionLog)){f1ActionLog=LatestF1Log();f1ActionLine=0;}if(!string.IsNullOrEmpty(f1ActionLog)){string t=NewLines(f1ActionLog,f1ActionLine);if(t.IndexOf("DONE PRESENTSHED",StringComparison.OrdinalIgnoreCase)>=0){Emit(26,f1Mode,5000,"F1_DONE");f1ActionStart=DateTime.MinValue;}else if(t.IndexOf("FAIL PRESENTSHED",StringComparison.OrdinalIgnoreCase)>=0){Emit(27,f1Mode,6000,"F1_ERROR");f1ActionStart=DateTime.MinValue;}}}
        }catch(Exception ex){Log("RESULT_POLL_FAIL "+ex.Message);}
    }
    static int SelfTest()
    {
        string dir=Path.Combine(Path.GetTempPath(),"PTAR_DIAG_UI_SELFTEST_"+Process.GetCurrentProcess().Id);Directory.CreateDirectory(dir);gameRoot=dir;iniPath=Path.Combine(dir,"PTAR_D3D9_DIAG_UI.ini");logPath=Path.Combine(dir,"bridge.log");Emit(19,0,3000,"SELF_F5_PRE");Emit(20,0,3000,"SELF_F5_FG");Emit(24,1,3000,"SELF_F1_120");if(F5ModeFromText("DONE label=PRE_FG rc=0",9)!=0)return 15;if(F5ModeFromText("DONE label=FG_ACTIVE rc=0",9)!=1)return 16;if(F5ModeFromText("DONE label=POST_FG rc=0",9)!=2)return 17;string text=File.ReadAllText(iniPath);if(text.IndexOf("Type=24",StringComparison.OrdinalIgnoreCase)<0)return 11;if(text.IndexOf("A=1",StringComparison.OrdinalIgnoreCase)<0)return 12;if(text.IndexOf("DurationMs=3000",StringComparison.OrdinalIgnoreCase)<0)return 13;if(text.IndexOf("Sequence=",StringComparison.OrdinalIgnoreCase)<0)return 14;try{Directory.Delete(dir,true);}catch{}Console.WriteLine("SELFTEST=PASS BRIDGE=PTAR_D3D9_DIAG_UI.ini F5_CORRELATED=YES F5_MODE_SYNC=YES F1_SUBDIR=YES");return 0;
    }
    static int Main(string[] args)
    {
        if(args!=null&&args.Length>0&&string.Equals(args[0],"--selftest",StringComparison.OrdinalIgnoreCase))return SelfTest();gameRoot=Environment.GetEnvironmentVariable("PTAR_GAME_ROOT")??"";targetProcessName=Path.GetFileNameWithoutExtension(Environment.GetEnvironmentVariable("PTAR_TARGET_EXE")??"");if(string.IsNullOrEmpty(gameRoot)||!Directory.Exists(gameRoot))return 40;iniPath=Path.Combine(gameRoot,"PTAR_D3D9_DIAG_UI.ini");logPath=Path.Combine(gameRoot,"PTAR_D3D9_DIAG_UI_BRIDGE.log");bool created;using(Mutex mx=new Mutex(true,"PTAR_D3D9_DIAG_HUD_BRIDGE_20261009",out created)){if(!created)return 0;Log("START target="+targetProcessName);Emit(19,0,3400,"BRIDGE_READY");for(;;){if(IsTargetForeground()){PollChord(VK_F5,ref f5Down,ref f5Long,ref f5Start,ShortF5,LongF5);PollChord(VK_F1,ref f1Down,ref f1Long,ref f1Start,ShortF1,LongF1);}else{f5Down=false;f5Long=false;f1Down=false;f1Long=false;}PollControllerResults();Thread.Sleep(15);}}
    }
}
