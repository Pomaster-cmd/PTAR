.text
.intel_syntax noprefix
.globl quality_parse
.globl quality_apply_guard
.globl quality_calc_width
.globl quality_calc_height
.globl quality_hotkey_and_action0
.globl quality_status_wrapper

.extern QUALITY_PROFILE
.extern QUALITY_HOTKEY_LATCH
.extern QUALITY_APPLIED_TIER
.extern QUALITY_PENDING_ME
.extern QUALITY_CHANGES
.extern QUALITY_KEY
.extern BLEND_GUARD_GLOBAL
.extern FG_SCHEDULER_RUNNING
.extern GET_ASYNC_KEYSTATE_IAT
.extern HOTKEY_CHECK_FUNC
.extern STATUS_FUNC
.extern LOG_FUNC

# RC14 FGQUALITY2 profile contract:
# 0 LEGACY       = ME /3, Guard 65 (historical SatGat-style profile)
# 1 BALANCED     = ME /3, Guard 50
# 2 QUALITY      = ME /2, Guard 35 (RC13 Inquisitor profile)
# 3 CONSERVATIVE = ME /2, Guard 25
#
# BlendGuard changes are live. ME tier changes need the next FG construction;
# when changed while scheduler is active, QUALITY_PENDING_ME is set until the
# next ME geometry build.

# Replaces the legacy ignored FrameGenerationMEScalePercent parser call.
# Caller context preserves rdi=section, rsi=ini path, r14=GetPrivateProfileIntW.
quality_parse:
    sub rsp, 0x28
    lea rdx, [rip + QUALITY_KEY]
    mov rcx, rdi
    mov r8d, 2
    mov r9, rsi
    call r14
    cmp eax, 3
    jbe .qp_valid
    mov eax, 2
.qp_valid:
    mov dword ptr [rip + QUALITY_PROFILE], eax
    mov dword ptr [rip + QUALITY_HOTKEY_LATCH], 0
    mov dword ptr [rip + QUALITY_PENDING_ME], 0
    mov dword ptr [rip + QUALITY_CHANGES], 0
    add rsp, 0x28
    ret

# Replaces the final BlendGuard store so the named profile is authoritative.
quality_apply_guard:
    mov eax, dword ptr [rip + QUALITY_PROFILE]
    cmp eax, 0
    je .qag_legacy
    cmp eax, 1
    je .qag_balanced
    cmp eax, 2
    je .qag_quality
    mov ecx, 25
    jmp .qag_store
.qag_legacy:
    mov ecx, 65
    jmp .qag_store
.qag_balanced:
    mov ecx, 50
    jmp .qag_store
.qag_quality:
    mov ecx, 35
.qag_store:
    mov dword ptr [rip + BLEND_GUARD_GLOBAL], ecx
    ret

# RC13's fixed /2 geometry becomes profile-selectable.
# Width source is EBX; result must remain in EAX.
quality_calc_width:
    mov eax, dword ptr [rip + QUALITY_PROFILE]
    cmp eax, 2
    jb .qcw_div3
    mov dword ptr [rip + QUALITY_APPLIED_TIER], 1
    mov dword ptr [rip + QUALITY_PENDING_ME], 0
    lea eax, [rbx + 1]
    shr eax, 1
    ret
.qcw_div3:
    mov dword ptr [rip + QUALITY_APPLIED_TIER], 0
    mov dword ptr [rip + QUALITY_PENDING_ME], 0
    lea eax, [rbx + 2]
    movzx eax, ax
    imul eax, eax, 0xaaab
    shr eax, 0x11
    ret

# Height source is R14D; result must remain in ECX.
quality_calc_height:
    mov ecx, dword ptr [rip + QUALITY_PROFILE]
    cmp ecx, 2
    jb .qch_div3
    lea ecx, [r14 + 1]
    shr ecx, 1
    ret
.qch_div3:
    lea ecx, [r14 + 2]
    movzx ecx, cx
    imul ecx, ecx, 0xaaab
    shr ecx, 0x11
    ret

# Called in place of the original action-0 check sequence. It polls fixed
# CTRL+F8 with a release latch, cycles quality, then performs original action 0.
quality_hotkey_and_action0:
    sub rsp, 0x28
    mov ecx, 0x11                      # VK_CONTROL
    call qword ptr [rip + GET_ASYNC_KEYSTATE_IAT]
    test ax, ax
    jns .qh_release
    mov ecx, 0x77                      # VK_F8
    call qword ptr [rip + GET_ASYNC_KEYSTATE_IAT]
    test ax, ax
    jns .qh_release
    mov ecx, 0x10                      # VK_SHIFT must be UP (exact CTRL+F8)
    call qword ptr [rip + GET_ASYNC_KEYSTATE_IAT]
    test ax, ax
    js .qh_release
    mov ecx, 0x12                      # VK_MENU/ALT must be UP
    call qword ptr [rip + GET_ASYNC_KEYSTATE_IAT]
    test ax, ax
    js .qh_release
    cmp dword ptr [rip + QUALITY_HOTKEY_LATCH], 0
    jne .qh_after
    mov dword ptr [rip + QUALITY_HOTKEY_LATCH], 1
    mov eax, dword ptr [rip + QUALITY_PROFILE]
    inc eax
    and eax, 3
    mov dword ptr [rip + QUALITY_PROFILE], eax
    inc dword ptr [rip + QUALITY_CHANGES]
    call quality_apply_guard

    # If FG is active, flag a pending ME-tier transition only when /3<->/2 changed.
    cmp dword ptr [rip + FG_SCHEDULER_RUNNING], 0
    je .qh_no_pending
    mov eax, dword ptr [rip + QUALITY_PROFILE]
    shr eax, 1
    cmp eax, dword ptr [rip + QUALITY_APPLIED_TIER]
    je .qh_no_pending
    mov dword ptr [rip + QUALITY_PENDING_ME], 1
    jmp .qh_log
.qh_no_pending:
    mov dword ptr [rip + QUALITY_PENDING_ME], 0
.qh_log:
    call quality_log_profile
    cmp dword ptr [rip + QUALITY_PENDING_ME], 0
    je .qh_after
    lea rcx, [rip + str_pending]
    call LOG_FUNC
    jmp .qh_after
.qh_release:
    mov dword ptr [rip + QUALITY_HOTKEY_LATCH], 0
.qh_after:
    xor ecx, ecx
    call HOTKEY_CHECK_FUNC
    add rsp, 0x28
    ret

# Replaces the F8 status call. Existing status is emitted first, then RC14 profile.
quality_status_wrapper:
    sub rsp, 0x28
    call STATUS_FUNC
    call quality_log_profile
    cmp dword ptr [rip + QUALITY_PENDING_ME], 0
    je .qsw_done
    lea rcx, [rip + str_pending]
    call LOG_FUNC
.qsw_done:
    add rsp, 0x28
    ret

quality_log_profile:
    sub rsp, 0x28
    mov eax, dword ptr [rip + QUALITY_PROFILE]
    cmp eax, 0
    je .qlp_legacy
    cmp eax, 1
    je .qlp_balanced
    cmp eax, 2
    je .qlp_quality
    lea rcx, [rip + str_conservative]
    jmp .qlp_emit
.qlp_legacy:
    lea rcx, [rip + str_legacy]
    jmp .qlp_emit
.qlp_balanced:
    lea rcx, [rip + str_balanced]
    jmp .qlp_emit
.qlp_quality:
    lea rcx, [rip + str_quality]
.qlp_emit:
    call LOG_FUNC
    add rsp, 0x28
    ret

str_legacy:
    .asciz "P1FG7N QUALITY LEGACY - ME /3 - BLEND GUARD 65"
str_balanced:
    .asciz "P1FG7N QUALITY BALANCED - ME /3 - BLEND GUARD 50"
str_quality:
    .asciz "P1FG7N QUALITY QUALITY - ME /2 - BLEND GUARD 35"
str_conservative:
    .asciz "P1FG7N QUALITY CONSERVATIVE - ME /2 - BLEND GUARD 25"
str_pending:
    .asciz "P1FG7N QUALITY ME TIER PENDING - CTRL+F6 OFF/ON TO APPLY FULL PROFILE"
