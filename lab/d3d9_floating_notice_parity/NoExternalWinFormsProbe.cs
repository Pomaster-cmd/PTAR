using System;
using System.Runtime.InteropServices;
using System.Windows.Forms;

internal sealed class HiddenControllerForm : Form
{
    [DllImport("user32.dll")]
    static extern bool IsWindowVisible(IntPtr hWnd);

    readonly Timer timer = new Timer();
    public int TickCount { get; private set; }
    public bool WasEverManagedVisible { get; private set; }
    public bool WasEverNativeVisible { get; private set; }

    protected override void SetVisibleCore(bool value)
    {
        // Same mechanism used by the packaged RAWCOMPARE3/PRESENTSHED1
        // controllers: keep Form/Timer/BeginInvoke infrastructure alive while
        // refusing every request to surface a desktop HWND.
        base.SetVisibleCore(false);
    }

    public HiddenControllerForm()
    {
        ShowInTaskbar = false;
        IntPtr ensureHandle = Handle;
        timer.Interval = 40;
        timer.Tick += delegate
        {
            ++TickCount;
            WasEverManagedVisible |= Visible;
            WasEverNativeVisible |= IsWindowVisible(Handle);
            if (TickCount >= 8)
                Close();
        };
        timer.Start();
    }
}

internal static class NoExternalWinFormsProbe
{
    [STAThread]
    static int Main()
    {
        Application.EnableVisualStyles();
        Application.SetCompatibleTextRenderingDefault(false);
        using (HiddenControllerForm f = new HiddenControllerForm())
        {
            // Application.Run(Form) is the exact lifecycle used by the package.
            // PASS therefore requires the hidden Form to keep pumping Timer
            // messages instead of exiting just because it is never visible.
            Application.Run(f);
            if (f.TickCount < 8) return 10;
            if (f.WasEverManagedVisible) return 11;
            if (f.WasEverNativeVisible) return 12;
            Console.WriteLine("NO_EXTERNAL_WINFORMS_UI=PASS");
            Console.WriteLine("HIDDEN_MESSAGE_PUMP_TICKS=" + f.TickCount.ToString());
            Console.WriteLine("APPLICATION_RUN_HIDDEN_FORM=PASS");
            Console.WriteLine("UI_OWNER=D3D9_IN_GAME_FEEDBACK_BOX");
        }
        return 0;
    }
}
