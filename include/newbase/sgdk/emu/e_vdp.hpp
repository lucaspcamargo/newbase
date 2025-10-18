#pragma once

#include <vector>
#include <cstdint>

namespace nb {

class sgdk_vdp final {
public:
    sgdk_vdp();
    ~sgdk_vdp();

    // renders a frame
    void render_frame();

    std::vector<uint8_t>& vram() {return m_vram;}
    std::vector<uint16_t>& cram() {return m_cram;}
    std::vector<uint16_t>& vsram() {return m_vsram;}

    // memory sizes
    static constexpr size_t VRAM_SZ = 64*1024; // consider increasing this if widescreen (in the future)
    static constexpr size_t CRAM_SZ = 64; // use u16 for color (9-bit rgb fits nicely)
    static constexpr size_t VSRAM_SZ = 2*25; // internal cram stores r,g,b values separately

    static constexpr size_t TILE_SZ = 32; // tile size in bytes
    static constexpr size_t SPRITE_SZ = 8; // sprite def size in bytes

    static constexpr size_t REG_COUNT = 0x18; // sprite def size in bytes
    
private:
    uint16_t m_reg_status;
    std::vector<uint8_t> m_regs;
    
    std::vector<uint8_t> m_vram;
    std::vector<uint16_t> m_cram;
    std::vector<uint16_t> m_vsram;
};

}