#pragma once

#include <filesystem>
#include <memory>
#include <sys/types.h>
#include <cstdint>
#include <optional>
#include <libcbg/registers.hpp>

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
        bool is_attached = true;
        process_state state_ = process_state::STOPPED;
        std::unique_ptr<Registers> registers_ = nullptr;

        Process(pid_t pid, bool terminate_on_end, bool is_attached)
            : pid_(pid), terminate_on_end_(terminate_on_end), is_attached(is_attached), registers_(new Registers(this->pid_)) {}
        
        void read_all_registers();

    public:
        static std::unique_ptr<Process> launch(const std::filesystem::path &path,
                                               bool debug = true,
                                               std::optional<int> stdout_replacement = std::nullopt);

        static std::unique_ptr<Process> attach(pid_t pid);

        void resume();
        stop_reason wait_on_signal();
        pid_t pid() const { return pid_; }
        process_state state() const { return state_; }

        Registers &get_registers() { return *registers_; }
        const Registers &get_registers() const { return *registers_; }
        void write_back_registers();

        Process() = delete;
        Process(const Process &) = delete;
        Process &operator=(const Process &) = delete;

        ~Process();
    };

} // namespace cbg
