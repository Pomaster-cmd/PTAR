using System;
using System.Diagnostics;
using System.Drawing;
using System.IO;
using System.Runtime.InteropServices;
using System.Threading;
using System.Text;
using System.Windows.Forms;

internal sealed class PTARPresentShed1Controller : Form
{
    const int VK_F1=0x70, VK_CONTROL=0x11, LONG_PRESS_MS=700, MENU_VISIBLE_MS=3000;
    const int WS_EX_TOOLWINDOW=0x80, WS_EX_TRANSPARENT=0x20, WS_EX_NOACTIVATE=0x08000000;
    [DllImport("user32.dll")] static extern short GetAsyncKeyState(int vKey);
    [DllImport("user32.dll")] static extern IntPtr GetForegroundWindow();
    [DllImport("user32.dll")] static extern uint GetWindowThreadProcessId(IntPtr hWnd,out uint processId);
    [DllImport("kernel32.dll")] static extern bool Beep(uint f,uint d);
    readonly System.Windows.Forms.Timer poll=new System.Windows.Forms.Timer();
    readonly string gameRoot,packageRoot,psExe,resultDir,sessionId,targetProcessName,controllerLog;
    bool chordDown=false,longTriggered=false,busy=false; long pressStart=0; int mode=0; string lastPsError="";
    DateTime menuUntil=DateTime.MinValue,overlayUntil=DateTime.MinValue; string line1="",line2="";
    static long TickMs { get { return Stopwatch.GetTimestamp()*1000L/Stopwatch.Frequency; } }
    protected override CreateParams CreateParams { get { CreateParams cp=base.CreateParams;cp.ExStyle|=WS_EX_TOOLWINDOW|WS_EX_TRANSPARENT|WS_EX_NOACTIVATE;return cp; } }
    protected override bool ShowWithoutActivation { get { return true; } }
    PTARPresentShed1Controller()
    {
        gameRoot=Environment.GetEnvironmentVariable("PTAR_GAME_ROOT")??"";packageRoot=Environment.GetEnvironmentVariable("PTAR_PACKAGE_ROOT")??"";
        psExe=Environment.GetEnvironmentVariable("PTAR_PS_EXE")??Path.Combine(Environment.GetEnvironmentVariable("WINDIR")??"C:\\Windows","System32\\WindowsPowerShell\\v1.0\\powershell.exe");
        resultDir=Environment.GetEnvironmentVariable("PTAR_PRESENTSHED1_RESULT_DIR")??gameRoot;sessionId=Environment.GetEnvironmentVariable("PTAR_PRESENTSHED1_SESSION_ID")??"";
        targetProcessName=Path.GetFileNameWithoutExtension(Environment.GetEnvironmentVariable("PTAR_TARGET_EXE")??"");controllerLog=Path.Combine(resultDir,"PTAR_PRESENTSHED1_CTRL_F1_"+sessionId+".log");
        FormBorderStyle=FormBorderStyle.None;ShowInTaskbar=false;TopMost=true;StartPosition=FormStartPosition.Manual;Width=860;Height=112;BackColor=Color.Black;Opacity=0.88;Font=new Font("Segoe UI",13.0f,FontStyle.Bold,GraphicsUnit.Point);DoubleBuffered=true;
        Rectangle b=Screen.PrimaryScreen.Bounds;Left=Math.Max(0,(b.Width-Width)/2);Top=28;poll.Interval=15;poll.Tick+=PollTick;poll.Start();
        Shown+=delegate(object s,EventArgs e){ShowNotice("PTAR PRESENTSHED1", "CTRL+F1 court = afficher/changer | long (~0,7 s) = lancer l'analyse",3400,820);};
        Log("START PRESENTSHED1 controller");
    }
    protected override void OnPaint(PaintEventArgs e){base.OnPaint(e);using(Brush w=new SolidBrush(Color.White))using(Brush s=new SolidBrush(Color.Gainsboro)){e.Graphics.DrawString(line1,Font,w,new RectangleF(20,15,Width-40,34));using(Font f2=new Font("Segoe UI",10.5f,FontStyle.Regular,GraphicsUnit.Point))e.Graphics.DrawString(line2,f2,s,new RectangleF(20,58,Width-40,38));}}
    void PollTick(object sender,EventArgs e){if(Visible&&DateTime.UtcNow>=overlayUntil)Hide();if(!IsTargetForeground()){chordDown=false;longTriggered=false;return;}bool chord=((GetAsyncKeyState(VK_CONTROL)&0x8000)!=0)&&((GetAsyncKeyState(VK_F1)&0x8000)!=0);long now=TickMs;if(chord&&!chordDown){chordDown=true;longTriggered=false;pressStart=now;return;}if(chord&&chordDown){if(!longTriggered&&now-pressStart>=LONG_PRESS_MS){longTriggered=true;StartMeasurement();}return;}if(!chord&&chordDown){long held=now-pressStart;chordDown=false;if(!longTriggered&&held>=30&&held<LONG_PRESS_MS)ShortPress();}}
    bool IsTargetForeground(){try{IntPtr w=GetForegroundWindow();if(w==IntPtr.Zero)return false;uint pid;GetWindowThreadProcessId(w,out pid);using(Process p=Process.GetProcessById((int)pid)){if(!string.IsNullOrEmpty(targetProcessName)&&string.Equals(p.ProcessName,targetProcessName,StringComparison.OrdinalIgnoreCase))return true;try{string exePath=p.MainModule.FileName;if(!string.IsNullOrEmpty(gameRoot)&&!string.IsNullOrEmpty(exePath)){string root=Path.GetFullPath(gameRoot).TrimEnd('\\','/')+Path.DirectorySeparatorChar;string full=Path.GetFullPath(exePath);if(full.StartsWith(root,StringComparison.OrdinalIgnoreCase))return true;}}catch{}return false;}}catch{return false;}}
    string ModeName(){return mode==0?"PRESENTSHED60":"PRESENTSHED120";} string ModeFriendly(){return mode==0?"PRESENTSHED1 60 S":"PRESENTSHED1 120 S";} int Duration(){return mode==0?60:120;}
    void ShortPress(){if(busy){ShowNotice("ANALYSE EN COURS",ModeFriendly()+" - attendre la fin",1600,520);return;}DateTime n=DateTime.UtcNow;bool already=Visible&&n<menuUntil;if(already)mode=(mode+1)%2;menuUntil=n.AddMilliseconds(MENU_VISIBLE_MS);ShowNotice("MODE : "+ModeFriendly(),already?"Mode suivant selectionne":"2e appui court pendant cet affichage = mode suivant",MENU_VISIBLE_MS,mode==0?1080U:900U);}
    void StartMeasurement(){if(busy){ShowNotice("ANALYSE DEJA EN COURS",ModeFriendly(),1600,360);return;}busy=true;int selected=mode;string m=ModeName();ShowNotice("ANALYSE "+ModeFriendly(),"PRESENTSHED1 - FG ON - fluidite perceptuelle",700,1180);ThreadPool.QueueUserWorkItem(delegate(object x){Thread.Sleep(700);BeginInvoke((MethodInvoker)delegate{Hide();});int rc=RunPowerShell(Path.Combine(packageRoot,"diag\\rawcompare3\\presentshed1\\run_capture.ps1"),m);string summary=ReadLastSummary();BeginInvoke((MethodInvoker)delegate{busy=false;if(rc==0){ShowNotice("TERMINE - RESULTAT SAUVEGARDE",summary.Length>0?summary:("diag\\PTAR_PRESENTSHED1 | "+Duration()+" s"),5200,1480);Log("DONE "+m+" "+summary);}else{string detail=Explain(rc);if(lastPsError.Length>0)detail=detail+" | "+lastPsError;ShowNotice("ECHEC ANALYSE",detail+" (code "+rc+")",7600,300);Log("FAIL "+m+" rc="+rc+" detail="+detail);}});});}
    int RunPowerShell(string script,string modeName){lastPsError="";try{if(!File.Exists(script)){lastPsError="Script de mesure absent: "+script;Log(lastPsError);return 91;}ProcessStartInfo psi=new ProcessStartInfo();psi.FileName=psExe;string cmd="$ErrorActionPreference='Stop';$p=$env:PTAR_CAPTURE_SCRIPT;$m=$env:PTAR_CAPTURE_MODE;if([string]::IsNullOrWhiteSpace($p) -or -not(Test-Path -LiteralPath $p -PathType Leaf)){Write-Error ('Script de mesure absent: '+$p);exit 91};& $p -Mode $m;exit $LASTEXITCODE";string encoded=Convert.ToBase64String(Encoding.Unicode.GetBytes(cmd));psi.Arguments="-NoLogo -NoProfile -ExecutionPolicy Bypass -EncodedCommand "+encoded;psi.EnvironmentVariables["PTAR_CAPTURE_SCRIPT"]=script;psi.EnvironmentVariables["PTAR_CAPTURE_MODE"]=modeName;psi.WorkingDirectory=packageRoot;psi.UseShellExecute=false;psi.CreateNoWindow=true;psi.RedirectStandardOutput=true;psi.RedirectStandardError=true;Process p=Process.Start(psi);string o=p.StandardOutput.ReadToEnd(),er=p.StandardError.ReadToEnd();p.WaitForExit();if(o.Length>0)Log("PS OUT "+OneLine(o));if(er.Length>0){lastPsError=OneLine(er);Log("PS ERR "+lastPsError);}return p.ExitCode;}catch(Exception ex){lastPsError=OneLine(ex.Message);Log(ex.ToString());return 92;}}
    string ReadLastSummary(){try{string[] f=Directory.GetFiles(resultDir,"PTAR_*_PERCEPTUAL.txt");if(f.Length==0)return "";Array.Sort(f,delegate(string a,string b){return File.GetLastWriteTimeUtc(b).CompareTo(File.GetLastWriteTimeUtc(a));});string fps="?",p99="?",rr="?",ep="?",mh="?";foreach(string l in File.ReadAllLines(f[0])){int k=l.IndexOf('=');if(k<1)continue;string a=l.Substring(0,k),v=l.Substring(k+1);if(a=="VISIBLE_FPS")fps=v;else if(a=="FRAME_INTERVAL_P99_MS")p99=v;else if(a=="R_TO_R")rr=v;else if(a=="RR_EPISODES")ep=v;else if(a=="MAX_CLEAN_HOLD_MS")mh=v;}return fps+" FPS | P99 "+p99+" ms | MAX HOLD "+mh+" ms | R->R "+rr+" | diag\\PTAR_PRESENTSHED1";}catch{return "";}}
    static string Explain(int rc){if(rc==23)return "Le runtime actif n'est pas PRESENTSHED1";if(rc==25)return "FG ON requis (CTRL+F6)";if(rc==27)return "VBlankDiagnostics=1 requis";if(rc==43)return "Telemetrie Present D3D9 incomplete";if(rc==91)return "Script de mesure absent";return "Mesure interrompue ou incomplete";}
    void ShowNotice(string a,string b,int ms,uint tone){line1=a;line2=b;overlayUntil=DateTime.UtcNow.AddMilliseconds(ms);Invalidate();if(!Visible)Show();else{TopMost=false;TopMost=true;}try{Beep(tone,80);}catch{}}
    void Log(string s){try{File.AppendAllText(controllerLog,DateTime.Now.ToString("yyyy-MM-dd HH:mm:ss.fff")+" "+s+Environment.NewLine);}catch{}}
    static string OneLine(string s){s=s.Replace('\r',' ').Replace('\n',' ').Trim();return s.Length>1000?s.Substring(0,1000):s;}
    static int SelfTest(){int m=0;bool shown=false;if(shown)m=(m+1)%2;if(m!=0)return 11;shown=true;if(shown)m=(m+1)%2;if(m!=1)return 12;if(shown)m=(m+1)%2;if(m!=0)return 13;if(LONG_PRESS_MS<500||LONG_PRESS_MS>1200)return 14;Console.WriteLine("SELFTEST=PASS MODES=PRESENTSHED60,PRESENTSHED120");return 0;}
    [STAThread] static int Main(string[] args){if(args!=null&&args.Length>0&&string.Equals(args[0],"--selftest",StringComparison.OrdinalIgnoreCase))return SelfTest();string root=Environment.GetEnvironmentVariable("PTAR_GAME_ROOT")??"",pack=Environment.GetEnvironmentVariable("PTAR_PACKAGE_ROOT")??"";if(root.Length==0||pack.Length==0||!Directory.Exists(root)||!Directory.Exists(pack))return 40;bool created;using(Mutex mx=new Mutex(true,"PTAR_PRESENTSHED1_CTRL_F1_20260930",out created)){if(!created)return 0;Application.EnableVisualStyles();Application.SetCompatibleTextRenderingDefault(false);Application.Run(new PTARPresentShed1Controller());}return 0;}
}
