.text
.intel_syntax noprefix
.globl quality_hotkey_menu1

.extern QUALITY_PROFILE
.extern QUALITY_HOTKEY_LATCH
.extern QUALITY_APPLIED_TIER
.extern QUALITY_PENDING_ME
.extern QUALITY_CHANGES
.extern QUALITY_ACTIVE_PROFILE
.extern FG_SCHEDULER_RUNNING
.extern GET_ASYNC_KEYSTATE_IAT
.extern QPC_IAT
.extern NOTICE_TYPE
.extern NOTICE_DEADLINE
.extern QUALITY_APPLY_GUARD
.extern HOTKEY_CHECK_FUNC
.extern NOTICE_FUNC

# RC17 QUALITYMENU1 contract:
# - first exact CTRL+F8 press while the quality notice is not visible: show current selected mode only.
# - a subsequent fully released + re-pressed CTRL+F8 while that notice is still visible: advance one mode and refresh the notice.
# - after the notice expires, the next press returns to show-only behavior.
# - exact chord only: Shift and Alt must be up.
# - existing RC14 pending-ME contract remains unchanged; no teardown/rebuild is triggered here.
quality_hotkey_menu1:
    sub rsp, 0x38

    mov ecx, 0x11                      # VK_CONTROL
    call qword ptr [rip + GET_ASYNC_KEYSTATE_IAT]
    test ax, ax
    jns .release
    mov ecx, 0x77                      # VK_F8
    call qword ptr [rip + GET_ASYNC_KEYSTATE_IAT]
    test ax, ax
    jns .release
    mov ecx, 0x10                      # exact CTRL+F8: SHIFT up
    call qword ptr [rip + GET_ASYNC_KEYSTATE_IAT]
    test ax, ax
    js .release
    mov ecx, 0x12                      # exact CTRL+F8: ALT up
    call qword ptr [rip + GET_ASYNC_KEYSTATE_IAT]
    test ax, ax
    js .release

    cmp dword ptr [rip + QUALITY_HOTKEY_LATCH], 0
    jne .action0
    mov dword ptr [rip + QUALITY_HOTKEY_LATCH], 1

    # A quality notice currently on screen is the menu-open state.
    cmp dword ptr [rip + NOTICE_TYPE], 8
    jne .show
    lea rcx, [rsp + 0x28]
    call qword ptr [rip + QPC_IAT]
    test eax, eax
    jz .show
    mov rax, qword ptr [rsp + 0x28]
    cmp rax, qword ptr [rip + NOTICE_DEADLINE]
    jae .show

    # Menu is still visible: cycle to the next profile.
    mov eax, dword ptr [rip + QUALITY_PROFILE]
    inc eax
    and eax, 3
    mov dword ptr [rip + QUALITY_PROFILE], eax
    inc dword ptr [rip + QUALITY_CHANGES]
    call QUALITY_APPLY_GUARD

    # Preserve RC14 semantics: only a /3 <-> /2 tier crossing while FG runs
    # becomes pending. Same-tier changes apply live.
    cmp dword ptr [rip + FG_SCHEDULER_RUNNING], 0
    je .inactive_apply
    mov eax, dword ptr [rip + QUALITY_PROFILE]
    shr eax, 1
    cmp eax, dword ptr [rip + QUALITY_APPLIED_TIER]
    jne .pending
    mov dword ptr [rip + QUALITY_PENDING_ME], 0
    mov eax, dword ptr [rip + QUALITY_PROFILE]
    mov dword ptr [rip + QUALITY_ACTIVE_PROFILE], eax
    jmp .show
.pending:
    mov dword ptr [rip + QUALITY_PENDING_ME], 1
    jmp .show
.inactive_apply:
    mov dword ptr [rip + QUALITY_PENDING_ME], 0
    mov eax, dword ptr [rip + QUALITY_PROFILE]
    mov dword ptr [rip + QUALITY_ACTIVE_PROFILE], eax

.show:
    # When FG is off there is no separate live interpolation state.
    cmp dword ptr [rip + FG_SCHEDULER_RUNNING], 0
    jne .notice
    mov eax, dword ptr [rip + QUALITY_PROFILE]
    mov dword ptr [rip + QUALITY_ACTIVE_PROFILE], eax
.notice:
    mov ecx, 8
    mov edx, dword ptr [rip + QUALITY_ACTIVE_PROFILE]
    mov r8d, dword ptr [rip + QUALITY_PROFILE]
    mov r9d, dword ptr [rip + QUALITY_PENDING_ME]
    mov dword ptr [rsp + 0x20], 3000
    call NOTICE_FUNC
    jmp .action0

.release:
    mov dword ptr [rip + QUALITY_HOTKEY_LATCH], 0
.action0:
    xor ecx, ecx
    call HOTKEY_CHECK_FUNC
    add rsp, 0x38
    ret
