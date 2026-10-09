using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Drawing;
using System.IO;
using System.Runtime.InteropServices;
using System.Threading;
using System.Windows.Forms;

internal sealed class PTARRawCompare3Controller : Form
{
    const int VK_F5 = 0x74;
    const int VK_CONTROL = 0x11;
    const int LONG_PRESS_MS = 700;
    const int MENU_VISIBLE_MS = 3000;
    const int WS_EX_TOOLWINDOW = 0x00000080;
    const int WS_EX_TRANSPARENT = 0x00000020;
    const int WS_EX_NOACTIVATE = 0x08000000;

    [DllImport("user32.dll")]
    static extern short GetAsyncKeyState(int vKey);
    [DllImport("user32.dll")]
    static extern IntPtr GetForegroundWindow();
    [DllImport("user32.dll")]
    static extern uint GetWindowThreadProcessId(IntPtr hWnd, out uint processId);
    [DllImport("kernel32.dll")]
    static extern bool Beep(uint dwFreq, uint dwDuration);

    readonly System.Windows.Forms.Timer poll = new System.Windows.Forms.Timer();
    readonly object logLock = new object();
    readonly string gameRoot;
    readonly string packageRoot;
    readonly string psExe;
    readonly string controllerLog;
    readonly string targetProcessName;
    readonly string sessionId;
    int mode = 0;
    bool chordDown = false;
    bool longTriggered = false;
    long pressStartTick = 0;
    DateTime menuUntilUtc = DateTime.MinValue;
    DateTime overlayUntilUtc = DateTime.MinValue;
    bool busy = false;
    string line1 = "";
    string line2 = "";

    static long TickMs { get { return Stopwatch.GetTimestamp() * 1000L / Stopwatch.Frequency; } }
    protected override CreateParams CreateParams { get { CreateParams cp=base.CreateParams; cp.ExStyle|=WS_EX_TOOLWINDOW|WS_EX_TRANSPARENT|WS_EX_NOACTIVATE; return cp; } }
    protected override bool ShowWithoutActivation { get { return true; } }

    PTARRawCompare3Controller()
    {
        gameRoot=Environment.GetEnvironmentVariable("PTAR_GAME_ROOT")??"";
        packageRoot=Environment.GetEnvironmentVariable("PTAR_PACKAGE_ROOT")??"";
        psExe=Environment.GetEnvironmentVariable("PTAR_PS_EXE")??Path.Combine((Environment.GetEnvironmentVariable("WINDIR")??"C:\\Windows"),"System32\\WindowsPowerShell\\v1.0\\powershell.exe");
        controllerLog=string.IsNullOrEmpty(gameRoot)?Path.Combine(Path.GetTempPath(),"PTAR_RAWCOMPARE3_CONTROLLER.log"):Path.Combine(gameRoot,"PTAR_RAWCOMPARE3_CONTROLLER.log");
        string targetExe=Environment.GetEnvironmentVariable("PTAR_TARGET_EXE")??"";
        targetProcessName=Path.GetFileNameWithoutExtension(targetExe);
        sessionId=Environment.GetEnvironmentVariable("PTAR_RAWCOMPARE3_SESSION_ID")??"";
        FormBorderStyle=FormBorderStyle.None; ShowInTaskbar=false; TopMost=true; StartPosition=FormStartPosition.Manual; Width=790; Height=112; BackColor=Color.Black; Opacity=0.88; Font=new Font("Segoe UI",13.0f,FontStyle.Bold,GraphicsUnit.Point); DoubleBuffered=true;
        Rectangle b=Screen.PrimaryScreen.Bounds; Left=Math.Max(0,(b.Width-Width)/2); Top=28;
        poll.Interval=15; poll.Tick+=PollTick; poll.Start();
        Shown+=delegate(object sender,EventArgs e){ShowNotice("PTAR RAWCOMPARE3 HOTFIX1 PRET","CTRL+F5 court = afficher/changer le mode | long (~0,7 s) = mesurer 20 s",3400,820);};
        Log("START RAWCOMPARE3 HOTFIX1 controller; mode=PRE_FG session="+sessionId);
    }

    protected override void OnPaint(PaintEventArgs e)
    {
        base.OnPaint(e); e.Graphics.TextRenderingHint=System.Drawing.Text.TextRenderingHint.ClearTypeGridFit;
        using(Brush white=new SolidBrush(Color.White)) using(Brush sub=new SolidBrush(Color.Gainsboro))
        { e.Graphics.DrawString(line1,Font,white,new RectangleF(20,15,Width-40,34)); using(Font f2=new Font("Segoe UI",10.5f,FontStyle.Regular,GraphicsUnit.Point)) e.Graphics.DrawString(line2,f2,sub,new RectangleF(20,58,Width-40,38)); }
    }

    void PollTick(object sender,EventArgs e)
    {
        if(Visible&&DateTime.UtcNow>=overlayUntilUtc)Hide();
        if(!IsTargetForeground()){chordDown=false;longTriggered=false;return;}
        bool ctrl=(GetAsyncKeyState(VK_CONTROL)&0x8000)!=0, f5=(GetAsyncKeyState(VK_F5)&0x8000)!=0, chord=ctrl&&f5; long now=TickMs;
        if(chord&&!chordDown){chordDown=true;longTriggered=false;pressStartTick=now;return;}
        if(chord&&chordDown){if(!longTriggered&&now-pressStartTick>=LONG_PRESS_MS){longTriggered=true;StartSelectedMeasurement();}return;}
        if(!chord&&chordDown){long held=now-pressStartTick;chordDown=false;if(!longTriggered&&held>=30&&held<LONG_PRESS_MS)HandleShortPress();}
    }

    bool IsTargetForeground()
    {
        if(string.IsNullOrEmpty(targetProcessName)&&string.IsNullOrEmpty(gameRoot))return false;
        try{IntPtr w=GetForegroundWindow();if(w==IntPtr.Zero)return false;uint pid;GetWindowThreadProcessId(w,out pid);if(pid==0)return false;using(Process p=Process.GetProcessById((int)pid)){if(!string.IsNullOrEmpty(targetProcessName)&&string.Equals(p.ProcessName,targetProcessName,StringComparison.OrdinalIgnoreCase))return true;try{string exePath=p.MainModule.FileName;if(!string.IsNullOrEmpty(gameRoot)&&!string.IsNullOrEmpty(exePath)){string root=Path.GetFullPath(gameRoot).TrimEnd('\\','/')+Path.DirectorySeparatorChar;string full=Path.GetFullPath(exePath);if(full.StartsWith(root,StringComparison.OrdinalIgnoreCase))return true;}}catch{}return false;}}catch{return false;}
    }

    string ObservedFgState()
    {
        try{string p=Path.Combine(gameRoot,"PTAR_X86_D3D9.log");if(!File.Exists(p))return "ETAT NON LOGGE";string last=null;foreach(string l in File.ReadAllLines(p))if(l.IndexOf("HOTKEY CTRL+F6 FrameGeneration=",StringComparison.OrdinalIgnoreCase)>=0)last=l;if(last==null)return "ETAT NON LOGGE";if(last.IndexOf("=OFF",StringComparison.OrdinalIgnoreCase)>=0)return "OFF";if(last.IndexOf("=ON",StringComparison.OrdinalIgnoreCase)>=0)return "ON";return "INCONNU";}catch{return "INCONNU";}
    }

    void HandleShortPress()
    {
        if(busy){ShowNotice("ANALYSE EN COURS",ModeFriendly(mode)+" | attendre la fin des 20 s",1800,520);return;}
        DateTime now=DateTime.UtcNow;bool menuAlreadyDisplayed=Visible&&now<menuUntilUtc;if(menuAlreadyDisplayed)mode=(mode+1)%3;menuUntilUtc=now.AddMilliseconds(MENU_VISIBLE_MS);string suffix=menuAlreadyDisplayed?"Mode suivant selectionne":"2e appui court pendant cet affichage = mode suivant";string state=ObservedFgState();ShowNotice("MODE : "+ModeFriendly(mode),suffix+" | "+ModeRequirement(mode)+" | FG observe: "+state,MENU_VISIBLE_MS,ModeTone(mode));Log("SHORT mode="+ModeName(mode)+" cycled="+(menuAlreadyDisplayed?"YES":"NO")+" fg_observed="+state);
    }

    void StartSelectedMeasurement()
    {
        if(busy){ShowNotice("ANALYSE DEJA EN COURS",ModeFriendly(mode),1800,360);return;}
        busy=true;menuUntilUtc=DateTime.MinValue;int selected=mode;string label=ModeName(selected);ShowNotice("ANALYSE "+ModeFriendly(selected),"Demarrage | "+ModeRequirement(selected)+" | ne touche a rien pendant 20 s",700,1180);Log("LONG START label="+label+" fg_observed="+ObservedFgState());
        ThreadPool.QueueUserWorkItem(delegate(object ignored){Thread.Sleep(700);BeginInvoke((MethodInvoker)delegate{Hide();});int rc=RunMeasurement(label);string metric=ReadLastMetric(label);int compareRc=-1;if(selected==2&&rc==0)compareRc=RunPowerShell(Path.Combine(packageRoot,"diag\\compare_raw_pre_fg_post.ps1"),"");BeginInvoke((MethodInvoker)delegate{busy=false;if(rc==0){string second=metric.Length>0?metric:"Mesure sauvegardee";if(selected==0){mode=1;second+=" | suivant: active FG (CTRL+F6), puis appui long";}else if(selected==1){mode=2;second+=" | suivant: coupe FG, attends ~3 s, puis appui long";}else{mode=0;second+=compareRc==0?" | comparaison PRE/FG/POST generee":" | comparaison incomplete";}ShowNotice("TERMINE : "+ModeFriendly(selected),second,4700,1480);Log("DONE label="+label+" rc=0 compare_rc="+compareRc.ToString()+" "+metric+" next_mode="+ModeName(mode));}else{string msg=ExplainError(rc);ShowNotice("ECHEC : "+ModeFriendly(selected),msg+" (code "+rc.ToString()+")",5600,300);Log("FAIL label="+label+" rc="+rc.ToString()+" "+msg);}});});
    }

    int RunMeasurement(string label){string script=Path.Combine(packageRoot,"diag\\raw_compare3_run.ps1");if(!File.Exists(script))return 91;return RunPowerShell(script,"-Label "+label);}
    int RunPowerShell(string script,string extraArgs)
    {
        try{ProcessStartInfo psi=new ProcessStartInfo();psi.FileName=psExe;psi.Arguments="-NoLogo -NoProfile -ExecutionPolicy Bypass -File \""+script+"\" "+extraArgs;psi.UseShellExecute=false;psi.CreateNoWindow=true;psi.WindowStyle=ProcessWindowStyle.Hidden;psi.RedirectStandardOutput=true;psi.RedirectStandardError=true;Process p=Process.Start(psi);string o=p.StandardOutput.ReadToEnd(),er=p.StandardError.ReadToEnd();p.WaitForExit();if(!string.IsNullOrEmpty(o))Log("PS OUT "+OneLine(o));if(!string.IsNullOrEmpty(er))Log("PS ERR "+OneLine(er));return p.ExitCode;}catch(Exception ex){Log("PS EXCEPTION "+ex.ToString());return 92;}
    }
    string ReadLastMetric(string label)
    {
        try{string pattern=string.IsNullOrEmpty(sessionId)?"PTAR_RAWCOMPARE_*_"+label+"_*_METRICS.txt":"PTAR_RAWCOMPARE_"+sessionId+"_"+label+"_*_METRICS.txt";string[] files=Directory.GetFiles(gameRoot,pattern);if(files.Length==0)return "";Array.Sort(files,delegate(string a,string b){return File.GetLastWriteTimeUtc(b).CompareTo(File.GetLastWriteTimeUtc(a));});Dictionary<string,string> kv=new Dictionary<string,string>(StringComparer.OrdinalIgnoreCase);foreach(string l in File.ReadAllLines(files[0])){int k=l.IndexOf('=');if(k>0)kv[l.Substring(0,k)]=l.Substring(k+1);}string fps=kv.ContainsKey("VISIBLE_FPS")?kv["VISIBLE_FPS"]:"?",med=kv.ContainsKey("FRAME_INTERVAL_MEDIAN_MS")?kv["FRAME_INTERVAL_MEDIAN_MS"]:"?";if(string.Equals(label,"FG_ACTIVE",StringComparison.OrdinalIgnoreCase)){string r=kv.ContainsKey("REAL_CONTENTS")?kv["REAL_CONTENTS"]:"?",g=kv.ContainsKey("GENERATED_CONTENTS")?kv["GENERATED_CONTENTS"]:"?";return fps+" FPS | R="+r+" G="+g+" | mediane "+med+" ms";}return fps+" FPS | mediane "+med+" ms";}catch{return "";}
    }
    static string ExplainError(int rc){if(rc==20||rc==21||rc==22||rc==23)return "Installation/runtime D3D9 invalide";if(rc==24)return "FG ACTIF refuse : active d'abord le FG avec CTRL+F6";if(rc==25)return "POST-FG refuse : aucun cycle FG detecte";if(rc==26)return "POST-FG refuse : FG est encore ON";if(rc==28)return "PRE-FG refuse : FG est actuellement ON";if(rc==43)return "Telemetrie Present D3D9 insuffisante ou sequence R/G incomplete";if(rc==44)return "Moteur de cadence brute PRE/POST absent";if(rc==45)return "Sortie cadence brute PRE/POST incomplete";if(rc==91)return "Script RAWCOMPARE3 absent";if(rc==92)return "Impossible de lancer PowerShell";return "Mesure interrompue ou incomplete";}
    void ShowNotice(string a,string b,int ms,uint tone){line1=a;line2=b;overlayUntilUtc=DateTime.UtcNow.AddMilliseconds(ms);Invalidate();if(!Visible)Show();else{TopMost=false;TopMost=true;}try{Beep(tone,80);}catch{}}
    void Log(string s){try{lock(logLock)File.AppendAllText(controllerLog,DateTime.Now.ToString("yyyy-MM-dd HH:mm:ss.fff")+" "+s+Environment.NewLine);}catch{}}
    static string OneLine(string s){s=s.Replace('\r',' ').Replace('\n',' ').Trim();return s.Length>900?s.Substring(0,900):s;}
    static string ModeName(int m){if(m==0)return "PRE_FG";if(m==1)return "FG_ACTIVE";return "POST_FG";}
    static string ModeFriendly(int m){if(m==0)return "PRE-FG BRUT";if(m==1)return "FG ACTIF";return "POST-FG BRUT";}
    static string ModeRequirement(int m){if(m==0)return "FG OFF, idealement jamais active";if(m==1)return "FG ON requis";return "FG OFF apres activation";}
    static uint ModeTone(int m){if(m==0)return 760U;if(m==1)return 1080U;return 940U;}
    static int SelfTest(){int m=0;bool displayed=false;if(displayed)m=(m+1)%3;if(m!=0)return 11;displayed=true;if(displayed)m=(m+1)%3;if(m!=1)return 12;if(displayed)m=(m+1)%3;if(m!=2)return 13;if(displayed)m=(m+1)%3;if(m!=0)return 14;if(LONG_PRESS_MS<500||LONG_PRESS_MS>1200)return 15;if(MENU_VISIBLE_MS<1500)return 16;if(ModeName(0)!="PRE_FG"||ModeName(1)!="FG_ACTIVE"||ModeName(2)!="POST_FG")return 17;Console.WriteLine("SELFTEST=PASS MODES=PRE_FG,FG_ACTIVE,POST_FG FIRST_SHORT_SHOW_ONLY SECOND_SHORT_NEXT LONG_START_MS="+LONG_PRESS_MS.ToString());return 0;}
    [STAThread] static int Main(string[] args){if(args!=null&&args.Length>0&&string.Equals(args[0],"--selftest",StringComparison.OrdinalIgnoreCase))return SelfTest();string root=Environment.GetEnvironmentVariable("PTAR_GAME_ROOT")??"",pack=Environment.GetEnvironmentVariable("PTAR_PACKAGE_ROOT")??"";if(root.Length==0||pack.Length==0||!Directory.Exists(root)||!Directory.Exists(pack))return 40;bool created;using(Mutex mx=new Mutex(true,"PTAR_RAWCOMPARE3_INGAME_CONTROLLER_20260925",out created)){if(!created)return 0;Application.EnableVisualStyles();Application.SetCompatibleTextRenderingDefault(false);Application.Run(new PTARRawCompare3Controller());}return 0;}
}
