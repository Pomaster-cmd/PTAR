.intel_syntax noprefix
.text
.global rc41b_loader

# RC41B bootstrap loader.
# Intended placement RVA: 0x035006C0.
# Main entry never blocks rendering: it spawns a worker thread, then restores the
# value RCX expected by the patched P1U46 call site and returns.
rc41b_loader:
    sub rsp, 0x38
    lea r10, [rip]
    sub r10, 0x035006CB
    mov [rsp+0x30], r10

    xor eax, eax
    mov dl, 1
    lock cmpxchg byte ptr [rip + rc41b_flag], dl
    jne rc41b_main_finish

    xor ecx, ecx
    xor edx, edx
    lea r8, [rip + rc41b_worker]
    mov r9, r10
    mov qword ptr [rsp+0x20], 0
    mov qword ptr [rsp+0x28], 0
    call qword ptr [r10+0x49EA0]      # CreateThread
    test rax, rax
    jne rc41b_thread_started
    mov byte ptr [rip + rc41b_flag], 0 # allow a later call-site retry
    jmp rc41b_main_finish

rc41b_thread_started:
    mov rcx, rax
    mov r10, [rsp+0x30]
    call qword ptr [r10+0x49E70]      # CloseHandle

rc41b_main_finish:
    mov r10, [rsp+0x30]
    lea rcx, [r10+0x3E42B]            # preserve RC41/RC40 call-site contract
    add rsp, 0x38
    ret

# Append a sibling filename to a module path in-place.
# rcx=buffer, edx=character count from GetModuleFileNameW, r8=wide filename.
# volatile registers only; no calls.
rc41b_append_name:
    mov r10, rcx
    mov eax, edx
    lea r11, [rcx+rax*2]
    cmp r11, rcx
    jbe rc41b_append_at_start
rc41b_scan_slash:
    sub r11, 2
    cmp word ptr [r11], 0x005C
    je rc41b_slash_found
    cmp r11, rcx
    ja rc41b_scan_slash
rc41b_append_at_start:
    mov r11, rcx
    jmp rc41b_copy_name
rc41b_slash_found:
    add r11, 2
rc41b_copy_name:
    movzx eax, word ptr [r8]
    mov word ptr [r11], ax
    add r8, 2
    add r11, 2
    test ax, ax
    jne rc41b_copy_name
    ret

# Persist a fixed 64-byte diagnostic record.
# rcx=runtime base, rdx=status path, r8=record pointer.
rc41b_write_state:
    sub rsp, 0x58
    mov [rsp+0x40], rcx
    mov [rsp+0x48], rdx
    mov [rsp+0x50], r8

    mov rcx, rdx
    mov edx, 0x40000000               # GENERIC_WRITE
    mov r8d, 3                        # FILE_SHARE_READ|FILE_SHARE_WRITE
    xor r9d, r9d
    mov qword ptr [rsp+0x20], 2       # CREATE_ALWAYS
    mov qword ptr [rsp+0x28], 0x80    # FILE_ATTRIBUTE_NORMAL
    mov qword ptr [rsp+0x30], 0
    mov r10, [rsp+0x40]
    call qword ptr [r10+0x49E88]      # CreateFileW
    cmp rax, -1
    je rc41b_write_done

    mov [rsp+0x38], rax
    mov rcx, rax
    mov rdx, [rsp+0x50]
    mov r8d, 64
    lea r9, [rsp+0x34]
    mov qword ptr [rsp+0x20], 0
    mov r10, [rsp+0x40]
    call qword ptr [r10+0x49F80]      # WriteFile

    mov rcx, [rsp+0x38]
    mov r10, [rsp+0x40]
    call qword ptr [r10+0x49E70]      # CloseHandle
rc41b_write_done:
    add rsp, 0x58
    ret

# Worker thread. rcx = runtime HMODULE/base.
rc41b_worker:
    sub rsp, 0x568
    mov [rsp+0x40], rcx               # runtime base

    # record[0..63]
    lea r10, [rsp+0x60]
    xor eax, eax
    mov qword ptr [r10+0x08], rax
    mov qword ptr [r10+0x10], rax
    mov qword ptr [r10+0x18], rax
    mov qword ptr [r10+0x20], rax
    mov qword ptr [r10+0x28], rax
    mov qword ptr [r10+0x30], rax
    mov qword ptr [r10+0x38], rax
    mov rax, 0x3154534231344352       # "RC41BST1"
    mov qword ptr [r10+0x00], rax
    mov dword ptr [r10+0x08], 1       # record version
    mov rax, [rsp+0x40]
    mov qword ptr [r10+0x30], rax     # runtime base
    mov dword ptr [r10+0x18], 0x80000000 # autostart rc sentinel

    # Build absolute DLL path beside the loaded proxy runtime.
    mov rcx, [rsp+0x40]
    lea rdx, [rsp+0xA0]
    mov r8d, 260
    mov r10, [rsp+0x40]
    call qword ptr [r10+0x49EE0]      # GetModuleFileNameW
    test eax, eax
    je rc41b_worker_terminal_no_path
    mov edx, eax
    lea rcx, [rsp+0xA0]
    lea r8, [rip+rc41b_dll_name]
    call rc41b_append_name

    # Build absolute bootstrap-state path beside the proxy runtime.
    mov rcx, [rsp+0x40]
    lea rdx, [rsp+0x2B0]
    mov r8d, 260
    mov r10, [rsp+0x40]
    call qword ptr [r10+0x49EE0]      # GetModuleFileNameW
    test eax, eax
    je rc41b_worker_terminal_no_path
    mov edx, eax
    lea rcx, [rsp+0x2B0]
    lea r8, [rip+rc41b_state_name]
    call rc41b_append_name

    mov dword ptr [rsp+0x58], 1       # attempt
    mov dword ptr [rsp+0x6C], 1       # stage=ENTER/PATHS_READY
    mov dword ptr [rsp+0x70], 1       # attempt in record
    mov rcx, [rsp+0x40]
    lea rdx, [rsp+0x2B0]
    lea r8, [rsp+0x60]
    call rc41b_write_state

rc41b_load_loop:
    mov eax, [rsp+0x58]
    mov dword ptr [rsp+0x70], eax
    mov dword ptr [rsp+0x74], 0       # last error

    lea rcx, [rsp+0xA0]
    mov r10, [rsp+0x40]
    call qword ptr [r10+0x49F10]      # LoadLibraryW absolute
    test rax, rax
    jne rc41b_load_ok

    mov r10, [rsp+0x40]
    call qword ptr [r10+0x49ED0]      # GetLastError
    mov dword ptr [rsp+0x74], eax
    inc dword ptr [rsp+0x7C]          # record load_fail_count
    mov dword ptr [rsp+0x6C], 2       # stage=LOADLIB_FAIL
    mov rcx, [rsp+0x40]
    lea rdx, [rsp+0x2B0]
    lea r8, [rsp+0x60]
    call rc41b_write_state

    cmp dword ptr [rsp+0x58], 40
    jae rc41b_worker_exhausted
    mov ecx, 250
    mov r10, [rsp+0x40]
    call qword ptr [r10+0x49F50]      # Sleep
    inc dword ptr [rsp+0x58]
    jmp rc41b_load_loop

rc41b_load_ok:
    mov [rsp+0x48], rax
    mov qword ptr [rsp+0x80], rax     # record module
    mov dword ptr [rsp+0x6C], 3       # stage=LOADLIB_OK
    mov rcx, [rsp+0x40]
    lea rdx, [rsp+0x2B0]
    lea r8, [rsp+0x60]
    call rc41b_write_state

    mov rcx, [rsp+0x48]
    lea rdx, [rip+rc41b_auto_name]
    mov r10, [rsp+0x40]
    call qword ptr [r10+0x49F00]      # GetProcAddress
    test rax, rax
    jne rc41b_proc_ok

    mov r10, [rsp+0x40]
    call qword ptr [r10+0x49ED0]      # GetLastError
    mov dword ptr [rsp+0x74], eax
    mov dword ptr [rsp+0x6C], 4       # stage=GETPROC_FAIL
    mov rcx, [rsp+0x40]
    lea rdx, [rsp+0x2B0]
    lea r8, [rsp+0x60]
    call rc41b_write_state
    jmp rc41b_worker_terminal_reset

rc41b_proc_ok:
    mov [rsp+0x50], rax
    mov qword ptr [rsp+0x88], rax     # record proc
    mov dword ptr [rsp+0x6C], 5       # stage=GETPROC_OK
    mov rcx, [rsp+0x40]
    lea rdx, [rsp+0x2B0]
    lea r8, [rsp+0x60]
    call rc41b_write_state

    mov dword ptr [rsp+0x58], 1
rc41b_autostart_loop:
    mov eax, [rsp+0x58]
    mov dword ptr [rsp+0x70], eax
    mov rcx, [rsp+0x40]
    call qword ptr [rsp+0x50]
    mov dword ptr [rsp+0x78], eax     # record autostart rc
    cmp eax, 0
    je rc41b_worker_active
    cmp eax, 1
    je rc41b_worker_active

    inc dword ptr [rsp+0x98]          # record autostart_retry_count
    mov dword ptr [rsp+0x6C], 6       # stage=AUTOSTART_RETRY
    mov rcx, [rsp+0x40]
    lea rdx, [rsp+0x2B0]
    lea r8, [rsp+0x60]
    call rc41b_write_state

    cmp dword ptr [rsp+0x58], 40
    jae rc41b_worker_exhausted
    mov ecx, 250
    mov r10, [rsp+0x40]
    call qword ptr [r10+0x49F50]      # Sleep
    inc dword ptr [rsp+0x58]
    jmp rc41b_autostart_loop

rc41b_worker_active:
    mov dword ptr [rsp+0x6C], 7       # stage=ACTIVE
    mov rcx, [rsp+0x40]
    lea rdx, [rsp+0x2B0]
    lea r8, [rsp+0x60]
    call rc41b_write_state
    mov byte ptr [rip+rc41b_flag], 2
    xor eax, eax
    add rsp, 0x568
    ret

rc41b_worker_exhausted:
    mov dword ptr [rsp+0x6C], 8       # stage=EXHAUSTED
    mov rcx, [rsp+0x40]
    lea rdx, [rsp+0x2B0]
    lea r8, [rsp+0x60]
    call rc41b_write_state
rc41b_worker_terminal_reset:
    mov byte ptr [rip+rc41b_flag], 0
    xor eax, eax
    add rsp, 0x568
    ret

rc41b_worker_terminal_no_path:
    mov byte ptr [rip+rc41b_flag], 0
    xor eax, eax
    add rsp, 0x568
    ret

.align 2
rc41b_flag:
    .byte 0
    .byte 0

rc41b_dll_name:
    .short 'p','t','a','r','_','r','c','4','1','.','d','l','l',0
rc41b_state_name:
    .short 'p','t','a','r','_','r','c','4','1','_','b','o','o','t','s','t','r','a','p','.','b','i','n',0
rc41b_auto_name:
    .asciz "PTAR_RC41_AutoStart"
