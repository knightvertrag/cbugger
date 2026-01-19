#pragma once

// Register formats
// - U32: 32-bit integer
// - U64: 64-bit integer
// - Vec128: 128-bit SIMD/FPR
// (We keep them simple, debugger interprets them later)

#define REGISTER_LIST(GPR, FPR, HWBRK, HWWATCH) \
    /* ---------------- General Purpose Registers ---------------- */ \
    GPR("x0",  gpr, regs[0],  U64, 0) \
    GPR("x1",  gpr, regs[1],  U64, 1) \
    GPR("x2",  gpr, regs[2],  U64, 2) \
    GPR("x3",  gpr, regs[3],  U64, 3) \
    GPR("x4",  gpr, regs[4],  U64, 4) \
    GPR("x5",  gpr, regs[5],  U64, 5) \
    GPR("x6",  gpr, regs[6],  U64, 6) \
    GPR("x7",  gpr, regs[7],  U64, 7) \
    GPR("x8",  gpr, regs[8],  U64, 8) \
    GPR("x9",  gpr, regs[9],  U64, 9) \
    GPR("x10", gpr, regs[10], U64, 10) \
    GPR("x11", gpr, regs[11], U64, 11) \
    GPR("x12", gpr, regs[12], U64, 12) \
    GPR("x13", gpr, regs[13], U64, 13) \
    GPR("x14", gpr, regs[14], U64, 14) \
    GPR("x15", gpr, regs[15], U64, 15) \
    GPR("x16", gpr, regs[16], U64, 16) \
    GPR("x17", gpr, regs[17], U64, 17) \
    GPR("x18", gpr, regs[18], U64, 18) \
    GPR("x19", gpr, regs[19], U64, 19) \
    GPR("x20", gpr, regs[20], U64, 20) \
    GPR("x21", gpr, regs[21], U64, 21) \
    GPR("x22", gpr, regs[22], U64, 22) \
    GPR("x23", gpr, regs[23], U64, 23) \
    GPR("x24", gpr, regs[24], U64, 24) \
    GPR("x25", gpr, regs[25], U64, 25) \
    GPR("x26", gpr, regs[26], U64, 26) \
    GPR("x27", gpr, regs[27], U64, 27) \
    GPR("x28", gpr, regs[28], U64, 28) \
    GPR("x29", gpr, regs[29], U64, 29) \
    GPR("x30", gpr, regs[30], U64, 30) \
    GPR("sp",  gpr, sp,       U64, 31) \
    GPR("pc",  gpr, pc,       U64, 32) \
    GPR("pstate", gpr, pstate, U64, 33) \
    \
    /* ---------------- SIMD/Floating Point Registers ---------------- */ \
    FPR("v0",  fpr, vregs[0],  Vec128, 64) \
    FPR("v1",  fpr, vregs[1],  Vec128, 65) \
    FPR("v2",  fpr, vregs[2],  Vec128, 66) \
    FPR("v3",  fpr, vregs[3],  Vec128, 67) \
    FPR("v4",  fpr, vregs[4],  Vec128, 68) \
    FPR("v5",  fpr, vregs[5],  Vec128, 69) \
    FPR("v6",  fpr, vregs[6],  Vec128, 70) \
    FPR("v7",  fpr, vregs[7],  Vec128, 71) \
    FPR("v8",  fpr, vregs[8],  Vec128, 72) \
    FPR("v9",  fpr, vregs[9],  Vec128, 73) \
    FPR("v10", fpr, vregs[10], Vec128, 74) \
    FPR("v11", fpr, vregs[11], Vec128, 75) \
    FPR("v12", fpr, vregs[12], Vec128, 76) \
    FPR("v13", fpr, vregs[13], Vec128, 77) \
    FPR("v14", fpr, vregs[14], Vec128, 78) \
    FPR("v15", fpr, vregs[15], Vec128, 79) \
    FPR("v16", fpr, vregs[16], Vec128, 80) \
    FPR("v17", fpr, vregs[17], Vec128, 81) \
    FPR("v18", fpr, vregs[18], Vec128, 82) \
    FPR("v19", fpr, vregs[19], Vec128, 83) \
    FPR("v20", fpr, vregs[20], Vec128, 84) \
    FPR("v21", fpr, vregs[21], Vec128, 85) \
    FPR("v22", fpr, vregs[22], Vec128, 86) \
    FPR("v23", fpr, vregs[23], Vec128, 87) \
    FPR("v24", fpr, vregs[24], Vec128, 88) \
    FPR("v25", fpr, vregs[25], Vec128, 89) \
    FPR("v26", fpr, vregs[26], Vec128, 90) \
    FPR("v27", fpr, vregs[27], Vec128, 91) \
    FPR("v28", fpr, vregs[28], Vec128, 92) \
    FPR("v29", fpr, vregs[29], Vec128, 93) \
    FPR("v30", fpr, vregs[30], Vec128, 94) \
    FPR("v31", fpr, vregs[31], Vec128, 95) \
    FPR("fpsr", fpr, fpsr, U32, 96) \
    FPR("fpcr", fpr, fpcr, U32, 97) \
    \
    /* ---------------- Debug Breakpoint Registers ---------------- */ \
    HWBRK("brk_addr0", hw_break, dbg_regs[0].addr, U64, -1) \
    HWBRK("brk_ctrl0", hw_break, dbg_regs[0].ctrl, U32, -1) \
    HWBRK("brk_addr1", hw_break, dbg_regs[1].addr, U64, -1) \
    HWBRK("brk_ctrl1", hw_break, dbg_regs[1].ctrl, U32, -1) \
    HWBRK("brk_addr2", hw_break, dbg_regs[2].addr, U64, -1) \
    HWBRK("brk_ctrl2", hw_break, dbg_regs[2].ctrl, U32, -1) \
    HWBRK("brk_addr3", hw_break, dbg_regs[3].addr, U64, -1) \
    HWBRK("brk_ctrl3", hw_break, dbg_regs[3].ctrl, U32, -1) \
    HWBRK("brk_addr4", hw_break, dbg_regs[4].addr, U64, -1) \
    HWBRK("brk_ctrl4", hw_break, dbg_regs[4].ctrl, U32, -1) \
    HWBRK("brk_addr5", hw_break, dbg_regs[5].addr, U64, -1) \
    HWBRK("brk_ctrl5", hw_break, dbg_regs[5].ctrl, U32, -1) \
    HWBRK("brk_addr6", hw_break, dbg_regs[6].addr, U64, -1) \
    HWBRK("brk_ctrl6", hw_break, dbg_regs[6].ctrl, U32, -1) \
    HWBRK("brk_addr7", hw_break, dbg_regs[7].addr, U64, -1) \
    HWBRK("brk_ctrl7", hw_break, dbg_regs[7].ctrl, U32, -1) \
    HWBRK("brk_addr8", hw_break, dbg_regs[8].addr, U64, -1) \
    HWBRK("brk_ctrl8", hw_break, dbg_regs[8].ctrl, U32, -1) \
    HWBRK("brk_addr9", hw_break, dbg_regs[9].addr, U64, -1) \
    HWBRK("brk_ctrl9", hw_break, dbg_regs[9].ctrl, U32, -1) \
    HWBRK("brk_addr10", hw_break, dbg_regs[10].addr, U64, -1) \
    HWBRK("brk_ctrl10", hw_break, dbg_regs[10].ctrl, U32, -1) \
    HWBRK("brk_addr11", hw_break, dbg_regs[11].addr, U64, -1) \
    HWBRK("brk_ctrl11", hw_break, dbg_regs[11].ctrl, U32, -1) \
    HWBRK("brk_addr12", hw_break, dbg_regs[12].addr, U64, -1) \
    HWBRK("brk_ctrl12", hw_break, dbg_regs[12].ctrl, U32, -1) \
    HWBRK("brk_addr13", hw_break, dbg_regs[13].addr, U64, -1) \
    HWBRK("brk_ctrl13", hw_break, dbg_regs[13].ctrl, U32, -1) \
    HWBRK("brk_addr14", hw_break, dbg_regs[14].addr, U64, -1) \
    HWBRK("brk_ctrl14", hw_break, dbg_regs[14].ctrl, U32, -1) \
    HWBRK("brk_addr15", hw_break, dbg_regs[15].addr, U64, -1) \
    HWBRK("brk_ctrl15", hw_break, dbg_regs[15].ctrl, U32, -1) \
    \
    /* ---------------- Debug Watchpoint Registers ---------------- */ \
    HWWATCH("watch_addr0", hw_watch, dbg_regs[0].addr, U64, -1) \
    HWWATCH("watch_ctrl0", hw_watch, dbg_regs[0].ctrl, U32, -1) \
    HWWATCH("watch_addr1", hw_watch, dbg_regs[1].addr, U64, -1) \
    HWWATCH("watch_ctrl1", hw_watch, dbg_regs[1].ctrl, U32, -1) \
    HWWATCH("watch_addr2", hw_watch, dbg_regs[2].addr, U64, -1) \
    HWWATCH("watch_ctrl2", hw_watch, dbg_regs[2].ctrl, U32, -1) \
    HWWATCH("watch_addr3", hw_watch, dbg_regs[3].addr, U64, -1) \
    HWWATCH("watch_ctrl3", hw_watch, dbg_regs[3].ctrl, U32, -1) \
    HWWATCH("watch_addr4", hw_watch, dbg_regs[4].addr, U64, -1) \
    HWWATCH("watch_ctrl4", hw_watch, dbg_regs[4].ctrl, U32, -1) \
    HWWATCH("watch_addr5", hw_watch, dbg_regs[5].addr, U64, -1) \
    HWWATCH("watch_ctrl5", hw_watch, dbg_regs[5].ctrl, U32, -1) \
    HWWATCH("watch_addr6", hw_watch, dbg_regs[6].addr, U64, -1) \
    HWWATCH("watch_ctrl6", hw_watch, dbg_regs[6].ctrl, U32, -1) \
    HWWATCH("watch_addr7", hw_watch, dbg_regs[7].addr, U64, -1) \
    HWWATCH("watch_ctrl7", hw_watch, dbg_regs[7].ctrl, U32, -1) \
    HWWATCH("watch_addr8", hw_watch, dbg_regs[8].addr, U64, -1) \
    HWWATCH("watch_ctrl8", hw_watch, dbg_regs[8].ctrl, U32, -1) \
    HWWATCH("watch_addr9", hw_watch, dbg_regs[9].addr, U64, -1) \
    HWWATCH("watch_ctrl9", hw_watch, dbg_regs[9].ctrl, U32, -1) \
    HWWATCH("watch_addr10", hw_watch, dbg_regs[10].addr, U64, -1) \
    HWWATCH("watch_ctrl10", hw_watch, dbg_regs[10].ctrl, U32, -1) \
    HWWATCH("watch_addr11", hw_watch, dbg_regs[11].addr, U64, -1) \
    HWWATCH("watch_ctrl11", hw_watch, dbg_regs[11].ctrl, U32, -1) \
    HWWATCH("watch_addr12", hw_watch, dbg_regs[12].addr, U64, -1) \
    HWWATCH("watch_ctrl12", hw_watch, dbg_regs[12].ctrl, U32, -1) \
    HWWATCH("watch_addr13", hw_watch, dbg_regs[13].addr, U64, -1) \
    HWWATCH("watch_ctrl13", hw_watch, dbg_regs[13].ctrl, U32, -1) \
    HWWATCH("watch_addr14", hw_watch, dbg_regs[14].addr, U64, -1) \
    HWWATCH("watch_ctrl14", hw_watch, dbg_regs[14].ctrl, U32, -1) \
    HWWATCH("watch_addr15", hw_watch, dbg_regs[15].addr, U64, -1) \
    HWWATCH("watch_ctrl15", hw_watch, dbg_regs[15].ctrl, U32, -1)
