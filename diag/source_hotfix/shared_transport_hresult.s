.text
.intel_syntax noprefix
.globl shared_transport_hresult_gate
.extern LAST_STATUS
.extern SUCCESS_TARGET
.extern FAILURE_TARGET

# SHAREHR1 diagnostic gate.
# Entered immediately after FGCreateSharedTransport() returned in EAX.
# RC7/SHAREDIAG1 already proved the deterministic failure is slot 0 / stage 1
# (producer CreateTexture2D). Preserve success semantics exactly. On failure,
# expose the raw 32-bit HRESULT through the existing P1FG7N LAST STATUS field.
shared_transport_hresult_gate:
    test eax, eax
    jns SUCCESS_TARGET
    mov dword ptr [rip + LAST_STATUS], eax
    jmp FAILURE_TARGET
