// SPDX-FileCopyrightText: Copyright 2024-2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later
// Unmodified embedded VS id 0 code and metadata from shadPS4 gnmdriver.cpp.
// Source: https://github.com/shadps4-emu/shadPS4/blob/main/src/core/libraries/gnmdriver/gnmdriver.cpp
// Source SHA256: 7ab0d9d2a6179922dd0c0b84bbd4df06a32b3802beb2faab449804776f4bce55
#pragma once
#include <array>
namespace maxps4::gpu::gnm_embedded {
inline constexpr std::array FullscreenVS = {
        0xbeeb03ffu, 0x00000007u, // s_mov_b32     vcc_hi, $0x00000007
        0x36020081u,              // v_and_b32     v1, 1, v0
        0x34020281u,              // v_lshlrev_b32 v1, 1, v1
        0x360000c2u,              // v_and_b32     v0, -2, v0
        0x4a0202c1u,              // v_add_i32     v1, vcc, -1, v1
        0x4a0000c1u,              // v_add_i32     v0, vcc, -1, v0
        0x7e020b01u,              // v_cvt_f32_i32 v1, v1
        0x7e000b00U,              // v_cvt_f32_i32 v0, v0
        0x7e040280u,              // v_mov_b32     v2, 0
        0x7e0602f2u,              // v_mov_b32     v3, 1.0
        0xf80008cfu, 0x03020001u, // exp           pos0, v1, v0, v2, v3 done
        0xf800020fu, 0x03030303u, // exp           param0, v3, v3, v3, v3
        0xbf810000u,              // s_endpgm

        // Binary header
        0x5362724fu, 0x07726468u, 0x00004047u, 0u, 0x47f8c29fu, 0x9b2da5cfu, 0xff7c5b7du, 0x00000017u,
        // VS regs
        0x0fe000f1u, 0u, 0x000c0000u, 4u, 0u, 4u, 0u, 7u,
    };
}
