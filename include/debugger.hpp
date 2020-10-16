#ifndef DEBUGGER_HPP
#define DEBUGGER_HPP
#include <unordered_map>
#include <cstdint>
#include <signal.h>
#include <fcntl.h>

#include "dwarf/dwarf++.hh"
#include "elf/elf++.hh"

class debugger
{
private:
    std::string m_program_name;
    pid_t m_pid;

public:
    debugger(std::string prog_name, pid_t pid) : m_program_name{std::move(prog_name)}, m_pid{pid} {}

    void run();
};
#endif