// s_mov_b32 vcc_hi, 13 has a literal word in the guest encoding.
// LLVM canonicalizes this first instruction to an inline constant.
s_mov_b32 vcc_hi, 13
s_mul_i32 vcc_lo, s12, 64
s_buffer_load_dword s12, s[8:11], 0
v_add_i32 v0, vcc, vcc_lo, v0
s_waitcnt lgkmcnt(0)
v_cmpx_gt_u32 vcc, s12, v0
s_cbranch_execz 10
s_buffer_load_dword s8, s[8:11], 1
s_waitcnt lgkmcnt(0)
v_and_b32 v1, s8, v0
buffer_load_format_x v1, v1, s[0:3], 0 idxen
v_cmpx_gt_u32_e64 s[0:1], s6, v0
s_waitcnt vmcnt(0)
buffer_store_format_x v1, v0, s[4:7], 0 idxen
s_endpgm
