#include <libcbg/breakpoints.hpp>
#include <libcbg/process.hpp>
#include <libcbg/error.hpp>
#include <libcbg/registers.hpp>

#include <sys/ptrace.h>
#include <sys/wait.h>
#include <cerrno>
#include <unordered_map>
#include <algorithm>

namespace cbg
{

// Internal implementation (pimpl). All the state and policy that used to live
// directly in Process now lives here.
class Breakpoints::Impl
{
public:
    explicit Impl(Process& owner)
        : owner_(owner)
    {}

    int add_breakpoint(uint64_t addr)
    {
        if (owner_.pid() == 0)
        {
            Error::send("Process not initialized");
        }
        // Idempotent: if already present, return existing id
        for (const auto& [id, a] : bp_id_to_addr_)
        {
            if (a == addr)
                return id;
        }

        uint64_t orig = owner_.read_memory(addr);
        owner_.write_memory(addr, AARCH64_BRK_INSN);

        sw_breakpoints_[addr] = orig;
        int id = next_bp_id_++;
        bp_id_to_addr_[id] = addr;
        return id;
    }

    bool remove_breakpoint(int id)
    {
        auto it = bp_id_to_addr_.find(id);
        if (it == bp_id_to_addr_.end())
            return false;
        uint64_t addr = it->second;
        return remove_breakpoint(addr);
    }

    bool remove_breakpoint(uint64_t addr)
    {
        auto it = sw_breakpoints_.find(addr);
        if (it != sw_breakpoints_.end())
        {
            uint64_t orig = it->second;
            if (owner_.pid() != 0)
            {
                owner_.write_memory(addr, orig);
            }
            sw_breakpoints_.erase(it);
        }
        for (auto iit = bp_id_to_addr_.begin(); iit != bp_id_to_addr_.end(); )
        {
            if (iit->second == addr)
                iit = bp_id_to_addr_.erase(iit);
            else
                ++iit;
        }
        return true;
    }

    std::vector<std::pair<int, uint64_t>> get_breakpoints() const
    {
        std::vector<std::pair<int, uint64_t>> res;
        res.reserve(bp_id_to_addr_.size());
        for (const auto& p : bp_id_to_addr_)
            res.push_back(p);
        std::sort(res.begin(), res.end());
        return res;
    }

    std::optional<uint64_t> prepare_for_transparent_step_over(uint64_t current_pc)
    {
        auto it = sw_breakpoints_.find(current_pc);
        if (it == sw_breakpoints_.end())
            return std::nullopt;

        uint64_t orig = it->second;
        owner_.write_memory(current_pc, orig);
        return current_pc;   // bp_loc for the caller to remember
    }

    void rearm_and_rewind(uint64_t bp_loc)
    {
        owner_.write_memory(bp_loc, AARCH64_BRK_INSN);

        // Rewind PC for classic "stopped at breakpoint" presentation
        auto& regs = owner_.get_registers();
        auto h = regs.resolve("pc");
        regs.write(h, static_cast<__uint128_t>(bp_loc));
        owner_.write_back_registers();
    }

    // The owner will perform the actual SINGLESTEP + waitpid.
    // This method only does the temp-disable side for an explicit step when sitting on a bp.
    void prepare_step_for_instruction_at(uint64_t pc, uint64_t& out_bp_loc, uint64_t& out_orig, bool& out_was_at_bp)
    {
        out_was_at_bp = false;
        if (sw_breakpoints_.count(pc) > 0)
        {
            out_was_at_bp = true;
            out_bp_loc = pc;
            out_orig = sw_breakpoints_[pc];
            owner_.write_memory(pc, out_orig);
        }
    }

    void rearm_after_explicit_step(uint64_t bp_loc)
    {
        owner_.write_memory(bp_loc, AARCH64_BRK_INSN);
    }

    void restore_all_sw_sites()
    {
        for (const auto& [addr, orig] : sw_breakpoints_)
        {
            if (owner_.pid() != 0)
            {
                ptrace(PTRACE_POKEDATA, owner_.pid(), reinterpret_cast<void*>(addr), reinterpret_cast<void*>(orig));
            }
        }
        sw_breakpoints_.clear();
        bp_id_to_addr_.clear();
    }

    // --- HW support (thin over Registers name-based views + write_back via owner) ---

    int enable_hw_breakpoint(uint64_t addr)
    {
        if (owner_.pid() == 0)
        {
            Error::send("Process not initialized");
        }
        auto& regs = owner_.get_registers();
        regs.load();

        int max_slots = num_hw_breakpoint_slots();
        if (max_slots <= 0)
            max_slots = 16;

        for (int slot = 0; slot < max_slots && slot < 16; ++slot)
        {
            std::string ctrl_name = "brk_ctrl" + std::to_string(slot);
            auto h_ctrl = regs.resolve(ctrl_name);
            uint64_t ctrl = regs.read(h_ctrl).value_or(0);
            if ((ctrl & 1u) == 0)
            {
                std::string addr_name = "brk_addr" + std::to_string(slot);
                auto h_addr = regs.resolve(addr_name);
                regs.write(h_addr, static_cast<__uint128_t>(addr));
                regs.write(h_ctrl, static_cast<__uint128_t>(make_hw_break_ctrl()));
                owner_.write_back_registers();
                return slot;
            }
        }
        Error::send("No free hardware breakpoint slots available");
        return -1;
    }

    void disable_hw_breakpoint(int slot)
    {
        if (owner_.pid() == 0)
            Error::send("Process not initialized");
        if (slot < 0 || slot > 15)
            Error::send("Invalid hardware breakpoint slot");

        auto& regs = owner_.get_registers();
        regs.load();

        std::string ctrl_name = "brk_ctrl" + std::to_string(slot);
        auto h_ctrl = regs.resolve(ctrl_name);
        uint64_t ctrl = regs.read(h_ctrl).value_or(0);
        ctrl &= ~1u;
        regs.write(h_ctrl, static_cast<__uint128_t>(ctrl));
        owner_.write_back_registers();
    }

    int num_hw_breakpoint_slots() const
    {
        try
        {
            return static_cast<int>(owner_.get_registers().hw_breakpoint_info() & 0xffu);
        }
        catch (...)
        {
            return 0;
        }
    }

    int num_hw_watchpoint_slots() const
    {
        try
        {
            return static_cast<int>(owner_.get_registers().hw_watchpoint_info() & 0xffu);
        }
        catch (...)
        {
            return 0;
        }
    }

private:
    static constexpr uint32_t AARCH64_BRK_INSN = 0xd4200000u;

    // HW control bit builders (moved from the old anon namespace in process.cpp)
    static constexpr uint32_t HWBRK_E   = 1u << 0;
    static constexpr uint32_t HWBRK_PMC = 0b11u << 1;
    static constexpr uint32_t HWBRK_BAS = 0xFu << 5;
    static constexpr uint32_t HWBRK_BT  = 0b0000u << 20;

    static uint32_t make_hw_break_ctrl()
    {
        return HWBRK_E | HWBRK_PMC | HWBRK_BAS | HWBRK_BT;
    }

    Process& owner_;

    std::unordered_map<uint64_t, uint64_t> sw_breakpoints_; // addr -> orig word
    int next_bp_id_ = 1;
    std::unordered_map<int, uint64_t> bp_id_to_addr_;
};

// --- Breakpoints public facade (pimpl forwarding) ---

Breakpoints::Breakpoints(Process& owner)
    : impl_(std::make_unique<Impl>(owner))
{}

Breakpoints::~Breakpoints() = default;

int Breakpoints::add_breakpoint(uint64_t addr)
{
    return impl_->add_breakpoint(addr);
}

bool Breakpoints::remove_breakpoint(int id)
{
    return impl_->remove_breakpoint(id);
}

bool Breakpoints::remove_breakpoint(uint64_t addr)
{
    return impl_->remove_breakpoint(addr);
}

std::vector<std::pair<int, uint64_t>> Breakpoints::get_breakpoints() const
{
    return impl_->get_breakpoints();
}

std::optional<uint64_t> Breakpoints::prepare_for_transparent_step_over(uint64_t current_pc)
{
    return impl_->prepare_for_transparent_step_over(current_pc);
}

void Breakpoints::rearm_and_rewind(uint64_t bp_loc)
{
    impl_->rearm_and_rewind(bp_loc);
}

int Breakpoints::enable_hw_breakpoint(uint64_t addr)
{
    return impl_->enable_hw_breakpoint(addr);
}

void Breakpoints::disable_hw_breakpoint(int slot)
{
    impl_->disable_hw_breakpoint(slot);
}

void Breakpoints::restore_all_sw_sites()
{
    impl_->restore_all_sw_sites();
}

int Breakpoints::num_hw_breakpoint_slots() const
{
    return impl_->num_hw_breakpoint_slots();
}

int Breakpoints::num_hw_watchpoint_slots() const
{
    return impl_->num_hw_watchpoint_slots();
}

} // namespace cbg
