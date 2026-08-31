.text
.intel_syntax noprefix
.globl quality_parse_hud1
.globl quality_calc_width_hud1
.globl quality_hotkey_hud1
.globl quality_status_hud1

.extern RC14_QUALITY_PARSE
.extern RC14_QUALITY_CALC_WIDTH
.extern RC14_QUALITY_HOTKEY_ACTION0
.extern QUALITY_PROFILE
.extern QUALITY_APPLIED_TIER
.extern QUALITY_PENDING_ME
.extern QUALITY_ACTIVE_PROFILE
.extern NOTICE_FUNC
.extern STATUS_FUNC
.extern LOG_FUNC

# RC15 FGQUALITYHUD1: presentation/telemetry only.
# It does not change RC14 quality math, FG transport, cadence, GATE1 or resize guard.

quality_parse_hud1:
    sub rsp, 0x28
    call RC14_QUALITY_PARSE
    mov eax, dword ptr [rip + QUALITY_PROFILE]
    mov dword ptr [rip + QUALITY_ACTIVE_PROFILE], eax
    call quality_log_detail
    add rsp, 0x28
    ret

# RC14 applies/clears the ME tier in its width calculator. Once that constructor
# runs, the selected profile is the fully applied profile again.
quality_calc_width_hud1:
    sub rsp, 0x28
    mov qword ptr [rsp + 0x20], rcx
    call RC14_QUALITY_CALC_WIDTH
    mov ecx, dword ptr [rip + QUALITY_PROFILE]
    mov dword ptr [rip + QUALITY_ACTIVE_PROFILE], ecx
    mov rcx, qword ptr [rsp + 0x20]
    add rsp, 0x28
    ret

# Wrap RC14 CTRL+F8 polling/action. If the profile changed, display an in-game
# feedback notice through the already validated notice renderer:
#   CURR <active> NOW
#   SEL  <selected> P <0|1>
# P=1 means the ME tier still needs CTRL+F6 OFF/ON.
quality_hotkey_hud1:
    push rbx
    sub rsp, 0x30
    mov ebx, dword ptr [rip + QUALITY_PROFILE]
    call RC14_QUALITY_HOTKEY_ACTION0
    mov dword ptr [rsp + 0x28], eax
    mov eax, dword ptr [rip + QUALITY_PROFILE]
    cmp eax, ebx
    je .qhh_done
    cmp dword ptr [rip + QUALITY_PENDING_ME], 0
    jne .qhh_keep_active
    mov dword ptr [rip + QUALITY_ACTIVE_PROFILE], eax
.qhh_keep_active:
    mov ecx, 8
    mov edx, dword ptr [rip + QUALITY_ACTIVE_PROFILE]
    mov r8d, dword ptr [rip + QUALITY_PROFILE]
    mov r9d, dword ptr [rip + QUALITY_PENDING_ME]
    mov dword ptr [rsp + 0x20], 3000
    call NOTICE_FUNC
    call quality_log_detail
.qhh_done:
    mov eax, dword ptr [rsp + 0x28]
    add rsp, 0x30
    pop rbx
    ret

quality_status_hud1:
    sub rsp, 0x28
    call STATUS_FUNC
    call quality_log_detail
    add rsp, 0x28
    ret

quality_log_detail:
    sub rsp, 0x28
    lea rcx, [rip + str_hotkey]
    call LOG_FUNC

    mov eax, dword ptr [rip + QUALITY_ACTIVE_PROFILE]
    cmp eax, 0
    je .act0
    cmp eax, 1
    je .act1
    cmp eax, 2
    je .act2
    lea rcx, [rip + str_act3]
    jmp .act_emit
.act0:
    lea rcx, [rip + str_act0]
    jmp .act_emit
.act1:
    lea rcx, [rip + str_act1]
    jmp .act_emit
.act2:
    lea rcx, [rip + str_act2]
.act_emit:
    call LOG_FUNC

    mov eax, dword ptr [rip + QUALITY_PROFILE]
    cmp eax, 0
    je .sel0
    cmp eax, 1
    je .sel1
    cmp eax, 2
    je .sel2
    lea rcx, [rip + str_sel3]
    jmp .sel_emit
.sel0:
    lea rcx, [rip + str_sel0]
    jmp .sel_emit
.sel1:
    lea rcx, [rip + str_sel1]
    jmp .sel_emit
.sel2:
    lea rcx, [rip + str_sel2]
.sel_emit:
    call LOG_FUNC

    cmp dword ptr [rip + QUALITY_APPLIED_TIER], 0
    je .tier3
    lea rcx, [rip + str_tier2]
    jmp .tier_emit
.tier3:
    lea rcx, [rip + str_tier3]
.tier_emit:
    call LOG_FUNC

    cmp dword ptr [rip + QUALITY_PENDING_ME], 0
    je .pend_no
    lea rcx, [rip + str_pend_yes]
    jmp .pend_emit
.pend_no:
    lea rcx, [rip + str_pend_no]
.pend_emit:
    call LOG_FUNC
    add rsp, 0x28
    ret

str_hotkey:
    .asciz "P1U46 HOTKEY FG QUALITY = CTRL+F8"
str_act0:
    .asciz "P1FG7N QUALITY ACTIVE 0 LEGACY"
str_act1:
    .asciz "P1FG7N QUALITY ACTIVE 1 BALANCED"
str_act2:
    .asciz "P1FG7N QUALITY ACTIVE 2 QUALITY"
str_act3:
    .asciz "P1FG7N QUALITY ACTIVE 3 CONSERVATIVE"
str_sel0:
    .asciz "P1FG7N QUALITY SELECTED 0 LEGACY"
str_sel1:
    .asciz "P1FG7N QUALITY SELECTED 1 BALANCED"
str_sel2:
    .asciz "P1FG7N QUALITY SELECTED 2 QUALITY"
str_sel3:
    .asciz "P1FG7N QUALITY SELECTED 3 CONSERVATIVE"
str_tier3:
    .asciz "P1FG7N QUALITY ME TIER DIV3"
str_tier2:
    .asciz "P1FG7N QUALITY ME TIER DIV2"
str_pend_no:
    .asciz "P1FG7N QUALITY RESTART PENDING NO"
str_pend_yes:
    .asciz "P1FG7N QUALITY RESTART PENDING YES - CTRL+F6 OFF/ON TO APPLY ME TIER"
