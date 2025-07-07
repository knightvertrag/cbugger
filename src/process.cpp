#include <libcbg/process.hpp>
#include <libcbg/error.hpp>
#include <sys/ptrace.h>
#include <sys/wait.h>
#include <unistd.h>
#include <iostream>
#include <ostream>
#include <spdlog/spdlog.h>
#include <spdlog/fmt/ostr.h>
#include <fmt/format.h>
#include <libcbg/pipe.hpp>

namespace
{
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

std::unique_ptr<cbg::Process> cbg::Process::launch(const std::filesystem::path &path)
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
        if (ptrace(PTRACE_TRACEME, 0, nullptr, nullptr) < 0)
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
    auto proc = std::unique_ptr<Process>(new Process(pid, false));
    proc->wait_on_signal();
    return proc;
}

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

    auto proc = std::unique_ptr<Process>(new Process(pid, false));
    proc->wait_on_signal();
    return proc;
}

cbg::Process::~Process()
{
    if (pid_ != 0)
    {
        int status;
        if (state_ == process_state::RUNNING)
        {
            kill(pid_, SIGSTOP);
            waitpid(pid_, &status, 0);
        }

        ptrace(PTRACE_DETACH, pid_, nullptr, nullptr);
        kill(pid_, SIGKILL);
        if (terminate_on_end_)
        {
            kill(pid_, SIGKILL);
            waitpid(pid_, &status, 0);
        }
    }
}

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
    state_ = reason.reason;

    spdlog::debug("{}", reason);
    return reason;
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