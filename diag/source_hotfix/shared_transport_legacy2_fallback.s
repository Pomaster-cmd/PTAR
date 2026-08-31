.text
.intel_syntax noprefix
.globl shared_transport_legacy_gate
.globl shared_primary_call_wrapper
.globl shared_desc_misc_helper
.globl shared_mutex_qi_helper
.globl mailbox_call_wrapper
.globl mailbox_desc_misc_helper
.globl mailbox_producer_mutex_qi_helper
.globl mailbox_display_mutex_qi_helper
.extern SUCCESS_TARGET
.extern FAILURE_TARGET
.extern LAST_STATUS
.extern SHARED_FUNC
.extern MAILBOX_FUNC
.extern SHARED_FLUSH_CONFIG
.extern PRODUCER_TEX_ARRAY
.extern PRODUCER_MUTEX_ARRAY
.extern CONSUMER_TEX_ARRAY
.extern CONSUMER_MUTEX_ARRAY
.extern CONSUMER_SRV_ARRAY
.extern SHARED_HANDLE_ARRAY
.extern FALLBACK_FLAG
.extern FAKE_VTABLE
.extern FAKE_OBJECT

# SHARELEGACY2
# Primary keyed-mutex sharing remains the exact first path. Only the observed
# E_INVALIDARG + empty-transport + SharedFlush=1 shape retries with legacy
# D3D11_RESOURCE_MISC_SHARED. Unlike rejected RC10, the fallback mode remains
# coherent through BOTH the game<->worker transport and display-mailbox stage.

# Existing RC9 gate jump targets .fgdia RVA, so this function must start at 0.
shared_transport_legacy_gate:
    test eax, eax
    jns .primary_success
    cmp eax, 0x80070057                 # E_INVALIDARG only
    jne .raw_failure
    cmp dword ptr [rip + SHARED_FLUSH_CONFIG], 0
    je .raw_failure

    # No retry if the failed primary path populated any transport object.
    mov r10, qword ptr [rip + PRODUCER_TEX_ARRAY]
    or  r10, qword ptr [rip + PRODUCER_MUTEX_ARRAY]
    or  r10, qword ptr [rip + CONSUMER_TEX_ARRAY]
    or  r10, qword ptr [rip + CONSUMER_MUTEX_ARRAY]
    or  r10, qword ptr [rip + CONSUMER_SRV_ARRAY]
    or  r10, qword ptr [rip + SHARED_HANDLE_ARRAY]
    jne .raw_failure

    # Minimal inert IDXGIKeyedMutex-compatible object. The validated runtime
    # touches Release (IUnknown slot 2), AcquireSync (8) and ReleaseSync (9).
    # These become non-owning/no-op in legacy SHARED mode; explicit existing
    # D3D11 Flush boundaries remain enabled by contract.
    lea r10, [rip + FAKE_VTABLE]
    lea rax, [rip + fake_release]
    mov qword ptr [r10 + 16], rax
    lea rax, [rip + fake_sync_ok]
    mov qword ptr [r10 + 64], rax
    mov qword ptr [r10 + 72], rax
    mov qword ptr [rip + FAKE_OBJECT], r10

    mov dword ptr [rip + FALLBACK_FLAG], 1
    mov dword ptr [rip + LAST_STATUS], 0
    sub rsp, 0x20
    mov rcx, rsi
    call SHARED_FUNC
    add rsp, 0x20
    test eax, eax
    js .retry_failure

    # Keep legacy mode active through the later display-mailbox constructor.
    # mailbox_call_wrapper clears it on both mailbox success and failure.
    mov dword ptr [rip + LAST_STATUS], 0
    jmp SUCCESS_TARGET

.primary_success:
    mov dword ptr [rip + FALLBACK_FLAG], 0
    jmp SUCCESS_TARGET
.retry_failure:
    mov dword ptr [rip + FALLBACK_FLAG], 0
    mov dword ptr [rip + LAST_STATUS], eax
    jmp FAILURE_TARGET
.raw_failure:
    mov dword ptr [rip + FALLBACK_FLAG], 0
    mov dword ptr [rip + LAST_STATUS], eax
    jmp FAILURE_TARGET

# Every normal transport attempt is forced back to keyed-primary before the
# original constructor is called. This makes a stale legacy flag fail-safe.
shared_primary_call_wrapper:
    mov dword ptr [rip + FALLBACK_FLAG], 0
    sub rsp, 0x28
    call SHARED_FUNC
    add rsp, 0x28
    ret

# Replaces original transport descriptor CPUAccess+Misc qword. Helper entry
# RSP is caller_RSP-8: caller [rsp+0x64] => helper [rsp+0x6c].
shared_desc_misc_helper:
    mov dword ptr [rsp + 0x6c], 0
    mov eax, 0x100
    cmp dword ptr [rip + FALLBACK_FLAG], 0
    je .sd_store
    mov eax, 2
.sd_store:
    mov dword ptr [rsp + 0x70], eax
    ret

# Initial game<->worker transport mutex QI. Primary path performs the original
# QueryInterface; legacy writes the inert object into *r12.
shared_mutex_qi_helper:
    cmp dword ptr [rip + FALLBACK_FLAG], 0
    jne .sm_fake
    mov rdx, rbp
    mov r8, r12
    jmp .qi_common
.sm_fake:
    lea rax, [rip + FAKE_OBJECT]
    mov qword ptr [r12], rax
    xor eax, eax
    ret

# Wrap the single display-mailbox constructor. Clearing the legacy flag here
# is atomic with respect to construction outcome and restores keyed-primary
# semantics for the next activation/rebuild.
mailbox_call_wrapper:
    sub rsp, 0x28
    call MAILBOX_FUNC
    add rsp, 0x28
    mov dword ptr [rip + FALLBACK_FLAG], 0
    ret

# Mailbox descriptor writes MiscFlags only. Helper entry has RSP lowered by 8:
# caller [rsp+0x74] => helper [rsp+0x7c].
mailbox_desc_misc_helper:
    mov eax, 0x100
    cmp dword ptr [rip + FALLBACK_FLAG], 0
    je .md_store
    mov eax, 2
.md_store:
    mov dword ptr [rsp + 0x7c], eax
    ret

# Publisher-side mailbox mutex QI. Primary path is original; legacy stores the
# inert mutex in *r13.
mailbox_producer_mutex_qi_helper:
    cmp dword ptr [rip + FALLBACK_FLAG], 0
    jne .mp_fake
    mov rdx, r12
    mov r8, r13
    jmp .qi_common
.mp_fake:
    lea rax, [rip + FAKE_OBJECT]
    mov qword ptr [r13], rax
    xor eax, eax
    ret

# Isolated-display-side mailbox mutex QI.
mailbox_display_mutex_qi_helper:
    cmp dword ptr [rip + FALLBACK_FLAG], 0
    jne .md_fake
    mov rdx, rbx
    mov r8, r13
    jmp .qi_common
.md_fake:
    lea rax, [rip + FAKE_OBJECT]
    mov qword ptr [r13], rax
    xor eax, eax
    ret

# Primary QI helpers arrive with rcx=texture and rdx/r8 prepared.
.qi_common:
    mov rax, qword ptr [rcx]
    sub rsp, 0x28
    call qword ptr [rax]
    add rsp, 0x28
    ret

fake_release:
    mov eax, 1
    ret
fake_sync_ok:
    xor eax, eax
    ret
