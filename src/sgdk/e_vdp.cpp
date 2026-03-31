#include <newbase/sgdk/emu/e_vdp.hpp>

#include <cstring>

using namespace nb;

sgdk_vdp::sgdk_vdp()
    : m_reg_status(0)
    , m_ctrl_pending(false)
    , m_ctrl_first(0)
    , m_addr(0)
    , m_regs(REG_COUNT, 0)
    , m_vram(VRAM_SZ, 0)
    , m_cram(CRAM_SZ, 0)
    , m_vsram(VSRAM_SZ, 0)
{}

sgdk_vdp::~sgdk_vdp() = default;

// ---------------------------------------------------------------------------
// Control port
//
// The MD VDP uses a two-word write protocol on the control port:
//   First word:  CD1 CD0 | A13..A0   (high bits set the access type)
//   Second word: CD5 CD4 CD3 CD2 | 0 0 A15 A14
//
// After both words, m_addr and m_access are set and the command is ready.
// A single 32-bit write delivers both words atomically.
// ---------------------------------------------------------------------------

static constexpr uint32_t CTRL_VRAM_WRITE  = 0x40000000;
static constexpr uint32_t CTRL_CRAM_WRITE  = 0xC0000000;
static constexpr uint32_t CTRL_VSRAM_WRITE = 0x40000010;
static constexpr uint32_t CTRL_VRAM_READ   = 0x00000000;
static constexpr uint32_t CTRL_CRAM_READ   = 0x00000020;
static constexpr uint32_t CTRL_VSRAM_READ  = 0x00000010;
static constexpr uint32_t CTRL_REG_WRITE   = 0x80000000;

void sgdk_vdp::write_ctrl16(uint16_t value)
{
    // Register write: top two bits are 10
    if ((value & 0xC000) == 0x8000 && !m_ctrl_pending) {
        uint8_t reg = (value >> 8) & 0x1F;
        uint8_t val = value & 0xFF;
        if (reg < REG_COUNT)
            m_regs[reg] = val;
        return;
    }

    if (!m_ctrl_pending) {
        // First word of two-word address command
        m_ctrl_first   = value;
        m_ctrl_pending = true;
    } else {
        // Second word — decode full address and access type
        uint32_t cmd = ((uint32_t)m_ctrl_first << 16) | value;
        uint32_t cd  = ((cmd >> 14) & 0x3C) | ((cmd >> 30) & 0x03);  // CD5-CD0
        m_addr        = (cmd & 0x3FFF) | ((cmd >> 16) & 0xC000);
        m_access      = static_cast<uint8_t>(cd);
        m_ctrl_pending = false;
    }
}

void sgdk_vdp::write_ctrl(uint32_t value)
{
    // A 32-bit write delivers two 16-bit words atomically (high word first).
    // A plain 16-bit write has zero in the upper half.
    if (value > 0xFFFF) {
        write_ctrl16(static_cast<uint16_t>(value >> 16));
        write_ctrl16(static_cast<uint16_t>(value & 0xFFFF));
    } else {
        write_ctrl16(static_cast<uint16_t>(value));
    }
}

uint32_t sgdk_vdp::read_ctrl()
{
    return m_reg_status;
}

// ---------------------------------------------------------------------------
// Data port — writes go to VRAM/CRAM/VSRAM at m_addr, auto-increment
// ---------------------------------------------------------------------------

void sgdk_vdp::write_data(uint16_t value)
{
    uint8_t inc = m_regs[15];   // register 15 = auto-increment

    switch (m_access & 0x0F) {
    case 0x01: // VRAM write
        if (m_addr + 1 < VRAM_SZ) {
            m_vram[m_addr]     = (value >> 8) & 0xFF;
            m_vram[m_addr + 1] = value & 0xFF;
        }
        break;
    case 0x03: // CRAM write
        if ((m_addr / 2) < CRAM_SZ)
            m_cram[m_addr / 2] = value;
        break;
    case 0x05: // VSRAM write
        if ((m_addr / 2) < VSRAM_SZ)
            m_vsram[m_addr / 2] = value;
        break;
    default:
        break;
    }

    m_addr = (m_addr + inc) & 0xFFFF;
}

uint16_t sgdk_vdp::read_data()
{
    uint8_t  inc    = m_regs[15];
    uint16_t result = 0;

    switch (m_access & 0x0F) {
    case 0x00: // VRAM read
        if (m_addr + 1 < VRAM_SZ)
            result = (static_cast<uint16_t>(m_vram[m_addr]) << 8) | m_vram[m_addr + 1];
        break;
    case 0x02: // CRAM read
        if ((m_addr / 2) < CRAM_SZ)
            result = m_cram[m_addr / 2];
        break;
    case 0x04: // VSRAM read
        if ((m_addr / 2) < VSRAM_SZ)
            result = m_vsram[m_addr / 2];
        break;
    default:
        break;
    }

    m_addr = (m_addr + inc) & 0xFFFF;
    return result;
}

uint16_t sgdk_vdp::read_hvcounter()
{
    // On real hardware this returns the beam position.
    // Games mostly use it for timing; returning 0 is safe for now.
    return 0;
}

// ---------------------------------------------------------------------------
// Rendering — placeholder; real implementation goes here
// ---------------------------------------------------------------------------

void sgdk_vdp::render_frame(SDL_Surface* /*out*/)
{
    // TODO: scanline renderer — planes A/B/window + sprites → out (RGBA32, 320×224)
}
