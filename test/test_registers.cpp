#include <catch2/catch_test_macros.hpp>
#include <fstream>
#include <variant>
#include <cstring>
#include <cmath>
#include <libcbg/process.hpp>
#include <libcbg/error.hpp>
#include <libcbg/pipe.hpp>
#include <libcbg/bit.hpp>
#include "test_common.hpp"

using namespace cbg;

// =============================================================================
// Register read / write (via targets + pipes)
// =============================================================================

TEST_CASE("Write register works", "[register]")
{
    bool close_on_exec = false;
    cbg::Pipe channel(close_on_exec);

    auto proc = Process::launch("targets/register_write", true, channel.get_write());
    channel.close_write();

    proc->resume();
    proc->wait_on_signal();

    auto &regs = proc->get_registers();
    {
        auto h = regs.resolve("x20");
        regs.write(h, static_cast<__uint128_t>(0x12345678));
    }
    regs.save();
    proc->resume();
    proc->wait_on_signal();
    // regs.load();
    auto data = channel.read();
    REQUIRE(to_string_view(data) == "0x12345678");
    
    {
        auto h = regs.resolve("v20");
        regs.write(h, static_cast<__uint128_t>(0x1234567812345678ULL));
    }
    regs.save();
    proc->resume();
    proc->wait_on_signal();
    data = channel.read();
    REQUIRE(to_string_view(data) == "0000000000000000 1234567812345678");

}

TEST_CASE("Read register works", "[register]")
{
    bool close_on_exec = false;
    cbg::Pipe channel(close_on_exec);

    auto proc = Process::launch("targets/register_read", true, channel.get_write());
    channel.close_write();

    proc->resume();
    proc->wait_on_signal();

    auto &regs = proc->get_registers();
    regs.load();
    auto x22 = regs.get_register("x22");
    REQUIRE(x22.get<uint64_t>() == 0xDEADBEEF);

    // auto v20 = regs.get_register("v20");
    // REQUIRE(v20 == 0x8765432187654321);

}

// =============================================================================
// Subregister views, write policies, aliasing (wN/sN/dN/vN lanes)
// =============================================================================

TEST_CASE("Subregister views (wN/sN/dN + vN lanes) implement correct aliasing and write policies", "[subregister]")
{
    // Launch with tracing enabled (default). This gives us a stopped + ptraced process immediately.
    // We do not need to resume the child at all — all subregister work happens in the debugger's
    // local copies of the regsets (gpr/fpr). The child target is only there to give us a live pid.
    auto proc = Process::launch("targets/run_endlessly");

    auto &regs = proc->get_registers();
    regs.load();

    // --- wN: 32-bit write zero-extends in the parent xN; reads truncate ---
    {
        auto sub = regs.make_subview_by_name("w19");
        REQUIRE(sub.has_value());
        sub->write_u32(0xDEADBEEF);
    }
    uint64_t x19 = regs.get_register("x19").get<uint64_t>();
    REQUIRE(x19 == 0x00000000DEADBEEF);

    {
        auto sub = regs.make_subview_by_name("w19");
        REQUIRE(sub.has_value());
        REQUIRE(sub->read_u64() == 0xDEADBEEF);
    }

    {
        auto h = regs.resolve("x8");
        regs.write(h, static_cast<__uint128_t>(0x1122334455667788ULL));
    }
    {
        auto sub = regs.make_subview_by_name("w8");
        REQUIRE(sub.has_value());
        REQUIRE(sub->read_u64() == 0x55667788);
    }

    REQUIRE_FALSE(regs.make_subview_by_name("w31").has_value());   // no physical w31 in the regset
    REQUIRE_FALSE(regs.make_subview_by_name("w99").has_value());

    // Use handle write (recommended) for sub names too (no more string set_* overloads)
    {
        auto h = regs.resolve("w20");
        regs.write(h, static_cast<__uint128_t>(0xCAFEBABE));
    }
    uint64_t x20 = regs.get_register("x20").get<uint64_t>();
    REQUIRE(x20 == 0x00000000CAFEBABE);  // zero-extend policy still applied

    {
        auto sub = regs.make_subview_by_name("w20");
        REQUIRE(sub.has_value());
        REQUIRE(sub->read_u64() == 0xCAFEBABE);
    }

    // FP sub via handle write (raw bit pattern) — s7 write using integer bits
    {
        auto h = regs.resolve("s7");
        regs.write(h, static_cast<__uint128_t>(0x40490FDBu));  // bits for 3.1415927f approx
    }
    {
        auto sub = regs.make_subview_by_name("d7");
        REQUIRE(sub.has_value());
        uint64_t d7_after_s_via_set = sub->read_u64();
        REQUIRE((d7_after_s_via_set >> 32) == 0);  // ZeroUpperVector128 still honored
    }

    // --- sN / dN scalar writes zero the upper bits of the parent vN (ZeroUpperVector128) ---
    auto s5 = regs.make_subview_by_name("s5");
    REQUIRE(s5.has_value());
    s5->write_f32(3.14159f);

    // After s5 write, bits [127:32] of v5 are zeroed. Therefore d5 (low 64 bits) has bits [63:32] == 0.
    {
        auto sub = regs.make_subview_by_name("d5");
        REQUIRE(sub.has_value());
        uint64_t d5_after_s = sub->read_u64();
        REQUIRE((d5_after_s >> 32) == 0);
    }

    // Full dN write also zeros the upper 64 bits of the vN (bits 127:64).
    auto d12 = regs.make_subview_by_name("d12");
    REQUIRE(d12.has_value());
    d12->write_f64(2.718281828);
    uint64_t v12_upper_lane = regs.make_subview_by_name("v12.2d[1]")->read_u64();
    REQUIRE(v12_upper_lane == 0);

    {
        auto sub = regs.make_subview_by_name("d31");
        REQUIRE(sub.has_value());
        sub->write_u64(0xCAFEBABECAFEBABEULL);
    }
    uint64_t v31_upper = regs.make_subview_by_name("v31.2d[1]")->read_u64();
    REQUIRE(v31_upper == 0);

    // --- vN lane writes (4s/2d) use PreserveParentBits (other lanes must be untouched) ---
    // All of these should now preserve siblings because the name parser forces Preserve for lane syntax.
    auto l0 = regs.make_subview_by_name("v20.4s[0]");
    auto l2 = regs.make_subview_by_name("v20.4s[2]");
    l0->write_u32(0x11111111);
    l2->write_u32(0x22222222);
    {
        auto sub = regs.make_subview_by_name("v20.4s[1]");
        REQUIRE(sub.has_value());
        sub->write_u32(0x33333333);
    }
    {
        auto sub = regs.make_subview_by_name("v20.4s[3]");
        REQUIRE(sub.has_value());
        sub->write_u32(0x44444444);
    }

    REQUIRE(regs.make_subview_by_name("v20.4s[0]")->read_u32() == 0x11111111);
    REQUIRE(regs.make_subview_by_name("v20.4s[1]")->read_u32() == 0x33333333);
    REQUIRE(regs.make_subview_by_name("v20.4s[2]")->read_u32() == 0x22222222);
    REQUIRE(regs.make_subview_by_name("v20.4s[3]")->read_u32() == 0x44444444);

    // Out-of-range lanes and unknown arrangements return empty optional
    REQUIRE_FALSE(regs.make_subview_by_name("v5.4s[4]").has_value());
    REQUIRE_FALSE(regs.make_subview_by_name("v5.2d[2]").has_value());
    REQUIRE_FALSE(regs.make_subview_by_name("v0.8h[0]").has_value()); // unsupported syntax

    // Exercise the zero_upper_fp parameter path (false → PreserveParentBits for the lane)
    auto lane_preserve = regs.make_subview_by_name("v7.4s[1]", /*zero_upper_fp=*/false);
    REQUIRE(lane_preserve.has_value());
    lane_preserve->write_u32(0xABCDABCD);
    // (We don't assert sibling state here because we didn't initialize the other lanes in this scope,
    // but the call exercises the Preserve code path and the factory.)

    // No resume needed. The Process dtor will clean up the traced child.
}

// =============================================================================
// Descriptor (O(1)) and Handle (unified full+sub read/write) APIs
// =============================================================================

TEST_CASE("RegisterDescriptor provides fast O(1) access equivalent to string lookup", "[register][descriptor]")
{
    auto proc = Process::launch("targets/run_endlessly");
    auto &regs = proc->get_registers();
    regs.load();

    // Basic equivalence for a few representative registers
    auto d_x19 = regs.lookup("x19");
    auto d_v5  = regs.lookup("v5");
    auto d_fpsr = regs.lookup("fpsr");

    REQUIRE(&regs.get_register("x19") == &regs.get_register(d_x19));
    REQUIRE(&regs.get_register("v5")  == &regs.get_register(d_v5));
    REQUIRE(&regs.get_register("fpsr") == &regs.get_register(d_fpsr));

    // Writes via descriptor are visible via get (string or desc)
    regs.set_register(d_x19, static_cast<__uint128_t>(0xDEADBEEFCAFEBABEULL));
    REQUIRE(regs.get_register("x19").get<uint64_t>() == 0xDEADBEEFCAFEBABEULL);

    // Exercise 128-bit set_register via descriptor (string name overloads removed;
    // use resolve+write(h, val) or lookup+set_register(desc, val) instead)
    __uint128_t v128val = (__uint128_t(0x1122334455667788ULL) << 64) | 0x99AABBCCDDEEFF00ULL;
    regs.set_register(d_v5, v128val);
    auto v5_via_desc = regs.get_register(d_v5).get<__uint128_t>();
    REQUIRE(v5_via_desc == v128val);

    // legacy direct view set still works
    regs.get_register("v5").set<__uint128_t>(v128val);
    auto v5_via_desc2 = regs.get_register(d_v5).get<__uint128_t>();
    REQUIRE(v5_via_desc2 == v128val);

    // "Resolve once, use many times" pattern works cleanly
    std::vector<cbg::RegisterDescriptor> gpr_descs;
    const char *some_gprs[] = {"x0", "x8", "sp", "pc"};
    for (const char *n : some_gprs)
        gpr_descs.push_back(regs.lookup(n));

    for (size_t i = 0; i < gpr_descs.size(); ++i)
    {
        uint64_t val = 0x1000 + i;
        regs.set_register(gpr_descs[i], static_cast<__uint128_t>(val));
        REQUIRE(regs.get_register(some_gprs[i]).get<uint64_t>() == val);
    }

    // HW debug registers also work
    auto d_brk = regs.lookup("brk_addr3");
    regs.set_register(d_brk, static_cast<__uint128_t>(0x0000AAAA00001234ULL));
    REQUIRE(regs.get_register("brk_addr3").get<uint64_t>() == 0x0000AAAA00001234ULL);
}

// =============================================================================
// RegisterHandle unified resolve + read/write + parse_register_value
// =============================================================================

TEST_CASE("RegisterHandle provides unified resolve + read/write for full and sub registers", "[register][handle]")
{
    auto proc = Process::launch("targets/run_endlessly");
    auto &regs = proc->get_registers();
    regs.load();

    // Full register handle
    auto h_x19 = regs.resolve("x19");
    REQUIRE(regs.get_format(h_x19) == RegisterFormat::U64);
    REQUIRE(regs.get_bit_size(h_x19) == 64);

    // Subregister handles of different kinds
    auto h_w19   = regs.resolve("w19");
    auto h_s5    = regs.resolve("s5");
    auto h_d12   = regs.resolve("d12");
    auto h_lane  = regs.resolve("v20.4s[2]");
    auto h_lane2 = regs.resolve("v7.2d[1]");

    REQUIRE(regs.get_format(h_w19) == RegisterFormat::U32);
    REQUIRE(regs.get_bit_size(h_w19) == 32);

    REQUIRE(regs.get_format(h_s5) == RegisterFormat::F32);
    REQUIRE(regs.get_bit_size(h_s5) == 32);

    REQUIRE(regs.get_format(h_d12) == RegisterFormat::F64);
    REQUIRE(regs.get_bit_size(h_d12) == 64);

    REQUIRE(regs.get_format(h_lane) == RegisterFormat::F32);
    REQUIRE(regs.get_bit_size(h_lane) == 32);

    // Read equivalence with old paths
    auto via_handle = regs.read(h_x19);
    auto via_classic = regs.get_register("x19").get<uint64_t>();
    REQUIRE(via_handle.has_value());
    REQUIRE(*via_handle == via_classic);

    auto via_sub = regs.read(h_w19);
    REQUIRE(via_sub.has_value());

    // Write via handle + read back (full reg)
    regs.write(h_x19, 0xDEADBEEFCAFEBABEULL);
    REQUIRE(*regs.read(h_x19) == 0xDEADBEEFCAFEBABEULL);

    // Write via handle + policy verification for wN (zero-extend)
    regs.write(regs.resolve("x8"), 0xFFFFFFFFFFFFFFFFULL);
    regs.write(regs.resolve("w8"), 0x12345678);
    REQUIRE(regs.get_register("x8").get<uint64_t>() == 0x0000000012345678ULL);

    // Write via handle for scalar float view (sN should zero upper bits of parent v reg)
    regs.write(regs.resolve("v5"), 0xFFFFFFFFFFFFFFFFULL); // lower 64 bits of v5
    regs.write(regs.resolve("s5"), 0x40490FDBu); // bits for ~3.14159f
    auto d5_after = regs.read(regs.resolve("d5"));
    REQUIRE(d5_after.has_value());
    REQUIRE((*d5_after >> 32) == 0); // upper 32 bits of the low 64 should be zeroed

    // 128-bit full vN via handle (addresses prior limitation: read/write now preserve all bits)
    __uint128_t vfull = (static_cast<__uint128_t>(0xDEADBEEFCAFEBABEULL) << 64) |
                        static_cast<__uint128_t>(0x0123456789ABCDEFULL);
    auto h_v10 = regs.resolve("v10");
    REQUIRE(regs.get_bit_size(h_v10) == 128);
    REQUIRE(regs.get_format(h_v10) == RegisterFormat::Vec128);
    regs.write(h_v10, vfull);
    auto v10_got = regs.read(h_v10);
    REQUIRE(v10_got.has_value());
    REQUIRE(*v10_got == vfull);
    // Also verify via classic path that memory was updated
    __uint128_t via_view = regs.get_register("v10").get<__uint128_t>();
    REQUIRE(via_view == vfull);

    // Direct make_subview from descriptor
    auto h_w10 = regs.resolve("w10");
    auto sv_direct = regs.make_subview(std::get<SubregisterDescriptor>(h_w19));
    REQUIRE(sv_direct.has_value());
    // Write through the direct view should behave identically
    sv_direct->write_u32(0xBEEFDEAD);
    REQUIRE(*regs.read(h_w19) == 0xBEEFDEAD);

    // parse_register_value round-trips
    auto bits_int = Registers::parse_register_value("0x1234ABCD", RegisterFormat::U32, 32);
    REQUIRE(bits_int.has_value());
    REQUIRE(*bits_int == static_cast<__uint128_t>(0x1234ABCD));

    auto bits_f32 = Registers::parse_register_value("3.14159", RegisterFormat::F32, 32);
    REQUIRE(bits_f32.has_value());
    // Reinterpret and check roughly
    float f;
    std::memcpy(&f, &*bits_f32, sizeof(f));
    REQUIRE(std::abs(f - 3.14159f) < 0.0001f);
}

// Additional direct tests for the value parser used by register write paths.
TEST_CASE("parse_register_value handles integer and floating-point inputs correctly", "[register][parse]")
{
    using cbg::RegisterFormat;

    // Integer paths
    REQUIRE(Registers::parse_register_value("123", RegisterFormat::U32, 32).value() == 123);
    REQUIRE(Registers::parse_register_value("0xFF", RegisterFormat::U8, 8).value() == 0xFF);
    REQUIRE(Registers::parse_register_value("-1", RegisterFormat::U64, 64).value() == static_cast<__uint128_t>(~0ULL)); // strtoull behavior, promoted

    // 128-bit hex support (addresses prior limitation for full vN)
    auto p128 = Registers::parse_register_value("0x0000000000000000DEADBEEFCAFEBABE", RegisterFormat::Vec128, 128);
    REQUIRE(p128.has_value());
    REQUIRE(static_cast<uint64_t>(*p128) == 0xDEADBEEFCAFEBABEULL);
    REQUIRE(static_cast<uint64_t>(*p128 >> 64) == 0ULL);

    auto p128_hi = Registers::parse_register_value("0xDEADBEEFCAFEBABE0000000000000001", RegisterFormat::Vec128, 128);
    REQUIRE(p128_hi.has_value());
    REQUIRE(static_cast<uint64_t>(*p128_hi) == 0x1ULL);
    REQUIRE(static_cast<uint64_t>(*p128_hi >> 64) == 0xDEADBEEFCAFEBABEULL);

    // -1 for 128-bit width
    auto all128 = Registers::parse_register_value("-1", RegisterFormat::Vec128, 128);
    REQUIRE(all128.has_value());
    REQUIRE(*all128 == ~__uint128_t(0));

    // overlong hex for 128-bit should be invalid (not silently truncate high digits)
    auto too_long = Registers::parse_register_value("0x0000000000000000deadbeefcafebabe0123456789abcdef0", RegisterFormat::Vec128, 128);
    REQUIRE_FALSE(too_long.has_value());

    // Float paths with correct bit reinterpret
    auto f32_bits = Registers::parse_register_value("1.5", RegisterFormat::F32, 32);
    REQUIRE(f32_bits.has_value());
    float f32;
    std::memcpy(&f32, &*f32_bits, sizeof(f32));
    REQUIRE(f32 == 1.5f);

    auto f64_bits = Registers::parse_register_value("2.718281828", RegisterFormat::F64, 64);
    REQUIRE(f64_bits.has_value());
    double f64;
    std::memcpy(&f64, &*f64_bits, sizeof(f64));
    REQUIRE(std::abs(f64 - 2.718281828) < 1e-9);
}
