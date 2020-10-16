#ifndef debugger_H
#define debugger_H
#include <string>

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