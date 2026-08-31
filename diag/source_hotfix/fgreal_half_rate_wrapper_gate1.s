.intel_syntax noprefix
.text
.globl FGRealGovernorWrapper
FGRealGovernorWrapper:
    sub rsp, 72
    mov dword ptr [rsp + 32], ecx
    mov dword ptr [rsp + 36], edx

    # STRICT FG-ONLY CONTRACT:
    # The hook call-sites can remain reachable while FG is requested but not
    # actually initialized (for example after a failed activation). Do not
    # pace the game until the runtime itself reports ENABLED and its scheduler
    # is running. In all other states, clear governor phase and pass through.
    cmp dword ptr [rip + FG_ENABLED], 0
    je .inactive_passthrough
    cmp dword ptr [rip + FG_SCHEDULER_RUNNING], 0
    je .inactive_passthrough

    mov r10d, dword ptr [rip + FG_TARGET_FPS]
    cmp r10d, 2
    jb .fail_open
    cmp r10d, 240
    ja .fail_open
    mov eax, dword ptr [rip + GOV_LAST_TARGET]
    cmp eax, r10d
    jne .initialize
    cmp dword ptr [rip + GOV_ARMED], 0
    je .initialize

    lea rcx, [rsp + 48]
    call qword ptr [rip + QPC_IAT]
    test eax, eax
    je .fail_open
    mov r8, qword ptr [rsp + 48]
    mov r9, qword ptr [rip + GOV_NEXT_TICK]
    cmp r8, r9
    jae .late_rebase

.wait_loop:
    mov rax, r9
    sub rax, r8
    xor ecx, ecx
    cmp rax, qword ptr [rip + GOV_SLEEP1_TICKS]
    jbe .do_sleep
    mov ecx, 1
.do_sleep:
    call qword ptr [rip + SLEEP_IAT]
    lea rcx, [rsp + 48]
    call qword ptr [rip + QPC_IAT]
    test eax, eax
    je .fail_open
    mov r8, qword ptr [rsp + 48]
    mov r9, qword ptr [rip + GOV_NEXT_TICK]
    cmp r8, r9
    jb .wait_loop

.waited_done:
    mov rax, r9
    add rax, qword ptr [rip + GOV_INTERVAL]
    cmp r8, rax
    jae .late_rebase
    mov qword ptr [rip + GOV_NEXT_TICK], rax
    jmp .submit

.late_rebase:
    mov rax, r8
    add rax, qword ptr [rip + GOV_INTERVAL]
    mov qword ptr [rip + GOV_NEXT_TICK], rax
    jmp .submit

.initialize:
    lea rcx, [rsp + 56]
    call qword ptr [rip + QPF_IAT]
    test eax, eax
    je .fail_open
    mov rax, qword ptr [rsp + 56]
    test rax, rax
    je .fail_open

    shl rax, 1
    xor edx, edx
    mov r10d, dword ptr [rip + FG_TARGET_FPS]
    mov r11d, r10d
    div r11
    test rax, rax
    je .fail_open
    mov qword ptr [rip + GOV_INTERVAL], rax

    mov rax, qword ptr [rsp + 56]
    xor edx, edx
    mov r11d, 400
    div r11
    test rax, rax
    jne .have_sleep_threshold
    mov eax, 1
.have_sleep_threshold:
    mov qword ptr [rip + GOV_SLEEP1_TICKS], rax
    mov dword ptr [rip + GOV_LAST_TARGET], r10d

    lea rcx, [rsp + 48]
    call qword ptr [rip + QPC_IAT]
    test eax, eax
    je .fail_open
    mov r8, qword ptr [rsp + 48]
    mov rax, r8
    add rax, qword ptr [rip + GOV_INTERVAL]
    mov qword ptr [rip + GOV_NEXT_TICK], rax
    mov dword ptr [rip + GOV_ARMED], 1
    jmp .submit

.inactive_passthrough:
    mov dword ptr [rip + GOV_ARMED], 0
    jmp .submit

.fail_open:
    mov dword ptr [rip + GOV_ARMED], 0

.submit:
    mov ecx, dword ptr [rsp + 32]
    mov edx, dword ptr [rsp + 36]
    add rsp, 72
    jmp FG_SUBMIT

.extern FG_ENABLED
.extern FG_SCHEDULER_RUNNING
.extern FG_TARGET_FPS
.extern QPC_IAT
.extern QPF_IAT
.extern SLEEP_IAT
.extern GOV_NEXT_TICK
.extern GOV_INTERVAL
.extern GOV_SLEEP1_TICKS
.extern GOV_LAST_TARGET
.extern GOV_ARMED
.extern FG_SUBMIT
