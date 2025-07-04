#include <unistd.h>
#include <iostream>
#include <sstream>
#include <vector>
#include <sys/ptrace.h>
#include <sys/wait.h>
#include <editline/readline.h>
#include <spdlog/spdlog.h>
#include <spdlog/sinks/basic_file_sink.h>
#include <string.h>
#include <libcbg/process.hpp>
#include <libcbg/error.hpp>

namespace
{
    std::vector<std::string> split(std::string_view str, char delimiter)
    {
        std::vector<std::string> res{};
        std::stringstream ss{std::string(str)};
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

    std::unique_ptr<cbg::Process> attach(int argc, const char **argv)
    {
        pid_t pid = 0;
        if (argc == 3 and argv[1] == std::string_view("-p"))
        {
            pid = std::atoi(argv[2]);
            return cbg::Process::attach(pid);
        }
        else
        {
            const char *program_path = argv[1];
            return cbg::Process::launch(program_path);
        }
    }
    void handle_command(std::unique_ptr<cbg::Process> &process, std::string_view line)
    {
        auto args = split(line, ' ');
        auto command = args[0];
        if (is_prefix(command, "continue"))
        {
            process->resume();
            auto reason = process->wait_on_signal();
            // print_stop_reason(*process, reason);
        }
        else
        {
            std::cerr << "Unknow command\n";
        }
    }

}

void main_loop(std::unique_ptr<cbg::Process> &process)
{
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
            try
            {
                handle_command(process, line_str);
            }
            catch (const cbg::Error &e)
            {
                std::cerr << e.what() << '\n';
            }
        }
    }
}

int main(int argc, const char **argv)
{
    // auto file_logger = spdlog::basic_logger_mt("file_logger", "logs/output.log");
    // spdlog::set_default_logger(file_logger);
    spdlog::set_level(spdlog::level::debug);
    // spdlog::set_pattern("[%H:%M:%S %z] [%n] [%^---%L---%$] [thread %t] %v");
    if (argc == 1)
    {
        std::cerr << "Usage: " << argv[0] << " -p <pid> | --self\n";
        return -1;
    }

    try
    {
        auto process = attach(argc, argv);
        main_loop(process);
    }
    catch (const cbg::Error &e)
    {
        std::cout << e.what() << "\n";
    }
}