using System;
using System.Diagnostics;
using System.Drawing;
using System.IO;
using System.Runtime.InteropServices;
using System.Windows.Forms;

internal sealed class VisualFeedbackProbe : Form
{
    const int WS_EX_TOOLWINDOW=0x00000080;
    const int WS_EX_TRANSPARENT=0x00000020;
    const int WS_EX_NOACTIVATE=0x08000000;
    [DllImport("user32.dll")] static extern bool IsWindowVisible(IntPtr hWnd);
    [DllImport("user32.dll")] static extern bool GetWindowRect(IntPtr hWnd,out RECT r);
    [DllImport("kernel32.dll")] static extern bool Beep(uint f,uint d);
    struct RECT { public int Left,Top,Right,Bottom; }
    string line1="",line2=""; DateTime until=DateTime.MinValue; readonly Timer t=new Timer(); readonly string state;
    protected override CreateParams CreateParams { get { var cp=base.CreateParams; cp.ExStyle|=WS_EX_TOOLWINDOW|WS_EX_TRANSPARENT|WS_EX_NOACTIVATE; return cp; } }
    protected override bool ShowWithoutActivation { get { return true; } }
    public VisualFeedbackProbe()
    {
        state=Environment.GetEnvironmentVariable("PTAR_LAB_STATE")??Path.Combine(Path.GetTempPath(),"ptar_visual_probe.txt");
        FormBorderStyle=FormBorderStyle.None; ShowInTaskbar=false; TopMost=true; StartPosition=FormStartPosition.Manual;
        Width=790; Height=112; BackColor=Color.Black; Opacity=0.88; Font=new Font("Segoe UI",13.0f,FontStyle.Bold,GraphicsUnit.Point); DoubleBuffered=true;
        Rectangle b=Screen.PrimaryScreen.Bounds; Left=Math.Max(0,(b.Width-Width)/2); Top=28;
        t.Interval=15; t.Tick+=(s,e)=>{ if(Visible&&DateTime.UtcNow>=until) Hide(); }; t.Start();
        Shown+=(s,e)=>{ ShowNotice("PTAR RAWCOMPARE3 HOTFIX1 PRET","CTRL+F5 court = afficher/changer le mode | long (~0,7 s) = mesurer 20 s",3400,820); Dump("SHOWN"); };
    }
    protected override void OnPaint(PaintEventArgs e)
    {
        base.OnPaint(e); using(var w=new SolidBrush(Color.White)) using(var s=new SolidBrush(Color.Gainsboro)) { e.Graphics.DrawString(line1,Font,w,new RectangleF(20,15,Width-40,34)); using(var f2=new Font("Segoe UI",10.5f,FontStyle.Regular,GraphicsUnit.Point)) e.Graphics.DrawString(line2,f2,s,new RectangleF(20,58,Width-40,38)); }
    }
    void ShowNotice(string a,string b,int ms,uint tone){ line1=a; line2=b; until=DateTime.UtcNow.AddMilliseconds(ms); Invalidate(); if(!Visible) Show(); else { TopMost=false;TopMost=true; } try{Beep(tone,80);}catch{} Dump("NOTICE"); }
    void Dump(string phase){ try{RECT r;GetWindowRect(Handle,out r); File.AppendAllText(state,phase+" PID="+Process.GetCurrentProcess().Id+" HANDLE="+Handle.ToInt64()+" FORM_VISIBLE="+Visible+" NATIVE_VISIBLE="+IsWindowVisible(Handle)+" BOUNDS="+r.Left+","+r.Top+","+(r.Right-r.Left)+","+(r.Bottom-r.Top)+Environment.NewLine);}catch(Exception ex){File.AppendAllText(state,"ERR "+ex+Environment.NewLine);} }
    [STAThread] static int Main(){ Application.EnableVisualStyles(); Application.SetCompatibleTextRenderingDefault(false); Application.Run(new VisualFeedbackProbe()); return 0; }
}
