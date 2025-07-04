#pragma once

#include <filesystem>
#include <memory>
#include <sys/types.h>
#include <cstdint>

namespace cbg
{
    enum class process_state
    {
        RUNNING,
        STOPPED,
        EXITED,
        TERMINATED,
    };

    struct stop_reason
    {
        stop_reason(int wait_status);
        process_state reason;
        uint8_t info;
    };

    class Process
    {
    private:
        pid_t pid_ = 0;
        bool terminate_on_end_ = true;
        process_state state_ = process_state::STOPPED;

        Process(pid_t pid, bool terminate_on_end)
            : pid_(pid), terminate_on_end_(terminate_on_end) {}

    public:
        static std::unique_ptr<Process> launch(const std::filesystem::path &path);
        static std::unique_ptr<Process> attach(pid_t pid);

        void resume();
        stop_reason wait_on_signal();
        pid_t pid() const { return pid_; }
        process_state state() const { return state_; }

        Process() = delete;
        Process(const Process &) = delete;
        Process &operator=(const Process &) = delete;

        ~Process();
    };

} // namespace cbg
