.text
.intel_syntax noprefix
.globl shared_transport_probe_gate
.extern LAST_STATUS
.extern SUCCESS_TARGET
.extern FAILURE_TARGET
.extern GAME_DEVICE

# SHAREPROBE1 diagnostic gate.
# Entered immediately after FGCreateSharedTransport() returned in EAX.
# Success semantics are untouched. Non-E_INVALIDARG failures keep their raw HRESULT.
# For E_INVALIDARG, probe seven temporary Texture2D descriptor variants on the same
# game device, release every successful temporary resource immediately, and encode
# successful variants as 0xE3000000 | bitmask in P1FG7N LAST STATUS.
# RSI is the caller-preserved source D3D11_TEXTURE2D_DESC pointer.
shared_transport_probe_gate:
    test eax, eax
    jns SUCCESS_TARGET
    cmp eax, 0x80070057
    je .probe
    mov dword ptr [rip + LAST_STATUS], eax
    jmp FAILURE_TARGET

.probe:
    sub rsp, 0x70

    # Normalized shared transport descriptor at [rsp+0x20].
    mov eax, dword ptr [rsi+0x00]
    mov dword ptr [rsp+0x20], eax       # Width
    mov eax, dword ptr [rsi+0x04]
    mov dword ptr [rsp+0x24], eax       # Height
    mov dword ptr [rsp+0x28], 1         # MipLevels
    mov dword ptr [rsp+0x2c], 1         # ArraySize
    mov eax, dword ptr [rsi+0x10]
    mov dword ptr [rsp+0x30], eax       # Format
    mov dword ptr [rsp+0x34], 1         # SampleDesc.Count
    mov dword ptr [rsp+0x38], 0         # SampleDesc.Quality
    mov dword ptr [rsp+0x3c], 0         # Usage DEFAULT
    mov dword ptr [rsp+0x44], 0         # CPUAccessFlags

    lea rax, [rip + probe_table]
    mov qword ptr [rsp+0x58], rax
    mov dword ptr [rsp+0x60], 7
    mov dword ptr [rsp+0x64], 0

.loop:
    mov r10, qword ptr [rsp+0x58]
    mov eax, dword ptr [r10+0]
    mov dword ptr [rsp+0x40], eax       # BindFlags
    mov eax, dword ptr [r10+4]
    mov dword ptr [rsp+0x48], eax       # MiscFlags
    mov eax, dword ptr [r10+8]
    mov dword ptr [rsp+0x68], eax       # success bit
    add r10, 12
    mov qword ptr [rsp+0x58], r10
    mov qword ptr [rsp+0x50], 0

    mov rcx, qword ptr [rip + GAME_DEVICE]
    test rcx, rcx
    jz .next
    mov rax, qword ptr [rcx]
    lea rdx, [rsp+0x20]
    xor r8d, r8d
    lea r9, [rsp+0x50]
    call qword ptr [rax+0x28]           # ID3D11Device::CreateTexture2D
    test eax, eax
    js .release_if_any
    cmp qword ptr [rsp+0x50], 0
    je .release_if_any
    mov eax, dword ptr [rsp+0x64]
    or eax, dword ptr [rsp+0x68]
    mov dword ptr [rsp+0x64], eax

.release_if_any:
    mov rcx, qword ptr [rsp+0x50]
    test rcx, rcx
    jz .next
    mov rax, qword ptr [rcx]
    call qword ptr [rax+0x10]           # IUnknown::Release

.next:
    sub dword ptr [rsp+0x60], 1
    jne .loop

    mov eax, dword ptr [rsp+0x64]
    or eax, 0xE3000000
    mov dword ptr [rip + LAST_STATUS], eax
    add rsp, 0x70
    jmp FAILURE_TARGET

.align 4
probe_table:
    # bit 0: ordinary texture, no sharing, SRV|RTV
    .long 0x28, 0x000, 0x01
    # bit 1: legacy SHARED, SRV|RTV
    .long 0x28, 0x002, 0x02
    # bit 2: original SHARED_KEYEDMUTEX, SRV|RTV
    .long 0x28, 0x100, 0x04
    # bit 3: SHARED_KEYEDMUTEX, SRV only
    .long 0x08, 0x100, 0x08
    # bit 4: SHARED_KEYEDMUTEX, RTV only
    .long 0x20, 0x100, 0x10
    # bit 5: SHARED_NTHANDLE, SRV|RTV
    .long 0x28, 0x800, 0x20
    # bit 6: SHARED_NTHANDLE|SHARED_KEYEDMUTEX, SRV|RTV
    .long 0x28, 0x900, 0x40
