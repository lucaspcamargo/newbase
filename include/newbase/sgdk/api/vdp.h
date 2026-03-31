#pragma once

#include "types.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// ---------------------------------------------------------------------------
// VDP port shims — called by SGDK code after sgdk_translator.py rewrites
// direct MMIO dereferences to these function calls.
// All state lives in the sgdk_vdp owned by the current thread's sgdk_p.
// ---------------------------------------------------------------------------

void     vdp_write_ctrl(uint32_t value);
uint32_t vdp_read_ctrl(void);

void     vdp_write_data(uint16_t value);
uint16_t vdp_read_data(void);

uint16_t vdp_read_hvcounter(void);

// Z80 bus arbitration — stubbed; no Z80 emulation on host
void     z80_write_busreq(uint16_t value);
uint16_t z80_read_busreq(void);
void     z80_write_reset(uint16_t value);

#define GET_VDP_STATUS(flag) (vdp_read_ctrl() & flag)

#include "vdp.patched.h"

#ifdef __cplusplus
} // extern "C"
#endif