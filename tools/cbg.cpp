#include<libcbg/libcbg.hpp>
#include <unistd.h>
#include <iostream>
namespace 
{
    pid_t attach(int argc, const char** argv);
}

int main(int argc, const char** argv)
{
    if (argc == 1)
    {
        std::cerr << "No arguments given\n";
        return -1;
    }

    pid_t pid = attach(argc, argv);
}