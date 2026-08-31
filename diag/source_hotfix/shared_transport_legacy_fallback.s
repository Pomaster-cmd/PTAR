.text
.intel_syntax noprefix
.globl shared_transport_legacy_gate
.globl shared_desc_misc_helper
.globl shared_mutex_qi_helper
.extern SUCCESS_TARGET
.extern FAILURE_TARGET
.extern LAST_STATUS
.extern SHARED_FUNC
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

# SHARELEGACY1
# Primary path is semantically the original keyed-mutex transport.
# Only the exact Inquisitor failure shape (E_INVALIDARG at the first shared
# texture, no transport objects populated, SharedFlush enabled) gets one
# retry using legacy D3D11_RESOURCE_MISC_SHARED.
# The existing producer/consumer slot-state machine is preserved. The
# runtime's already-enabled cross-device Flush calls remain authoritative.

# Existing caller jumps here immediately after FGCreateSharedTransport returns.
shared_transport_legacy_gate:
    test eax, eax
    jns .primary_success
    cmp eax, 0x80070057                 # E_INVALIDARG only
    jne .raw_failure

    # Legacy sharing requires the existing SharedFlush contract.
    cmp dword ptr [rip + SHARED_FLUSH_CONFIG], 0
    je .raw_failure

    # Bound the retry to the observed slot-0/step-1 failure. If the primary
    # path populated anything, do not reinterpret the failure.
    cmp qword ptr [rip + PRODUCER_TEX_ARRAY], 0
    jne .raw_failure
    cmp qword ptr [rip + PRODUCER_MUTEX_ARRAY], 0
    jne .raw_failure
    cmp qword ptr [rip + CONSUMER_TEX_ARRAY], 0
    jne .raw_failure
    cmp qword ptr [rip + CONSUMER_MUTEX_ARRAY], 0
    jne .raw_failure
    cmp qword ptr [rip + CONSUMER_SRV_ARRAY], 0
    jne .raw_failure
    cmp qword ptr [rip + SHARED_HANDLE_ARRAY], 0
    jne .raw_failure

    # Build an ASLR-safe inert IDXGIKeyedMutex-compatible object in .fgdat.
    # Slots not used by the runtime return E_NOTIMPL; Release is a no-op.
    lea r10, [rip + FAKE_VTABLE]
    lea rax, [rip + fake_notimpl]
    mov ecx, 10
.init_vtbl:
    mov qword ptr [r10], rax
    add r10, 8
    dec ecx
    jne .init_vtbl

    lea rax, [rip + fake_qi]
    mov qword ptr [rip + FAKE_VTABLE], rax
    lea rax, [rip + fake_addref]
    mov qword ptr [rip + FAKE_VTABLE + 8], rax
    lea rax, [rip + fake_release]
    mov qword ptr [rip + FAKE_VTABLE + 16], rax
    lea rax, [rip + fake_sync_ok]
    mov qword ptr [rip + FAKE_VTABLE + 64], rax   # AcquireSync
    mov qword ptr [rip + FAKE_VTABLE + 72], rax   # ReleaseSync
    lea rax, [rip + FAKE_VTABLE]
    mov qword ptr [rip + FAKE_OBJECT], rax

    mov dword ptr [rip + FALLBACK_FLAG], 1
    mov dword ptr [rip + LAST_STATUS], 0

    # Retry the original complete transport constructor. Its remainder stays
    # intact; only descriptor MiscFlags and the two keyed-mutex QI sites are
    # conditionalized while FALLBACK_FLAG is set.
    sub rsp, 0x20
    mov rcx, rsi
    call SHARED_FUNC
    add rsp, 0x20

    mov dword ptr [rip + FALLBACK_FLAG], 0
    test eax, eax
    js .retry_failure

    mov dword ptr [rip + LAST_STATUS], 0
    jmp SUCCESS_TARGET

.primary_success:
    mov dword ptr [rip + FALLBACK_FLAG], 0
    jmp SUCCESS_TARGET

.retry_failure:
    mov dword ptr [rip + LAST_STATUS], eax
    jmp FAILURE_TARGET

.raw_failure:
    mov dword ptr [rip + FALLBACK_FLAG], 0
    mov dword ptr [rip + LAST_STATUS], eax
    jmp FAILURE_TARGET

# Replaces only the 15-byte descriptor MiscFlags materialization in the
# original constructor. Helper entry RSP is caller_RSP-8, so [rsp+0x6c]
# is the original descriptor qword at caller [rsp+0x64].
shared_desc_misc_helper:
    cmp dword ptr [rip + FALLBACK_FLAG], 0
    jne .legacy_desc
    movabs rax, 0x0000010000000000     # CPUAccess=0, Misc=SHARED_KEYEDMUTEX
    mov qword ptr [rsp+0x6c], rax
    ret
.legacy_desc:
    movabs rax, 0x0000000200000000     # CPUAccess=0, Misc=SHARED
    mov qword ptr [rsp+0x6c], rax
    ret

# Replaces the two 8-byte "mov rdx,rbp / mov r8,r12 / call [rax]" QI
# sequences. Primary mode performs the exact same QueryInterface call.
# Legacy mode supplies the inert mutex object so the already-validated runtime
# state machine and its existing Flush boundaries require no broad rewrite.
shared_mutex_qi_helper:
    cmp dword ptr [rip + FALLBACK_FLAG], 0
    jne .fake_mutex
    mov rdx, rbp
    mov r8, r12
    mov rax, qword ptr [rcx]
    sub rsp, 0x28
    call qword ptr [rax]
    add rsp, 0x28
    ret
.fake_mutex:
    lea rax, [rip + FAKE_OBJECT]
    mov qword ptr [r12], rax
    xor eax, eax
    ret

# Minimal COM-compatible vtable targets for the inert fallback mutex.
fake_qi:
    test r8, r8
    je .qi_done
    mov qword ptr [r8], 0
.qi_done:
    mov eax, 0x80004002                 # E_NOINTERFACE
    ret
fake_addref:
    mov eax, 1
    ret
fake_release:
    mov eax, 1
    ret
fake_notimpl:
    mov eax, 0x80004001                 # E_NOTIMPL
    ret
fake_sync_ok:
    xor eax, eax                        # S_OK
    ret
