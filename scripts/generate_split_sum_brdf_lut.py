#!/usr/bin/env python3
"""Generate a split-sum GGX BRDF integration LUT as an R16G16_SFLOAT KTX2.

Usage:
    python3 scripts/generate_split_sum_brdf_lut.py [--output PATH]
        [--size 256] [--samples 256]

The horizontal axis stores N dot V, the vertical axis stores perceptual
roughness, and the two channels store the split-sum scale and bias.
"""

import argparse
import math
import struct
from pathlib import Path


KTX2_IDENTIFIER = b"\xabKTX 20\xbb\r\n\x1a\n"
VK_FORMAT_R16G16_SFLOAT = 83


def radical_inverse_vdc(bits: int) -> float:
    """Return the base-2 radical inverse of an unsigned 32-bit integer."""
    bits = ((bits << 16) | (bits >> 16)) & 0xFFFFFFFF
    bits = (((bits & 0x55555555) << 1) | ((bits & 0xAAAAAAAA) >> 1))
    bits = (((bits & 0x33333333) << 2) | ((bits & 0xCCCCCCCC) >> 2))
    bits = (((bits & 0x0F0F0F0F) << 4) | ((bits & 0xF0F0F0F0) >> 4))
    bits = (((bits & 0x00FF00FF) << 8) | ((bits & 0xFF00FF00) >> 8))
    return bits * 2.3283064365386963e-10


def importance_sample_ggx(xi_x: float, xi_y: float, roughness: float) -> tuple[float, float, float]:
    alpha = roughness * roughness
    alpha_squared = alpha * alpha
    phi = 2.0 * math.pi * xi_x
    cos_theta = math.sqrt((1.0 - xi_y) / (1.0 + (alpha_squared - 1.0) * xi_y))
    sin_theta = math.sqrt(max(0.0, 1.0 - cos_theta * cos_theta))
    return math.cos(phi) * sin_theta, math.sin(phi) * sin_theta, cos_theta


def integrate_brdf(no_v: float, roughness: float, samples: int,
                   half_vectors: list[tuple[float, float, float]]) -> tuple[float, float]:
    view_x = math.sqrt(max(0.0, 1.0 - no_v * no_v))
    geometry_k = roughness * roughness * 0.5
    geometry_view = no_v / (no_v * (1.0 - geometry_k) + geometry_k)
    scale = 0.0
    bias = 0.0

    for half_x, _half_y, no_h in half_vectors:
        vo_h = max(view_x * half_x + no_v * no_h, 0.0)
        light_z = 2.0 * vo_h * no_h - no_v
        no_l = max(light_z, 0.0)
        if no_l <= 0.0 or vo_h <= 0.0:
            continue

        geometry_light = no_l / (no_l * (1.0 - geometry_k) + geometry_k)
        geometry_visibility = (
            geometry_view * geometry_light * vo_h / (no_h * no_v)
        )
        fresnel = (1.0 - vo_h) ** 5
        scale += (1.0 - fresnel) * geometry_visibility
        bias += fresnel * geometry_visibility

    inverse_samples = 1.0 / samples
    return scale * inverse_samples, bias * inverse_samples


def make_dfd() -> bytes:
    """Build the KTX2 Data Format Descriptor for linear R16G16_SFLOAT."""
    sample_count = 2
    descriptor_block_size = 24 + 16 * sample_count
    dfd = bytearray(struct.pack(
        "<IHHHH4B4B8B",
        4 + descriptor_block_size,
        0,  # Khronos vendor id
        0,  # Basic descriptor type
        2,  # Descriptor version
        descriptor_block_size,
        1,  # RGBSDA color model
        1,  # BT.709 primaries
        1,  # Linear transfer function
        0,  # Flags
        0, 0, 0, 0,  # 1x1x1 texel block dimensions
        4, 0, 0, 0, 0, 0, 0, 0,  # Bytes per plane
    ))

    for channel, bit_offset in ((0, 0), (1, 16)):
        dfd.extend(struct.pack(
            "<HBB4BII",
            bit_offset,
            15,  # 16-bit sample, expressed as bit length minus one
            0x80 | channel,  # Floating-point R then G channel
            0, 0, 0, 0,  # Sample position
            0x00000000,  # Sample lower bound: 0.0
            0x3F800000,  # Sample upper bound: 1.0
        ))
    return bytes(dfd)


def write_ktx2(path: Path, size: int, pixels: bytes) -> None:
    dfd = make_dfd()
    header_size = 80
    level_index_size = 24
    dfd_offset = header_size + level_index_size
    level_data_offset = dfd_offset + len(dfd)

    header = struct.pack(
        "<12s13I2Q",
        KTX2_IDENTIFIER,
        VK_FORMAT_R16G16_SFLOAT,
        2,  # Type size
        size,
        size,
        0,  # Pixel depth
        0,  # Layer count (0 means non-array)
        1,  # Face count
        1,  # Mipmap level count
        0,  # No supercompression
        dfd_offset,
        len(dfd),
        0,  # No key/value data
        0,
        0,  # No supercompression global data
        0,
    )
    level_index = struct.pack(
        "<3Q",
        level_data_offset,
        len(pixels),
        len(pixels),
    )

    path.write_bytes(header + level_index + dfd + pixels)


def generate_lut(size: int, samples: int) -> bytes:
    pixels = bytearray()
    for y in range(size):
        roughness = (y + 0.5) / size
        half_vectors = [
            importance_sample_ggx(
                i / samples,
                radical_inverse_vdc(i),
                roughness,
            )
            for i in range(samples)
        ]

        for x in range(size):
            no_v = (x + 0.5) / size
            scale, bias = integrate_brdf(no_v, roughness, samples, half_vectors)
            pixels.extend(struct.pack("<ee", scale, bias))
    return bytes(pixels)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--output",
        type=Path,
        default=Path("split_sum_brdf_lut.ktx2"),
        help="output KTX2 path (default: split_sum_brdf_lut.ktx2)",
    )
    parser.add_argument("--size", type=int, default=256, help="LUT width and height")
    parser.add_argument("--samples", type=int, default=256, help="integration samples per texel")
    args = parser.parse_args()

    if args.size < 1:
        parser.error("--size must be positive")
    if args.samples < 1:
        parser.error("--samples must be positive")

    write_ktx2(args.output, args.size, generate_lut(args.size, args.samples))
    print(f"Wrote {args.size}x{args.size} R16G16_SFLOAT KTX2 LUT to {args.output}")


if __name__ == "__main__":
    main()
