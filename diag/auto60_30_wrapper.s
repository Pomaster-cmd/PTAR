.intel_syntax noprefix
.text
.globl AutoGovernorWrapper
AutoGovernorWrapper:
    sub rsp, 0x58
    mov dword ptr [rsp+0x20], ecx
    mov dword ptr [rsp+0x24], edx

    # GW12 soft-OFF keeps FG internally armed and uses synthetic load-shed.
    # Soft-OFF must not inherit either the 15-FPS LOW cap or the 30-FPS HIGH cap.
    # The existing governor targets target/2, so runtime target 120 gives 60 REAL/s on OFF.
    cmp dword ptr [rip+SOFT_FLAG], 0
    jne .force_reset

    # FGGATE1: startup FG=OFF does not assert the GW12 soft-OFF flag yet.
    # Gate AUTO on the actual runtime FG-enable state as well, otherwise a heavy
    # scene can arm LOW before CTRL+F6 and force Sync2/target30 while FG is OFF.
    cmp dword ptr [rip+FG_ENABLED], 0
    je .force_reset

    cmp dword ptr [rip+AUTO_ARMED], 0
    jne .sample

.init:
    lea rcx, [rsp+0x38]
    call qword ptr [rip+QPF_IAT]
    test eax, eax
    je .call_gov
    mov rax, qword ptr [rsp+0x38]
    test rax, rax
    je .call_gov

    # Convert QPF to approximate ticks/ms once. Downshift threshold = 34 ms.
    # Upshift threshold = 33 ms. This admits ~33-FPS FG-OFF scenes while retaining a small margin below the 33.33-ms 30-REAL/s budget.
    xor edx, edx
    mov r11d, 1000
    div r11
    test rax, rax
    je .call_gov
    mov r8, rax
    imul r8, r8, 34
    mov qword ptr [rip+AUTO_DOWN_TICKS], r8
    imul r9, rax, 33
    mov qword ptr [rip+AUTO_UP_TICKS], r9

    # NOLOCK30_1: FG activation enters HIGH immediately. The validated LOW path
    # remains present in the binary for provenance/rollback but is not reachable
    # from the active controller. This removes the hard 30-FPS clamp and its
    # visible 60<->30 cadence steps during traversal through mixed-load areas.
    mov dword ptr [rip+AUTO_MODE], 0
    mov dword ptr [rip+AUTO_DOWN_COUNT], 0
    mov dword ptr [rip+AUTO_UP_COUNT], 0
    mov dword ptr [rip+AUTO_BACKOFF], 0
    mov dword ptr [rip+FG_TARGET_FPS], 60
    mov dword ptr [rip+AUTO_ARMED], 1
    jmp .call_gov

.sample:
    # Workload measurement is taken BEFORE the governor and measured from the
    # previous governor RETURN. It therefore excludes the governor's own wait.
    lea rcx, [rsp+0x30]
    call qword ptr [rip+QPC_IAT]
    test eax, eax
    je .call_gov
    mov r8, qword ptr [rsp+0x30]
    mov r9, qword ptr [rip+AUTO_LAST_EXIT]
    test r9, r9
    je .call_gov
    sub r8, r9
    mov qword ptr [rip+AUTO_LAST_WORK], r8
    inc dword ptr [rip+AUTO_SAMPLES]

    cmp dword ptr [rip+AUTO_MODE], 0
    jne .low_mode

.high_mode:
    # NOLOCK30_1: keep the 60-FPS target armed regardless of transient source
    # workload. The comparison is retained for binary-layout/reproducibility
    # continuity, but the branch is deliberately unconditional to HIGH.
    cmp r8, qword ptr [rip+AUTO_DOWN_TICKS]
    jmp .high_good
    inc dword ptr [rip+AUTO_DOWN_COUNT]
    cmp dword ptr [rip+AUTO_DOWN_COUNT], 24
    jb .call_gov

    # LOW branch = 15 REAL + 15 GENERATED, SyncInterval 2, exact 30-Hz cadence.
    mov dword ptr [rip+AUTO_MODE], 1
    mov dword ptr [rip+FG_TARGET_FPS], 30
    mov dword ptr [rip+AUTO_DOWN_COUNT], 0
    mov dword ptr [rip+AUTO_UP_COUNT], 0
    mov dword ptr [rip+AUTO_BACKOFF], 180
    inc dword ptr [rip+AUTO_SWITCH_DOWN]
    lea rcx, [rip+STR_LOW]
    call LOG_FUNC
    jmp .call_gov

.high_good:
    mov dword ptr [rip+AUTO_DOWN_COUNT], 0
    jmp .call_gov

.low_mode:
    # Initial activation uses a short 15-frame LOW settle before qualification.
    # A failed HIGH attempt still installs the longer 180-frame cooldown below.
    mov eax, dword ptr [rip+AUTO_BACKOFF]
    test eax, eax
    je .low_eval
    dec eax
    mov dword ptr [rip+AUTO_BACKOFF], eax
    mov dword ptr [rip+AUTO_UP_COUNT], 0
    jmp .call_gov

.low_eval:
    # HIGHENTRY2: use a leaky qualification counter instead of demanding an unbroken
    # streak. A good (<33-ms) source-work sample adds one point; a marginal/bad
    # sample removes one point. Promote after 24 accumulated points. This tolerates
    # isolated scheduler/GPU spikes while still refusing scenes that are mostly too slow.
    cmp r8, qword ptr [rip+AUTO_UP_TICKS]
    jae .low_bad
    inc dword ptr [rip+AUTO_UP_COUNT]
    cmp dword ptr [rip+AUTO_UP_COUNT], 24
    jb .call_gov

    mov dword ptr [rip+AUTO_MODE], 0
    mov dword ptr [rip+FG_TARGET_FPS], 60
    mov dword ptr [rip+AUTO_DOWN_COUNT], 0
    mov dword ptr [rip+AUTO_UP_COUNT], 0
    inc dword ptr [rip+AUTO_SWITCH_UP]
    lea rcx, [rip+STR_HIGH]
    call LOG_FUNC
    jmp .call_gov

.low_bad:
    mov eax, dword ptr [rip+AUTO_UP_COUNT]
    test eax, eax
    je .call_gov
    dec eax
    mov dword ptr [rip+AUTO_UP_COUNT], eax
    jmp .call_gov

.force_reset:
    mov dword ptr [rip+AUTO_ARMED], 0
    mov dword ptr [rip+AUTO_MODE], 0
    mov dword ptr [rip+AUTO_DOWN_COUNT], 0
    mov dword ptr [rip+AUTO_UP_COUNT], 0
    mov dword ptr [rip+AUTO_BACKOFF], 0
    mov qword ptr [rip+AUTO_LAST_EXIT], 0
    # Existing governor REAL target = FrameGenerationTargetFPS / 2.
    # Use 120 only while soft-OFF is asserted => 60 REAL/s instead of a hidden 30-FPS cap.
    mov dword ptr [rip+FG_TARGET_FPS], 120

.call_gov:
    mov ecx, dword ptr [rsp+0x20]
    mov edx, dword ptr [rsp+0x24]
    call GOV_WRAPPER
    # CRITICAL RETURNFIX1: the two original callsites branch on the governor EAX.
    # QueryPerformanceCounter returns BOOL in EAX too, so preserve the governor
    # result before the telemetry call and restore it before returning.
    mov dword ptr [rsp+0x28], eax

    # Timestamp the actual return from the existing governor + submit path. The
    # next entry minus this timestamp is the best available render-work estimate.
    lea rcx, [rsp+0x30]
    call qword ptr [rip+QPC_IAT]
    test eax, eax
    je .done
    mov rax, qword ptr [rsp+0x30]
    mov qword ptr [rip+AUTO_LAST_EXIT], rax

.done:
    mov eax, dword ptr [rsp+0x28]
    add rsp, 0x58
    ret

.globl AutoFallbackPresentWrapper
AutoFallbackPresentWrapper:
    # REAL-only fallback must never inherit AUTO_MODE=LOW as SyncInterval=2.
    # Force the Present helper's r13 mode register to 0 => SyncInterval 1.
    push r13
    sub rsp, 0x20
    xor r13d, r13d
    call PRESENT_HELPER
    add rsp, 0x20
    pop r13
    ret

.extern SOFT_FLAG
.extern FG_ENABLED
.extern AUTO_ARMED
.extern AUTO_MODE
.extern AUTO_DOWN_COUNT
.extern AUTO_UP_COUNT
.extern AUTO_BACKOFF
.extern AUTO_SWITCH_DOWN
.extern AUTO_SWITCH_UP
.extern AUTO_SAMPLES
.extern AUTO_LAST_EXIT
.extern AUTO_LAST_WORK
.extern AUTO_DOWN_TICKS
.extern AUTO_UP_TICKS
.extern FG_TARGET_FPS
.extern QPC_IAT
.extern QPF_IAT
.extern GOV_WRAPPER
.extern PRESENT_HELPER
.extern LOG_FUNC
.extern STR_LOW
.extern STR_HIGH
