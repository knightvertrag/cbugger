#ifndef DEBUGGER_HPP
#define DEBUGGER_HPP
#include <unordered_map>
#include <cstdint>
#include <signal.h>
#include <fcntl.h>

#include "dwarf/dwarf++.hh"
#include "elf/elf++.hh"

namespace venus
{
    class debugger
    {
    private:
        std::string m_program_name;
        pid_t m_pid;
        void handle_command(const std::string &line);

    public:
        debugger(std::string prog_name, pid_t pid) : m_program_name{std::move(prog_name)}, m_pid{pid} {}

        void run();
    };
} // namespace venus
#endif