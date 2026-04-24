#pragma once

// Platform flush-to-zero setup for the audio thread, and an explicit subnormal
// killer for feedback paths on platforms where FTZ cannot be set (WASM).
//
// set_ftz()      — call once per audio thread (thread_local guard in caller).
//                  Sets FTZ+DAZ on x86, FZ on ARM, NI on PowerPC.
//                  No-op on WASM (no FPU control available).
//
// kill_denorm(x) — no-op on FTZ platforms (hardware handles it after set_ftz).
//                  On WASM: returns 0 for values whose square underflows 1e-30
//                  (safely below any audible signal, well above subnormal range).

#if defined(__i386__) || defined(__x86_64__) || defined(_M_IX86) || defined(_M_X64)
#  include <xmmintrin.h>
#  include <pmmintrin.h>
#elif defined(__aarch64__) || defined(__arm__)
#  include <cstdint>
#endif

namespace nb::dsp {

inline void set_ftz()
{
#if defined(__EMSCRIPTEN__)
    // No FPU mode control in WebAssembly.
    // V8 and SpiderMonkey permit (and usually apply) subnormal flushing as
    // a non-determinism allowance in the WASM spec, but it is not guaranteed.
    // kill_denorm() guards the worst feedback paths instead.

#elif defined(__i386__) || defined(__x86_64__) || defined(_M_IX86) || defined(_M_X64)
    _MM_SET_FLUSH_ZERO_MODE(_MM_FLUSH_ZERO_ON);       // flush outputs to zero
    _MM_SET_DENORMALS_ZERO_MODE(_MM_DENORMALS_ZERO_ON); // treat subnormal inputs as zero

#elif defined(__aarch64__)
    uint64_t fpcr;
    asm volatile("mrs %0, fpcr" : "=r"(fpcr));
    fpcr |= (1ULL << 24); // FZ bit — flush-to-zero (output side)
    asm volatile("msr fpcr, %0" : : "r"(fpcr) : "memory");

#elif defined(__arm__)
    uint32_t fpscr;
    asm volatile("vmrs %0, fpscr" : "=r"(fpscr));
    fpscr |= (1u << 24); // FZ bit
    asm volatile("vmsr fpscr, %0" : : "r"(fpscr) : "memory");

#elif defined(__powerpc__) || defined(__ppc__) || defined(__PPC__)
    // FPSCR bit 29 (MSB numbering) = NI (Non-IEEE mode): flush denormals to zero.
    asm volatile("mtfsb1 29");

#endif
}

inline float kill_denorm(float x)
{
#if defined(__EMSCRIPTEN__)
    // x*x underflows to 0 for any |x| below ~1e-15 (inaudible, -300 dBFS).
    // Subnormals (|x| < 1.18e-38) square to values far smaller than 1e-30.
    return x * x < 1e-30f ? 0.f : x;
#else
    return x; // FTZ handles it after set_ftz() on the first callback invocation.
#endif
}

} // namespace nb::dsp
