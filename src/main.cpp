#include "debugger.hpp"

#include "linenoise.h"

#include <iostream>
#include <unistd.h>
#include <sys/wait.h>

using namespace venus;

void debugger::run()
{
    int wait_status;
    auto options = 0;
    waitpid(m_pid, &wait_status, options);

    char *line = nullptr;
    while ((line = linenoise("venus> ")) != nullptr)
    {
        handle_command(line);
        linenoiseHistoryAdd(line);
        linenoiseFree(line);
    }
}

void debugger::handle_command(const std::string &line)
{
}

int main(int argc, char **argv)
{
    if (argc < 2)
    {
        std::cerr << "Program name not specified" << std::endl;
        return -1;
    }
    auto prog = argv[1];
    auto pid = fork();
    if (pid == 0)
    {
    }
    else if (pid >= 1)
    {
    }
}
