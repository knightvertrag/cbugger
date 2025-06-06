#include <libcbg/libcbg.hpp>
#include <unistd.h>
#include <iostream>
#include <sstream>
#include <vector>
#include <sys/ptrace.h>
#include <sys/wait.h>
#include <editline/readline.h>

namespace
{
    std::vector<std::string> split(std::string_view str, char delimiter)
    {
        std::vector<std::string> res{};
        std::stringstream ss {std::string(str)};
        std::string item;

        while (std::getline(ss, item, delimiter))
        {
            res.push_back(item);
        }
        return res;
    }

    bool is_prefix(std::string_view str, std::string_view of)
    {
        if (str.size() > of.size())
            return false;
        return std::equal(str.begin(), str.end(), of.begin());
    }

    void resume(pid_t pid) 
    {
        if (ptrace(PTRACE_CONT, pid, nullptr, nullptr) < 0)
        {   
            std::perror("Failed to continue process");
            std::exit(-1);
        }

    }
    void wait_on_signal(pid_t pid)
    {
        int wait_status;
        int options = 0;
        if (waitpid(pid, &wait_status, options) < 0)
        {
            std::perror("Failed to wait for process");
            std::exit(-1);
        }
    }
    pid_t attach(int argc, const char **argv)
    {
        pid_t pid = 0;
        if (argc == 3 and argv[1] == std::string_view("-p"))
        {
            pid = std::atoi(argv[2]);
            if (pid <= 0)
            {
                std::cerr << "Invalid PID: \n";
                return -1;
            }
            if (ptrace(PTRACE_ATTACH, pid, nullptr, nullptr) < 0)
            {
                std::perror("Failed to attach to process");
                return -1;
            }
        }
        else
        {
            const char *program_path = argv[1];
            if ((pid = fork()) < 0)
            {
                std::perror("Failed to fork");
                return -1;
            }
            if (pid == 0)
            {
                if (ptrace(PTRACE_TRACEME, 0, nullptr, nullptr) < 0)
                {
                    std::perror("Failed to trace process");
                    return -1;
                }
                if (execlp(program_path, program_path, nullptr) < 0)
                {
                    std::perror("Failed to execute program");
                    return -1;
                }
            }
        }

        return pid;
    }
    void handle_command(pid_t pid, std::string_view line)
    {
        auto args = split(line, ' ');
        auto command = args[0];
        if (is_prefix(command, "continue"))
        {
            resume(pid);
            wait_on_signal(pid);
        }
        else 
        {
            std::cerr << "Unknow command\n";
        }
    }

}

int main(int argc, const char **argv)
{
    if (argc == 1)
    {
        std::cerr << "Usage: " << argv[0] << " -p <pid> | --self\n";
        return -1;
    }

    pid_t pid = attach(argc, argv);

    int wait_status;
    int options = 0;
    if (waitpid(pid, &wait_status, options) < 0)
    {
        std::perror("Failed to wait for process");
    }

    char *line = nullptr;
    while ((line = readline("sdb> ")) != nullptr)
    {
        std::string line_str;
        if (line == std::string_view(""))
        {
            free(line);
            if (history_length > 0)
                line_str = history_get(history_length - 1)->line;
        }
        else
        {
            line_str = line;
            add_history(line);
            free(line);
        }
        if (!line_str.empty())
        {
            handle_command(pid, line_str);
        }
    }
}