.text
.intel_syntax noprefix
.globl dwmphase_present_wrapper
.globl dwmphase_snapshot
.globl dwmphase_hotkey_wrapper

.extern PRESENT_HELPER
.extern ORIG_DIAG
.extern NOTICE_FUNC
.extern LOG_FUNC
.extern LOG_UINT
.extern QPC_IAT
.extern QPC_DELTA_US
.extern LOADLIBRARYW_IAT
.extern GETPROCADDRESS_IAT
.extern GETASYNC_IAT

.extern DWM_MODULE_PTR
.extern DWM_TIMING_PTR
.extern G_TICK
.extern R_TICK
.extern DWM_CALLS
.extern DWM_FAILS
.extern QPC_FAILS
.extern NEG_START
.extern NEG_END
.extern G_SAMPLES
.extern R_SAMPLES
.extern LAST_PERIOD_TICKS
.extern BASE_SET
.extern BASE_DISPLAYED
.extern LAST_DISPLAYED
.extern BASE_DROPPED
.extern LAST_DROPPED
.extern BASE_LATE
.extern LAST_LATE
.extern G_START_HIST
.extern G_END_HIST
.extern R_START_HIST
.extern R_END_HIST
.extern COMPOSE_HIST
.extern LATCH_F4
.extern LATCH_F5
.extern LATCH_F8

# RC18 DWM PHASE2 AUTOCOLLECT1 passive sampler.
# Every 8th GENERATED and REAL frame is sampled. The exact RC18 Present helper
# remains untouched. DwmGetCompositionTimingInfo is called only after the sampled
# Present returns. No Sleep, DwmFlush, waitable object, swapchain or pacing change.
# Windows 8.1 contract: hwnd MUST be NULL, DWM_TIMING_INFO.cbSize = 292 (0x124).
dwmphase_present_wrapper:
    push rbx
    push rsi
    push rdi
    push r12
    push r13
    sub rsp, 0x170

    mov ebx, ecx
    mov dword ptr [rsp + 0x20], edx
    mov dword ptr [rsp + 0x24], r8d
    mov dword ptr [rsp + 0x28], r9d
    xor esi, esi

    test ebx, ebx
    jz .tick_real
    inc dword ptr [rip + G_TICK]
    mov eax, dword ptr [rip + G_TICK]
    test eax, 7
    jnz .call_original
    mov esi, 1
    jmp .qpc_start
.tick_real:
    inc dword ptr [rip + R_TICK]
    mov eax, dword ptr [rip + R_TICK]
    test eax, 7
    jnz .call_original
    mov esi, 1

.qpc_start:
    lea rcx, [rsp + 0x30]
    call qword ptr [rip + QPC_IAT]
    test eax, eax
    jnz .call_original
    inc dword ptr [rip + QPC_FAILS]
    mov qword ptr [rsp + 0x30], 0

.call_original:
    mov ecx, ebx
    mov edx, dword ptr [rsp + 0x20]
    mov r8d, dword ptr [rsp + 0x24]
    mov r9d, dword ptr [rsp + 0x28]
    call PRESENT_HELPER

    test esi, esi
    jz .done

    lea rcx, [rsp + 0x38]
    call qword ptr [rip + QPC_IAT]
    test eax, eax
    jnz .resolve_dwm
    inc dword ptr [rip + QPC_FAILS]
    jmp .done

.resolve_dwm:
    mov rax, qword ptr [rip + DWM_TIMING_PTR]
    test rax, rax
    jne .have_dwm
    mov rax, qword ptr [rip + DWM_MODULE_PTR]
    test rax, rax
    jne .have_module
    lea rcx, [rip + str_dwmapi]
    call qword ptr [rip + LOADLIBRARYW_IAT]
    test rax, rax
    je .dwm_fail
    mov qword ptr [rip + DWM_MODULE_PTR], rax
.have_module:
    mov rcx, rax
    lea rdx, [rip + str_dwmget]
    call qword ptr [rip + GETPROCADDRESS_IAT]
    test rax, rax
    je .dwm_fail
    mov qword ptr [rip + DWM_TIMING_PTR], rax
.have_dwm:
    # struct at rsp+0x40. Only cbSize must be initialized.
    mov dword ptr [rsp + 0x40], 0x124
    xor ecx, ecx
    lea rdx, [rsp + 0x40]
    call rax
    inc dword ptr [rip + DWM_CALLS]
    test eax, eax
    js .dwm_fail_post

    # packed DWM_TIMING_INFO offsets (MSVC/Windows ABI, sizeof=292):
    # qpcRefreshPeriod +12, qpcVBlank +28, qpcCompose +48,
    # cFramesLate +112, cFramesDisplayed +180, cFramesDropped +212.
    mov r12, qword ptr [rsp + 0x40 + 12]
    test r12, r12
    jz .dwm_fail_post
    mov qword ptr [rip + LAST_PERIOD_TICKS], r12
    mov r13, qword ptr [rsp + 0x40 + 28]

    test ebx, ebx
    jz .sample_real
    inc dword ptr [rip + G_SAMPLES]
    jmp .sample_common
.sample_real:
    inc dword ptr [rip + R_SAMPLES]

.sample_common:
    # START phase bucket relative to qpcVBlank.
    mov rax, qword ptr [rsp + 0x30]
    test rax, rax
    jz .end_phase
    cmp rax, r13
    jae .start_nonneg
    inc dword ptr [rip + NEG_START]
    jmp .end_phase
.start_nonneg:
    sub rax, r13
    xor edx, edx
    div r12
    mov rax, rdx
    shl rax, 3
    xor edx, edx
    div r12
    cmp eax, 7
    jbe .start_bucket_ok
    mov eax, 7
.start_bucket_ok:
    mov edi, eax
    test ebx, ebx
    jz .start_real_hist
    lea rdx, [rip + G_START_HIST]
    jmp .start_hist_inc
.start_real_hist:
    lea rdx, [rip + R_START_HIST]
.start_hist_inc:
    inc dword ptr [rdx + rdi*4]

.end_phase:
    # END phase bucket relative to qpcVBlank.
    mov rax, qword ptr [rsp + 0x38]
    cmp rax, r13
    jae .end_nonneg
    inc dword ptr [rip + NEG_END]
    jmp .compose_phase
.end_nonneg:
    sub rax, r13
    xor edx, edx
    div r12
    mov rax, rdx
    shl rax, 3
    xor edx, edx
    div r12
    cmp eax, 7
    jbe .end_bucket_ok
    mov eax, 7
.end_bucket_ok:
    mov edi, eax
    test ebx, ebx
    jz .end_real_hist
    lea rdx, [rip + G_END_HIST]
    jmp .end_hist_inc
.end_real_hist:
    lea rdx, [rip + R_END_HIST]
.end_hist_inc:
    inc dword ptr [rdx + rdi*4]

.compose_phase:
    # DWM qpcCompose phase relative to qpcVBlank.
    mov rax, qword ptr [rsp + 0x40 + 48]
    cmp rax, r13
    jb .frame_stats
    sub rax, r13
    xor edx, edx
    div r12
    mov rax, rdx
    shl rax, 3
    xor edx, edx
    div r12
    cmp eax, 7
    jbe .compose_bucket_ok
    mov eax, 7
.compose_bucket_ok:
    mov edi, eax
    lea rdx, [rip + COMPOSE_HIST]
    inc dword ptr [rdx + rdi*4]

.frame_stats:
    # First/last DWM aggregate counters for context only.
    mov rax, qword ptr [rsp + 0x40 + 180]
    mov qword ptr [rip + LAST_DISPLAYED], rax
    mov rax, qword ptr [rsp + 0x40 + 212]
    mov qword ptr [rip + LAST_DROPPED], rax
    mov rax, qword ptr [rsp + 0x40 + 112]
    mov qword ptr [rip + LAST_LATE], rax
    cmp dword ptr [rip + BASE_SET], 0
    jne .done
    mov rax, qword ptr [rip + LAST_DISPLAYED]
    mov qword ptr [rip + BASE_DISPLAYED], rax
    mov rax, qword ptr [rip + LAST_DROPPED]
    mov qword ptr [rip + BASE_DROPPED], rax
    mov rax, qword ptr [rip + LAST_LATE]
    mov qword ptr [rip + BASE_LATE], rax
    mov dword ptr [rip + BASE_SET], 1
    jmp .done

.dwm_fail_post:
    inc dword ptr [rip + DWM_FAILS]
    jmp .done
.dwm_fail:
    inc dword ptr [rip + DWM_FAILS]

.done:
    add rsp, 0x170
    pop r13
    pop r12
    pop rdi
    pop rsi
    pop rbx
    ret

# Helper: log eight dword buckets. RCX=hist ptr, RDX=labels ptr.
dwmlab_log8:
    push rbx
    push rsi
    push rdi
    sub rsp, 0x20
    mov rbx, rcx
    mov rsi, rdx
    mov edi, 8
.log8_loop:
    mov rcx, rsi
    mov edx, dword ptr [rbx]
    call LOG_UINT
    add rsi, 5
    add rbx, 4
    dec edi
    jnz .log8_loop
    add rsp, 0x20
    pop rdi
    pop rsi
    pop rbx
    ret

# CTRL+F4 snapshot. F8 is not required and no per-frame file I/O occurs.
dwmphase_snapshot:
    push rbx
    sub rsp, 0x20
    lea rcx, [rip + str_banner]
    call LOG_FUNC

    lea rcx, [rip + lbl_calls]
    mov edx, dword ptr [rip + DWM_CALLS]
    call LOG_UINT
    lea rcx, [rip + lbl_fails]
    mov edx, dword ptr [rip + DWM_FAILS]
    call LOG_UINT
    lea rcx, [rip + lbl_qpcfail]
    mov edx, dword ptr [rip + QPC_FAILS]
    call LOG_UINT
    lea rcx, [rip + lbl_gsamples]
    mov edx, dword ptr [rip + G_SAMPLES]
    call LOG_UINT
    lea rcx, [rip + lbl_rsamples]
    mov edx, dword ptr [rip + R_SAMPLES]
    call LOG_UINT
    lea rcx, [rip + lbl_negstart]
    mov edx, dword ptr [rip + NEG_START]
    call LOG_UINT
    lea rcx, [rip + lbl_negend]
    mov edx, dword ptr [rip + NEG_END]
    call LOG_UINT

    mov rdx, qword ptr [rip + LAST_PERIOD_TICKS]
    xor ecx, ecx
    test rdx, rdx
    jz .period_zero
    call QPC_DELTA_US
    mov edx, eax
    jmp .period_log
.period_zero:
    xor edx, edx
.period_log:
    lea rcx, [rip + lbl_period]
    call LOG_UINT

    mov rax, qword ptr [rip + LAST_DISPLAYED]
    sub rax, qword ptr [rip + BASE_DISPLAYED]
    mov edx, eax
    lea rcx, [rip + lbl_displayed]
    call LOG_UINT
    mov rax, qword ptr [rip + LAST_DROPPED]
    sub rax, qword ptr [rip + BASE_DROPPED]
    mov edx, eax
    lea rcx, [rip + lbl_dropped]
    call LOG_UINT
    mov rax, qword ptr [rip + LAST_LATE]
    sub rax, qword ptr [rip + BASE_LATE]
    mov edx, eax
    lea rcx, [rip + lbl_late]
    call LOG_UINT

    lea rcx, [rip + G_START_HIST]
    lea rdx, [rip + labels_gs]
    call dwmlab_log8
    lea rcx, [rip + G_END_HIST]
    lea rdx, [rip + labels_ge]
    call dwmlab_log8
    lea rcx, [rip + R_START_HIST]
    lea rdx, [rip + labels_rs]
    call dwmlab_log8
    lea rcx, [rip + R_END_HIST]
    lea rdx, [rip + labels_re]
    call dwmlab_log8
    lea rcx, [rip + COMPOSE_HIST]
    lea rdx, [rip + labels_cp]
    call dwmlab_log8

    lea rcx, [rip + str_end]
    call LOG_FUNC
    add rsp, 0x20
    pop rbx
    ret

# Existing per-frame diagnostic dispatch wrapper + CTRL+F4/CTRL+F5 HUD notices.
dwmphase_hotkey_wrapper:
    sub rsp, 0x38
    call ORIG_DIAG
    mov dword ptr [rsp + 0x30], eax

    mov ecx, 0x11
    call qword ptr [rip + GETASYNC_IAT]
    test ax, 0x8000
    jz .no_ctrl

    mov ecx, 0x73
    call qword ptr [rip + GETASYNC_IAT]
    test ax, 0x8000
    jz .f4_up
    cmp dword ptr [rip + LATCH_F4], 0
    jne .after_f4
    mov dword ptr [rip + LATCH_F4], 1
    mov dword ptr [rsp + 0x20], 3000
    mov ecx, 9
    mov edx, 1
    xor r8d, r8d
    xor r9d, r9d
    call NOTICE_FUNC
    lea rcx, [rip + str_hotkey_f4]
    call LOG_FUNC
    call dwmphase_snapshot
    jmp .after_f4
.f4_up:
    mov dword ptr [rip + LATCH_F4], 0
.after_f4:

    mov ecx, 0x74
    call qword ptr [rip + GETASYNC_IAT]
    test ax, 0x8000
    jz .f5_up
    cmp dword ptr [rip + LATCH_F5], 0
    jne .after_ctrl
    mov dword ptr [rip + LATCH_F5], 1
    mov dword ptr [rsp + 0x20], 3000
    mov ecx, 10
    xor edx, edx
    xor r8d, r8d
    xor r9d, r9d
    call NOTICE_FUNC
    lea rcx, [rip + str_hotkey_f5]
    call LOG_FUNC
    jmp .after_ctrl
.f5_up:
    mov dword ptr [rip + LATCH_F5], 0
    jmp .after_ctrl
.no_ctrl:
    mov dword ptr [rip + LATCH_F4], 0
    mov dword ptr [rip + LATCH_F5], 0
.after_ctrl:
    # DWMPHASE2 AUTOCOLLECT1: every normal F8 status press also dumps the
    # passive DWM histogram. This survives the user's no-alt-tab workflow and
    # removes dependency on remembering CTRL+F4. Original F8 status handling
    # already ran via ORIG_DIAG above; this is diagnostic logging only.
    mov ecx, 0x77
    call qword ptr [rip + GETASYNC_IAT]
    test ax, 0x8000
    jz .f8_up
    cmp dword ptr [rip + LATCH_F8], 0
    jne .done_hotkey
    mov dword ptr [rip + LATCH_F8], 1
    lea rcx, [rip + str_hotkey_f8]
    call LOG_FUNC
    call dwmphase_snapshot
    jmp .done_hotkey
.f8_up:
    mov dword ptr [rip + LATCH_F8], 0
.done_hotkey:
    mov eax, dword ptr [rsp + 0x30]
    add rsp, 0x38
    ret

.p2align 2
str_dwmapi:
    .short 100,119,109,97,112,105,46,100,108,108,0
str_dwmget:
    .asciz "DwmGetCompositionTimingInfo"
str_banner:
    .asciz "========== RC18 LAB DWMPHASE2 AUTOCOLLECT1 PASSIVE DWM PHASE SNAPSHOT =========="
str_end:
    .asciz "========== END RC18 LAB DWMPHASE2 AUTOCOLLECT1 =========="
str_hotkey_f4:
    .asciz "HOTKEY DIAG DWMPHASE2 SNAPSHOT - CTRL+F4"
str_hotkey_f5:
    .asciz "HOTKEY DIAG VISIBLE TEST - CTRL+F5"
str_hotkey_f8:
    .asciz "HOTKEY DIAG DWMPHASE2 AUTODUMP - F8"
lbl_calls: .asciz "DWM CALLS "
lbl_fails: .asciz "DWM FAILS "
lbl_qpcfail: .asciz "QPC FAILS "
lbl_gsamples: .asciz "G SAMPLES "
lbl_rsamples: .asciz "R SAMPLES "
lbl_negstart: .asciz "NEG START "
lbl_negend: .asciz "NEG END "
lbl_period: .asciz "REFRESH US "
lbl_displayed: .asciz "DWM DISPLAYED DELTA "
lbl_dropped: .asciz "DWM DROPPED DELTA "
lbl_late: .asciz "DWM LATE DELTA "
labels_gs: .ascii "GS0 \0GS1 \0GS2 \0GS3 \0GS4 \0GS5 \0GS6 \0GS7 \0"
labels_ge: .ascii "GE0 \0GE1 \0GE2 \0GE3 \0GE4 \0GE5 \0GE6 \0GE7 \0"
labels_rs: .ascii "RS0 \0RS1 \0RS2 \0RS3 \0RS4 \0RS5 \0RS6 \0RS7 \0"
labels_re: .ascii "RE0 \0RE1 \0RE2 \0RE3 \0RE4 \0RE5 \0RE6 \0RE7 \0"
labels_cp: .ascii "CP0 \0CP1 \0CP2 \0CP3 \0CP4 \0CP5 \0CP6 \0CP7 \0"
