
// GTA V PS5 - PPSA03420 v01.011.001
// real = 0x400000 + RVA (verificado byte a byte na memoria viva)
// Enderecos derivados por engenharia reversa do eboot decriptado.
// TODOS os RVA são relativos à base do modulo principal (imagebase do ELF = 0).
#pragma once
#include <stdint.h>
#include <stddef.h>

namespace gtav {

// ---- RVAs confirmados ----------------------------------------------------
// rage::scrEngine::RegisterNativeHandler(rdi = hash, rsi = handler)
static constexpr uint64_t RVA_RegisterNativeHandler = 0x0305DFE0;
// NativeRegistration* g_nativeRegistrationTable[256], bucket = hash & 0xFF
static constexpr uint64_t RVA_NativeRegTable        = 0x05D8EBC0;
// rage::scrThread::Run(rdi = scrThread*, rsi = opsToExecute) - tick por frame
static constexpr uint64_t RVA_ScrThreadRun          = 0x0305B8E0;
// interpretador de bytecode chamado por Run
static constexpr uint64_t RVA_ScrThreadRunInterp    = 0x0305BA00;
// scrThread* global do thread ativo (espelho do slot TLS)
static constexpr uint64_t RVA_ActiveThreadGlobal    = 0x05D8F3E0;
// ponteiro para o contexto do thread ativo (thread + 0x154)
static constexpr uint64_t RVA_ActiveThreadCtx       = 0x05D8F3F0;

// Slot TLS que guarda o scrThread ativo: fs:[0] - 0x140
static constexpr int64_t  TLS_ACTIVE_THREAD_OFF     = -0x140;

// ---- layout do rage::scrThread ------------------------------------------
struct scrThread {
    void*    vtable;      // +0x00
    uint32_t id;          // +0x08  (GET_ID_OF_THIS_THREAD lê daqui)
    uint32_t _pad0;
    uint32_t scriptHash;  // +0x10  usado para achar o scrProgram
    uint32_t _pad1;
    uint32_t state;       // +0x18  (state & ~1) == 2  => morto
    // ...
};

// ---- rage::scrNativeCallContext -----------------------------------------
// Confirmado: GET_FRAME_COUNT faz "mov rcx,[rdi]; mov [rcx],eax"  -> result em +0x00
//             GET_HASH_KEY   faz "mov rax,[rdi+10h]"              -> args   em +0x10
struct NativeCtx {
    void*    pReturn;     // +0x00
    uint32_t nArgCount;   // +0x08
    uint32_t _pad0;       // +0x0C
    void*    pArgs;       // +0x10
    uint32_t nDataCount;  // +0x18
    uint32_t _pad1;       // +0x1C
    uint64_t retBuf[4];   // +0x20
    uint64_t argBuf[32];  // +0x40
};

typedef void (*NativeHandler)(NativeCtx*);

// ---- índices de controle (extraídos da tabela de nomes deste build) ------
enum Control : int {
    CTRL_NEXT_CAMERA      = 0,
    CTRL_ATTACK           = 24,
    CTRL_AIM              = 25,
    CTRL_COVER            = 44,
    CTRL_VEH_HANDBRAKE    = 76,
    CTRL_FRONTEND_DOWN    = 187,
    CTRL_FRONTEND_UP      = 188,
    CTRL_FRONTEND_LEFT    = 189,
    CTRL_FRONTEND_RIGHT   = 190,
    CTRL_FRONTEND_ACCEPT  = 201,   // Cross
    CTRL_FRONTEND_CANCEL  = 202,   // Circle
    CTRL_FRONTEND_LB      = 205,   // L1
    CTRL_FRONTEND_RB      = 206,   // R1
    CTRL_FRONTEND_LT      = 207,   // L2
    CTRL_FRONTEND_RT      = 208,   // R2
};

} // namespace gtav
