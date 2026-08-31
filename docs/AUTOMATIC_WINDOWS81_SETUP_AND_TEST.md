# PTAR Automatic Windows 8.1 Setup + Hardware Validation

## User entry point

Double-click:

`RUN_PTAR_AUTO.bat`

Normal workflow:

1. normal Windows UAC elevation prompt;
2. detect Windows, NVIDIA GPU, free disk space;
3. reuse an existing MSVC/FXC installation when possible;
4. if MSVC is missing, download the official Microsoft VS 2019 Build Tools bootstrapper;
5. verify the downloaded EXE Authenticode signature is valid and signed by Microsoft;
6. install the C++ Build Tools workload silently with `--quiet --wait --norestart`;
7. if FXC is still missing, download the official Windows 8.1 SDK installer from the Microsoft SDK archive link;
8. verify the SDK installer Authenticode signature;
9. try unattended SDK installation;
10. configure `vcvars64.bat` and the FXC path automatically;
11. compile EDGE-NG v03 K185 and the hardware validator;
12. audit DXBC;
13. run 42-case GPU parity validation;
14. run GPU timing;
15. open the result folder.

## Safety rules

The automation:

- does not delete project files;
- does not delete previous runs;
- does not overwrite a previous hardware run;
- does not call `del`, `rmdir`, `Remove-Item`, disk formatting, cleanup, or purge commands;
- never reboots Windows automatically;
- does not bypass UAC;
- refuses unsigned/non-Microsoft downloaded installers;
- keeps downloaded installers and logs in the unique automation run directory.

Every execution creates:

`automation\windows\runs\run_YYYYMMDD_HHMMSS_RANDOM\`

## Microsoft endpoints

Visual Studio 2019 Build Tools bootstrapper:

`https://aka.ms/vs/16/release/vs_buildtools.exe`

Windows 8.1 SDK official archive installer link:

`https://go.microsoft.com/fwlink/p/?LinkId=323507`

These URLs are documented in the project because the automation depends on external Microsoft infrastructure.
If Microsoft changes the endpoint in the future, update this project version instead of silently substituting a third-party mirror.

## Reboot behavior

Some Microsoft installers can return 3010/1641.

PTAR records this but never restarts the machine automatically, because an unattended reboot could destroy unsaved user work.

If a reboot is required:
- save open work;
- restart Windows normally;
- double-click `RUN_PTAR_AUTO.bat` again.

The second execution detects the already installed components and continues.

## Build compatibility

The hardware validation executable is compiled with:

- `_WIN32_WINNT=0x0603`
- `WINVER=0x0603`
- `/SUBSYSTEM:CONSOLE,6.03`
- `/MT`

The last option statically links the MSVC runtime for the test executable.
