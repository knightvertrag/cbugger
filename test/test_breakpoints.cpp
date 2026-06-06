#include <catch2/catch_test_macros.hpp>
#include <libcbg/process.hpp>
#include <libcbg/error.hpp>
#include "test_common.hpp"

using namespace cbg;

// =============================================================================
// Software + Hardware breakpoint behavior
// =============================================================================

TEST_CASE("Software breakpoints: add/remove, memory restore, hit + step-over with PC rewind", "[breakpoint][sw]")
{
    // Launch traced; we are stopped at entry. Use a short sequence of explicit single-steps
    // to advance into the loop body, then use the *current PC* as a reliable bp target
    // (no fixed VA or disassembly required).
    auto proc = Process::launch("targets/run_endlessly");

    auto &regs = proc->get_registers();

    // Advance a few instructions into the binary (past typical prologue) so we are
    // reliably inside the "while(true) i=69;" loop.
    for (int i = 0; i < 6; ++i)
    {
        (void)proc->step_instruction();
    }

    // Capture a PC inside the hot loop as our bp target.
    regs.load();
    auto pc_d = regs.lookup("pc");
    uint64_t bp_addr = regs.get_register(pc_d).get<uint64_t>();
    REQUIRE(bp_addr != 0);

    // Remember the original instruction word at that location.
    uint64_t orig_word = proc->read_memory(bp_addr);

    // Install SW bp.
    int id = proc->add_breakpoint(bp_addr);
    REQUIRE(id > 0);

    // The memory at bp_addr must now contain the brk insn (low 32 bits).
    uint64_t after_install = proc->read_memory(bp_addr);
    REQUIRE((after_install & 0xFFFFFFFFu) == 0xd4200000u);

    // Continue (resume). The transparent step-over logic in wait_on_signal should:
    // - see the TRAP at bp_addr
    // - restore orig, SINGLESTEP it, re-insert BRK, rewind PC back to bp_addr
    // - return a STOPPED/TRAP reason with (rewound) PC == bp_addr
    proc->resume();
    auto reason = proc->wait_on_signal();
    REQUIRE(reason.reason == process_state::STOPPED);
    REQUIRE(reason.info == SIGTRAP);

    regs.load();
    uint64_t after_hit_pc = regs.get_register(pc_d).get<uint64_t>();
    REQUIRE(after_hit_pc == bp_addr);

    // The bp is re-armed (memory still has brk).
    uint64_t still_armed = proc->read_memory(bp_addr);
    REQUIRE((still_armed & 0xFFFFFFFFu) == 0xd4200000u);

    // Remove the bp and verify original bytes are restored.
    REQUIRE(proc->remove_breakpoint(id));
    uint64_t after_remove = proc->read_memory(bp_addr);
    REQUIRE(after_remove == orig_word);

    // Also exercise remove-by-addr and get_breakpoints a bit.
    int id2 = proc->add_breakpoint(bp_addr + 4); // some other addr (harmless)
    auto listed = proc->get_breakpoints();
    REQUIRE(listed.size() >= 1);
    REQUIRE(proc->remove_breakpoint(bp_addr + 4));

    // Scope exit will run dtor (which also does best-effort restore for any survivors).
}

TEST_CASE("Hardware breakpoint API: slot allocation, enable/disable, num_slots", "[breakpoint][hw]")
{
    auto proc = Process::launch("targets/run_endlessly");

    int n = proc->num_hw_breakpoint_slots();
    // Most aarch64 have at least a few; if the env reports 0 we still exercise the call paths.
    if (n > 0)
    {
        // Advance a little so we have a distinct VA.
        for (int i = 0; i < 4; ++i) (void)proc->step_instruction();
        auto &regs = proc->get_registers();
        regs.load();
        uint64_t addr = regs.get_register(regs.lookup("pc")).get<uint64_t>();

        int slot = proc->enable_hw_breakpoint(addr);
        REQUIRE(slot >= 0);
        REQUIRE(slot < n);

        // The corresponding ctrl should have E bit set (via raw reg view).
        regs.load();
        std::string cname = "brk_ctrl" + std::to_string(slot);
        uint32_t ctrl = regs.get_register(regs.lookup(cname)).get<uint32_t>();
        REQUIRE((ctrl & 1u) == 1u);

        proc->disable_hw_breakpoint(slot);

        regs.load();
        uint32_t ctrl2 = regs.get_register(regs.lookup(cname)).get<uint32_t>();
        REQUIRE((ctrl2 & 1u) == 0u);
    }
    else
    {
        // Still call enable to exercise the "no slots" error path (it throws).
        REQUIRE_THROWS_AS(proc->enable_hw_breakpoint(0x1000), cbg::Error);
    }
}
