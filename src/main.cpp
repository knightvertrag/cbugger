#include "debugger.hpp"

#include "linenoise.h"

#include <iostream>
#include <sstream>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/ptrace.h>

using namespace cbugger;

std::vector<std::string> split(const std::string &line, char delimiter)
{
    std::vector<std::string> res{};
    std::stringstream ss{line};
    std::string word;
    while (std::getline(ss, word, delimiter))
    {
        res.push_back(word);
    }
    return res;
}

bool is_prefix(const std::string &s, const std::string &of)
{
    if (s.size() > of.size())
        return false;
    return std::equal(s.begin(), s.end(), of.begin());
}

void debugger::run()
{
    int wait_status;
    auto options = 0;
    waitpid(m_pid, &wait_status, options);

    char *line = nullptr;
    while ((line = linenoise("cbugger> ")) != nullptr)
    {
        handle_command(line);
        linenoiseHistoryAdd(line);
        linenoiseFree(line);
    }
}

void debugger::continue_execution()
{
    ptrace(PTRACE_CONT, m_pid, nullptr, nullptr);

    int wait_status;
    auto options = 0;
    waitpid(m_pid, &wait_status, options);
}

void execute_debugee(const std::string &prog_name)
{
    if (ptrace(PTRACE_TRACEME, 0, 0, 0) < 0)
    {
        std::cerr << "Error in ptrace\n";
        return;
    }
    execl(prog_name.c_str(), prog_name.c_str(), nullptr);
}

void debugger::handle_command(const std::string &line)
{
    auto args = split(line, ' ');
    auto command = args[0];

    if (is_prefix(command, "cont"))
    {
        continue_execution();
    }
    else
    {
        std::cerr << "Unknown command\n";
    }
}

int main(int argc, char **argv)
{
    if (argc < 2)
    {
        std::cerr << "Program name not specified\n";
        return -1;
    }
    auto prog = argv[1];
    auto pid = fork();
    if (pid == 0)
    {
        execute_debugee(prog);
    }
    else if (pid >= 1)
    {
        std::cout << "Started debugging process " << pid << "\n";
        debugger dbg(prog, pid);
        dbg.run();
    }
}
