#include <iostream>
#include <unistd.h>
#include "debugger.h"
#include <sys/wait.h>

void debugger::run()
{
    int wait_status;
    auto options = 0;
    waitpid(m_pid, &wait_status, options);

    char *line = nullptr;
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
