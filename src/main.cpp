#include "debugger.hpp"

#include <iostream>

#include <unistd.h>

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
        // Inside child process
        cbugger::execute_debugee(prog);
    }
    else if (pid >= 1)
    {
        // Inside parent process
        std::cout << "Started debugging process " << pid << "\n";
        cbugger::debugger dbg(prog, pid);
        dbg.run();
    }
}
