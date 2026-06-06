#include <libcbg/process.hpp>
#include <libcbg/error.hpp>
#include <sys/ptrace.h>
#include <sys/wait.h>
#include <unistd.h>
#include <iostream>
#include <ostream>
#include <optional>
#include <spdlog/spdlog.h>
#include <spdlog/fmt/ostr.h>
#include <fmt/format.h>
#include <libcbg/pipe.hpp>

// =============================================================================
// process.cpp - Process lifecycle, execution control, memory, and facade APIs
// =============================================================================

namespace
{
    // -------------------------------------------------------------------------
    // Anonymous helpers (launch child error reporting + pretty stop reason)
    // -------------------------------------------------------------------------

    void exit_with_perror(cbg::Pipe &channel, std::string const &prefix)
    {
        auto message = prefix + ": " + std::strerror(errno);
        channel.write(reinterpret_cast<std::byte *>(message.data()), message.size());
        exit(-1);
    }

    std::string print_stop_reason(cbg::stop_reason reason)
    {
        std::stringstream ss;
        switch (reason.reason)
        {
        case cbg::process_state::EXITED:
            ss << "Exited with status " << static_cast<int>(reason.info);
            break;
        case cbg::process_state::TERMINATED:
            ss << "Terminated with signal " << sigabbrev_np(reason.info);
            break;
        case cbg::process_state::STOPPED:
            ss << "Stopped by signal " << sigabbrev_np(reason.info);
            break;
        }
        return ss.str();
    }
}

std::unique_ptr<cbg::Process> cbg::Process::launch(const std::filesystem::path &path, bool debug, std::optional<int> stdout_replacement)
{
    Pipe channel(true);
    pid_t pid = fork();
    if (pid < 0)
    {
        Error::send_errno("Failed to fork process");
    }
    if (pid == 0) // in child process
    {
        channel.close_read();
        if (stdout_replacement.has_value())
        {
            if (dup2(*stdout_replacement, STDOUT_FILENO) < 0)
            {
                exit_with_perror(channel, "Failed to redirect stdout");
            }
        }

        if (debug && ptrace(PTRACE_TRACEME, 0, nullptr, nullptr) < 0)
        {
            exit_with_perror(channel, "Failed to trace debugee process");
        }
        
        if (execlp(path.c_str(), path.c_str(), nullptr) < 0)
        {
            exit_with_perror(channel, "Failed to execute debugee program");
        }
    }
    channel.close_write();
    auto data = channel.read();
    channel.close_read();

    if (data.size() > 0)
    {
        waitpid(pid, nullptr, 0);
        auto chars = reinterpret_cast<char *>(data.data());
        Error::send(std::string(chars, chars + data.size()));
    }
    auto proc = std::unique_ptr<Process>(new Process(pid, true, debug));
    if (debug)
    {
        proc->wait_on_signal();
    }
    return proc;
}

// -----------------------------------------------------------------------------
// Process lifecycle (attach + private ctor + dtor)
// -----------------------------------------------------------------------------

std::unique_ptr<cbg::Process> cbg::Process::attach(pid_t pid)
{
    if (pid <= 0)
    {
        Error::send("Invalid PID");
    }
    if (ptrace(PTRACE_ATTACH, pid, nullptr, nullptr) < 0)
    {
        Error::send_errno("Failed to attach to process");
    }

    auto proc = std::unique_ptr<Process>(new Process(pid, false, true));
    proc->wait_on_signal();
    return proc;
}

// Definition of the private ctor (was inline). We initialize the Breakpoints
// manager here after the Registers member is ready.
cbg::Process::Process(pid_t pid, bool terminate_on_end, bool is_attached)
    : pid_(pid), terminate_on_end_(terminate_on_end), is_attached(is_attached), registers_(new Registers(this->pid_))
{
    breakpoints_ = std::make_unique<Breakpoints>(*this);
}

cbg::Process::~Process()
{
    if (pid_ != 0)
    {
        int status;

        // Delegate SW breakpoint site restoration (best-effort) to the manager so that
        // we do not leave traps in the debugee.
        if (breakpoints_)
            breakpoints_->restore_all_sw_sites();

        if (is_attached)
        {
            if (state_ == process_state::RUNNING)
            {
                kill(pid_, SIGSTOP);
                waitpid(pid_, &status, 0);
            }

            ptrace(PTRACE_DETACH, pid_, nullptr, nullptr);
            kill(pid_, SIGCONT);
        }

        if (terminate_on_end_)
        {
            kill(pid_, SIGKILL);
            waitpid(pid_, &status, 0);
        }
    }
}

// -----------------------------------------------------------------------------
// Execution control (resume, stop_reason, wait_on_signal, step_instruction)
// -----------------------------------------------------------------------------

void cbg::Process::resume()
{
    spdlog::debug("Resuming process with PID {}", pid_);
    if (pid_ == 0)
    {
        Error::send("Process not initialized");
    }
    if (ptrace(PTRACE_CONT, pid_, nullptr, nullptr) < 0)
    {
        Error::send_errno("Failed to continue process");
    }
    state_ = process_state::RUNNING;
    spdlog::debug("Process state after resume: {}", static_cast<int>(state_));
}

// -----------------------------------------------------------------------------
// stop_reason construction (from waitpid status)
// -----------------------------------------------------------------------------

cbg::stop_reason::stop_reason(int wait_status)
{
    if (WIFEXITED(wait_status))
    {
        reason = process_state::EXITED;
        info = WEXITSTATUS(wait_status);
    }
    else if (WIFSIGNALED(wait_status))
    {
        reason = process_state::TERMINATED;
        info = WTERMSIG(wait_status);
    }
    else if (WIFSTOPPED(wait_status))
    {
        reason = process_state::STOPPED;
        info = WSTOPSIG(wait_status);
    }
}

// wait_on_signal + transparent SW breakpoint step-over handling (delegates to Breakpoints)
cbg::stop_reason cbg::Process::wait_on_signal()
{
    spdlog::debug("Waiting for process to be signalled");

    int wait_status;
    int options = 0;
    if (waitpid(pid_, &wait_status, options) < 0)
    {
        Error::send_errno("Failed to wait for process");
    }
    stop_reason reason(wait_status);
    if (reason.reason == process_state::EXITED || reason.reason == process_state::TERMINATED)
    {
        pid_ = 0;
        std::cout << "Process exited" << std::endl;
        return reason;
    }
    state_ = reason.reason;
    spdlog::debug("{}", reason);
    if (is_attached && state_ == process_state::STOPPED)
    {
        read_all_registers();

        // Delegate transparent SW breakpoint step-over (restore, single-step, re-arm, PC rewind)
        // to the Breakpoints manager. If it performed work we re-read registers so callers see
        // the rewound PC.
        if (auto opt_loc = breakpoints_->prepare_for_transparent_step_over(
                get_registers().get_register(get_registers().lookup("pc")).get<uint64_t>()))
        {
            // Owner performs the raw SINGLESTEP + waitpid for the inner step.
            if (ptrace(PTRACE_SINGLESTEP, pid_, nullptr, nullptr) < 0)
            {
                write_memory(*opt_loc, 0xd4200000u); // best effort
                Error::send_errno("Failed to single-step over software breakpoint");
            }
            int ws;
            if (waitpid(pid_, &ws, 0) < 0)
            {
                write_memory(*opt_loc, 0xd4200000u);
                Error::send_errno("Failed to wait after single-step over software breakpoint");
            }

            breakpoints_->rearm_and_rewind(*opt_loc);
            read_all_registers();
        }
    }
    return reason;
}

// step_instruction lives in execution control because it performs a controlled
// single step (with transparent bp handling) and waits for the result.
cbg::stop_reason cbg::Process::step_instruction()
{
    if (pid_ == 0)
    {
        Error::send("Process not initialized");
    }

    get_registers().load();
    auto pc_d = get_registers().lookup("pc");
    uint64_t pc = get_registers().get_register(pc_d).get<uint64_t>();

    bool was_at_bp = false;
    uint64_t bp_loc = 0;
    uint64_t orig = 0;

    // Ask the Breakpoints manager whether we are at a SW site and need a temp restore.
    if (auto opt_loc = breakpoints_->prepare_for_transparent_step_over(pc))
    {
        // We were at a bp site; the manager already restored the original instruction.
        was_at_bp = true;
        bp_loc = *opt_loc;
    }

    if (ptrace(PTRACE_SINGLESTEP, pid_, nullptr, nullptr) < 0)
    {
        if (was_at_bp)
            write_memory(bp_loc, 0xd4200000u); // best-effort re-arm on failure path
        Error::send_errno("Failed to single-step");
    }
    state_ = process_state::RUNNING;

    auto reason = wait_on_signal();

    if (was_at_bp)
    {
        // Re-arm + rewind is handled by the manager (it will also do the PC write + write_back).
        breakpoints_->rearm_and_rewind(bp_loc);
    }

    return reason;
}

// -----------------------------------------------------------------------------
// Register load/save (thin wrappers over Registers)
// -----------------------------------------------------------------------------

void cbg::Process::read_all_registers() 
{
    get_registers().load();
}

void cbg::Process::write_back_registers()
{
    if (registers_)
        registers_->save();
}

// -----------------------------------------------------------------------------
// Memory access (ptrace PEEK/POKE)
// -----------------------------------------------------------------------------

uint64_t cbg::Process::read_memory(uint64_t addr)
{
    if (pid_ == 0)
    {
        Error::send("Process not initialized");
    }
    errno = 0;
    long word = ptrace(PTRACE_PEEKDATA, pid_, reinterpret_cast<void *>(addr), nullptr);
    if (word == -1 && errno != 0)
    {
        Error::send_errno("Failed to read memory at 0x" + std::to_string(addr));
    }
    return static_cast<uint64_t>(word);
}

void cbg::Process::write_memory(uint64_t addr, uint64_t value)
{
    if (pid_ == 0)
    {
        Error::send("Process not initialized");
    }
    if (ptrace(PTRACE_POKEDATA, pid_, reinterpret_cast<void *>(addr), reinterpret_cast<void *>(value)) == -1)
    {
        Error::send_errno("Failed to write memory at 0x" + std::to_string(addr));
    }
}

// -----------------------------------------------------------------------------
// Breakpoint API delegations (facade kept for API compatibility; impl in Breakpoints)
// -----------------------------------------------------------------------------

int cbg::Process::add_breakpoint(uint64_t addr)
{
    return breakpoints_->add_breakpoint(addr);
}

bool cbg::Process::remove_breakpoint(int id)
{
    return breakpoints_->remove_breakpoint(id);
}

bool cbg::Process::remove_breakpoint(uint64_t addr)
{
    return breakpoints_->remove_breakpoint(addr);
}

std::vector<std::pair<int, uint64_t>> cbg::Process::get_breakpoints() const
{
    return breakpoints_->get_breakpoints();
}

int cbg::Process::enable_hw_breakpoint(uint64_t addr)
{
    return breakpoints_->enable_hw_breakpoint(addr);
}

void cbg::Process::disable_hw_breakpoint(int slot)
{
    breakpoints_->disable_hw_breakpoint(slot);
}

int cbg::Process::num_hw_breakpoint_slots() const
{
    return breakpoints_->num_hw_breakpoint_slots();
}

int cbg::Process::num_hw_watchpoint_slots() const
{
    return breakpoints_->num_hw_watchpoint_slots();
}

// Formattable support for cbg::stop_reason with spdlog/fmt
namespace fmt
{
    template <>
    struct formatter<cbg::stop_reason> : fmt::formatter<std::string>
    {
        auto format(cbg::stop_reason stop_reason, format_context &ctx) const -> decltype(ctx.out())
        {
            return format_to(ctx.out(), "Reason: {}", print_stop_reason(stop_reason));
        }
    };
}