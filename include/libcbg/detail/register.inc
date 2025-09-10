#pragma once

// Register formats
// - UInt32: 32-bit integer
// - UInt64: 64-bit integer
// - UInt128: 128-bit SIMD/FPR
// (We keep them simple, debugger interprets them later)

#define REGISTER_LIST(GPR, FPR, HWBRK, HWWATCH) \
    /* ---------------- General Purpose Registers ---------------- */ \
    GPR("x0",  gpr, regs[0],  UInt64, 0) \
    GPR("x1",  gpr, regs[1],  UInt64, 1) \
    GPR("x2",  gpr, regs[2],  UInt64, 2) \
    GPR("x3",  gpr, regs[3],  UInt64, 3) \
    GPR("x4",  gpr, regs[4],  UInt64, 4) \
    GPR("x5",  gpr, regs[5],  UInt64, 5) \
    GPR("x6",  gpr, regs[6],  UInt64, 6) \
    GPR("x7",  gpr, regs[7],  UInt64, 7) \
    GPR("x8",  gpr, regs[8],  UInt64, 8) \
    GPR("x9",  gpr, regs[9],  UInt64, 9) \
    GPR("x10", gpr, regs[10], UInt64, 10) \
    GPR("x11", gpr, regs[11], UInt64, 11) \
    GPR("x12", gpr, regs[12], UInt64, 12) \
    GPR("x13", gpr, regs[13], UInt64, 13) \
    GPR("x14", gpr, regs[14], UInt64, 14) \
    GPR("x15", gpr, regs[15], UInt64, 15) \
    GPR("x16", gpr, regs[16], UInt64, 16) \
    GPR("x17", gpr, regs[17], UInt64, 17) \
    GPR("x18", gpr, regs[18], UInt64, 18) \
    GPR("x19", gpr, regs[19], UInt64, 19) \
    GPR("x20", gpr, regs[20], UInt64, 20) \
    GPR("x21", gpr, regs[21], UInt64, 21) \
    GPR("x22", gpr, regs[22], UInt64, 22) \
    GPR("x23", gpr, regs[23], UInt64, 23) \
    GPR("x24", gpr, regs[24], UInt64, 24) \
    GPR("x25", gpr, regs[25], UInt64, 25) \
    GPR("x26", gpr, regs[26], UInt64, 26) \
    GPR("x27", gpr, regs[27], UInt64, 27) \
    GPR("x28", gpr, regs[28], UInt64, 28) \
    GPR("x29", gpr, regs[29], UInt64, 29) \
    GPR("x30", gpr, regs[30], UInt64, 30) \
    GPR("sp",  gpr, sp,       UInt64, 31) \
    GPR("pc",  gpr, pc,       UInt64, 32) \
    GPR("pstate", gpr, pstate, UInt64, 33) \
    \
    /* ---------------- SIMD/Floating Point Registers ---------------- */ \
    FPR("v0",  fpr, vregs[0],  UInt128, 64) \
    FPR("v1",  fpr, vregs[1],  UInt128, 65) \
    FPR("v2",  fpr, vregs[2],  UInt128, 66) \
    FPR("v3",  fpr, vregs[3],  UInt128, 67) \
    FPR("v4",  fpr, vregs[4],  UInt128, 68) \
    FPR("v5",  fpr, vregs[5],  UInt128, 69) \
    FPR("v6",  fpr, vregs[6],  UInt128, 70) \
    FPR("v7",  fpr, vregs[7],  UInt128, 71) \
    FPR("v8",  fpr, vregs[8],  UInt128, 72) \
    FPR("v9",  fpr, vregs[9],  UInt128, 73) \
    FPR("v10", fpr, vregs[10], UInt128, 74) \
    FPR("v11", fpr, vregs[11], UInt128, 75) \
    FPR("v12", fpr, vregs[12], UInt128, 76) \
    FPR("v13", fpr, vregs[13], UInt128, 77) \
    FPR("v14", fpr, vregs[14], UInt128, 78) \
    FPR("v15", fpr, vregs[15], UInt128, 79) \
    FPR("v16", fpr, vregs[16], UInt128, 80) \
    FPR("v17", fpr, vregs[17], UInt128, 81) \
    FPR("v18", fpr, vregs[18], UInt128, 82) \
    FPR("v19", fpr, vregs[19], UInt128, 83) \
    FPR("v20", fpr, vregs[20], UInt128, 84) \
    FPR("v21", fpr, vregs[21], UInt128, 85) \
    FPR("v22", fpr, vregs[22], UInt128, 86) \
    FPR("v23", fpr, vregs[23], UInt128, 87) \
    FPR("v24", fpr, vregs[24], UInt128, 88) \
    FPR("v25", fpr, vregs[25], UInt128, 89) \
    FPR("v26", fpr, vregs[26], UInt128, 90) \
    FPR("v27", fpr, vregs[27], UInt128, 91) \
    FPR("v28", fpr, vregs[28], UInt128, 92) \
    FPR("v29", fpr, vregs[29], UInt128, 93) \
    FPR("v30", fpr, vregs[30], UInt128, 94) \
    FPR("v31", fpr, vregs[31], UInt128, 95) \
    FPR("fpsr", fpr, fpsr, UInt32, 96) \
    FPR("fpcr", fpr, fpcr, UInt32, 97) \
    \
    /* ---------------- Debug Breakpoint Registers ---------------- */ \
    HWBRK("brk_addr0", hw_break, dbg_regs[0].addr, UInt64, -1) \
    HWBRK("brk_ctrl0", hw_break, dbg_regs[0].ctrl, UInt32, -1) \
    HWBRK("brk_addr1", hw_break, dbg_regs[1].addr, UInt64, -1) \
    HWBRK("brk_ctrl1", hw_break, dbg_regs[1].ctrl, UInt32, -1) \
    HWBRK("brk_addr2", hw_break, dbg_regs[2].addr, UInt64, -1) \
    HWBRK("brk_ctrl2", hw_break, dbg_regs[2].ctrl, UInt32, -1) \
    HWBRK("brk_addr3", hw_break, dbg_regs[3].addr, UInt64, -1) \
    HWBRK("brk_ctrl3", hw_break, dbg_regs[3].ctrl, UInt32, -1) \
    HWBRK("brk_addr4", hw_break, dbg_regs[4].addr, UInt64, -1) \
    HWBRK("brk_ctrl4", hw_break, dbg_regs[4].ctrl, UInt32, -1) \
    HWBRK("brk_addr5", hw_break, dbg_regs[5].addr, UInt64, -1) \
    HWBRK("brk_ctrl5", hw_break, dbg_regs[5].ctrl, UInt32, -1) \
    HWBRK("brk_addr6", hw_break, dbg_regs[6].addr, UInt64, -1) \
    HWBRK("brk_ctrl6", hw_break, dbg_regs[6].ctrl, UInt32, -1) \
    HWBRK("brk_addr7", hw_break, dbg_regs[7].addr, UInt64, -1) \
    HWBRK("brk_ctrl7", hw_break, dbg_regs[7].ctrl, UInt32, -1) \
    HWBRK("brk_addr8", hw_break, dbg_regs[8].addr, UInt64, -1) \
    HWBRK("brk_ctrl8", hw_break, dbg_regs[8].ctrl, UInt32, -1) \
    HWBRK("brk_addr9", hw_break, dbg_regs[9].addr, UInt64, -1) \
    HWBRK("brk_ctrl9", hw_break, dbg_regs[9].ctrl, UInt32, -1) \
    HWBRK("brk_addr10", hw_break, dbg_regs[10].addr, UInt64, -1) \
    HWBRK("brk_ctrl10", hw_break, dbg_regs[10].ctrl, UInt32, -1) \
    HWBRK("brk_addr11", hw_break, dbg_regs[11].addr, UInt64, -1) \
    HWBRK("brk_ctrl11", hw_break, dbg_regs[11].ctrl, UInt32, -1) \
    HWBRK("brk_addr12", hw_break, dbg_regs[12].addr, UInt64, -1) \
    HWBRK("brk_ctrl12", hw_break, dbg_regs[12].ctrl, UInt32, -1) \
    HWBRK("brk_addr13", hw_break, dbg_regs[13].addr, UInt64, -1) \
    HWBRK("brk_ctrl13", hw_break, dbg_regs[13].ctrl, UInt32, -1) \
    HWBRK("brk_addr14", hw_break, dbg_regs[14].addr, UInt64, -1) \
    HWBRK("brk_ctrl14", hw_break, dbg_regs[14].ctrl, UInt32, -1) \
    HWBRK("brk_addr15", hw_break, dbg_regs[15].addr, UInt64, -1) \
    HWBRK("brk_ctrl15", hw_break, dbg_regs[15].ctrl, UInt32, -1) \
    \
    /* ---------------- Debug Watchpoint Registers ---------------- */ \
    HWWATCH("watch_addr0", hw_watch, dbg_regs[0].addr, UInt64, -1) \
    HWWATCH("watch_ctrl0", hw_watch, dbg_regs[0].ctrl, UInt32, -1) \
    HWWATCH("watch_addr1", hw_watch, dbg_regs[1].addr, UInt64, -1) \
    HWWATCH("watch_ctrl1", hw_watch, dbg_regs[1].ctrl, UInt32, -1) \
    HWWATCH("watch_addr2", hw_watch, dbg_regs[2].addr, UInt64, -1) \
    HWWATCH("watch_ctrl2", hw_watch, dbg_regs[2].ctrl, UInt32, -1) \
    HWWATCH("watch_addr3", hw_watch, dbg_regs[3].addr, UInt64, -1) \
    HWWATCH("watch_ctrl3", hw_watch, dbg_regs[3].ctrl, UInt32, -1) \
    HWWATCH("watch_addr4", hw_watch, dbg_regs[4].addr, UInt64, -1) \
    HWWATCH("watch_ctrl4", hw_watch, dbg_regs[4].ctrl, UInt32, -1) \
    HWWATCH("watch_addr5", hw_watch, dbg_regs[5].addr, UInt64, -1) \
    HWWATCH("watch_ctrl5", hw_watch, dbg_regs[5].ctrl, UInt32, -1) \
    HWWATCH("watch_addr6", hw_watch, dbg_regs[6].addr, UInt64, -1) \
    HWWATCH("watch_ctrl6", hw_watch, dbg_regs[6].ctrl, UInt32, -1) \
    HWWATCH("watch_addr7", hw_watch, dbg_regs[7].addr, UInt64, -1) \
    HWWATCH("watch_ctrl7", hw_watch, dbg_regs[7].ctrl, UInt32, -1) \
    HWWATCH("watch_addr8", hw_watch, dbg_regs[8].addr, UInt64, -1) \
    HWWATCH("watch_ctrl8", hw_watch, dbg_regs[8].ctrl, UInt32, -1) \
    HWWATCH("watch_addr9", hw_watch, dbg_regs[9].addr, UInt64, -1) \
    HWWATCH("watch_ctrl9", hw_watch, dbg_regs[9].ctrl, UInt32, -1) \
    HWWATCH("watch_addr10", hw_watch, dbg_regs[10].addr, UInt64, -1) \
    HWWATCH("watch_ctrl10", hw_watch, dbg_regs[10].ctrl, UInt32, -1) \
    HWWATCH("watch_addr11", hw_watch, dbg_regs[11].addr, UInt64, -1) \
    HWWATCH("watch_ctrl11", hw_watch, dbg_regs[11].ctrl, UInt32, -1) \
    HWWATCH("watch_addr12", hw_watch, dbg_regs[12].addr, UInt64, -1) \
    HWWATCH("watch_ctrl12", hw_watch, dbg_regs[12].ctrl, UInt32, -1) \
    HWWATCH("watch_addr13", hw_watch, dbg_regs[13].addr, UInt64, -1) \
    HWWATCH("watch_ctrl13", hw_watch, dbg_regs[13].ctrl, UInt32, -1) \
    HWWATCH("watch_addr14", hw_watch, dbg_regs[14].addr, UInt64, -1) \
    HWWATCH("watch_ctrl14", hw_watch, dbg_regs[14].ctrl, UInt32, -1) \
    HWWATCH("watch_addr15", hw_watch, dbg_regs[15].addr, UInt64, -1) \
    HWWATCH("watch_ctrl15", hw_watch, dbg_regs[15].ctrl, UInt32, -1)
