using System;
using System.Runtime.InteropServices;
using System.Windows.Forms;

internal sealed class HiddenControllerForm : Form
{
    [DllImport("user32.dll")]
    static extern bool IsWindowVisible(IntPtr hWnd);

    protected override void SetVisibleCore(bool value)
    {
        // D3D9 diagnostic controllers keep a WinForms message pump only for
        // Timer/BeginInvoke compatibility. All user-facing notices belong to
        // the in-game D3D9 feedback box, so the HWND must never be surfaced.
        base.SetVisibleCore(false);
    }

    public bool NativeVisible()
    {
        IntPtr h = Handle;
        return IsWindowVisible(h);
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
            f.Show();
            Application.DoEvents();
            if (f.Visible) return 11;
            if (f.NativeVisible()) return 12;
            Console.WriteLine("NO_EXTERNAL_WINFORMS_UI=PASS");
            Console.WriteLine("UI_OWNER=D3D9_IN_GAME_FEEDBACK_BOX");
        }
        return 0;
    }
}
