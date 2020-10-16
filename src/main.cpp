#include <iostream>
#include <unistd.h>

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
