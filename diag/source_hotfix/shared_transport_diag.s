.text
.intel_syntax noprefix
.globl shared_transport_diag_gate
.extern TEX_BASE
.extern HANDLE_BASE
.extern PROD_MUTEX_BASE
.extern CONS_TEX_BASE
.extern CONS_MUTEX_BASE
.extern SRV_BASE
.extern LAST_STATUS
.extern SUCCESS_TARGET
.extern FAILURE_TARGET

# Entered immediately after FGCreateSharedTransport() returned in EAX.
# Success path: reproduces original TEST/JNS semantics and changes nothing.
# Failure path only: inspect the six persistent arrays for the first incomplete
# slot and encode STATUS = 0xF100 + slot*0x10 + stage.
# stage 1 = producer texture
# stage 2 = IDXGIResource/shared handle
# stage 3 = producer keyed mutex
# stage 4 = game-device OpenSharedResource
# stage 5 = consumer keyed mutex
# stage 6 = consumer SRV
# 0xF1F0 = function failed after all four slot structures look complete.
shared_transport_diag_gate:
    test eax, eax
    jns SUCCESS_TARGET

    xor r8d, r8d
.Lslot:
    lea r10, [rip + TEX_BASE]
    cmp qword ptr [r10 + r8*8], 0
    je .Lstage1

    lea r10, [rip + HANDLE_BASE]
    cmp qword ptr [r10 + r8*8], 0
    je .Lstage2

    lea r10, [rip + PROD_MUTEX_BASE]
    cmp qword ptr [r10 + r8*8], 0
    je .Lstage3

    lea r10, [rip + CONS_TEX_BASE]
    cmp qword ptr [r10 + r8*8], 0
    je .Lstage4

    lea r10, [rip + CONS_MUTEX_BASE]
    cmp qword ptr [r10 + r8*8], 0
    je .Lstage5

    lea r10, [rip + SRV_BASE]
    cmp qword ptr [r10 + r8*8], 0
    je .Lstage6

    inc r8d
    cmp r8d, 4
    jb .Lslot

    mov edx, 0xF1F0
    jmp .Lstore

.Lstage1:
    mov ecx, 1
    jmp .Lencode
.Lstage2:
    mov ecx, 2
    jmp .Lencode
.Lstage3:
    mov ecx, 3
    jmp .Lencode
.Lstage4:
    mov ecx, 4
    jmp .Lencode
.Lstage5:
    mov ecx, 5
    jmp .Lencode
.Lstage6:
    mov ecx, 6

.Lencode:
    mov edx, r8d
    shl edx, 4
    add edx, 0xF100
    add edx, ecx

.Lstore:
    mov dword ptr [rip + LAST_STATUS], edx
    jmp FAILURE_TARGET
