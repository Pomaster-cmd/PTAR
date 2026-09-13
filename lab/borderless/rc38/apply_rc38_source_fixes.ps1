$ErrorActionPreference='Stop'
$p='lab/borderless/rc38/ptar_borderless_rc38.cpp'
$s=[IO.File]::ReadAllText($p)
$s=$s.Replace('SetWindowPos(g_presenter,HWND_TOP,g_monitor.left,g_monitor.top,(int)g_outputW,(int)g_outputH,SWP_NOACTIVATE|SWP_FRAMECHANGED|SWP_SHOWWINDOW);','SetWindowPos(g_presenter,HWND_NOTOPMOST,g_monitor.left,g_monitor.top,(int)g_outputW,(int)g_outputH,SWP_NOACTIVATE|SWP_FRAMECHANGED|SWP_SHOWWINDOW);')
$s=$s.Replace('SetWindowPos(g_presenter,HWND_TOP,g_monitor.left,g_monitor.top,(int)g_outputW,(int)g_outputH,SWP_NOACTIVATE|SWP_SHOWWINDOW);','SetWindowPos(g_presenter,HWND_NOTOPMOST,g_monitor.left,g_monitor.top,(int)g_outputW,(int)g_outputH,SWP_NOACTIVATE|SWP_SHOWWINDOW);')
$s=$s.Replace('wsprintfA(x,"%s %lld %lld %lld %lld",tag,(long long)a,(long long)b,(long long)c,(long long)d);','wsprintfA(x,"%s %I64d %I64d %I64d %I64d",tag,(long long)a,(long long)b,(long long)c,(long long)d);')
[IO.File]::WriteAllText($p,$s,(New-Object Text.UTF8Encoding($false)))
if($s -match 'SetWindowPos\(g_presenter,HWND_TOP,'){throw 'presenter TOPMOST-band SetWindowPos remains after RC38 fix'}
if($s -notmatch 'HWND_NOTOPMOST'){throw 'RC38 NOTOPMOST fix was not applied'}
