#include <catch2/catch_test_macros.hpp>
#include <fstream>
#include <libcbg/process.hpp>
#include <libcbg/error.hpp>
#include <sys/types.h>
#include <signal.h>

using namespace cbg;

namespace
{
    bool process_exists(pid_t pid)
    {
        auto ret = kill(pid, 0);
        return ret != -1 and errno != ESRCH;
    }

    char get_process_status(pid_t pid)
    {
        std::ifstream stat("/proc/" + std::to_string(pid) + "/stat");
        std::string data;
        std::getline(stat, data);
        auto last_parenthesis_index = data.rfind(')');
        auto status_indicator_index = last_parenthesis_index + 2;
        return data[status_indicator_index];
    }
}
TEST_CASE("Process::launch sucess", "[process]")
{
    auto proc = Process::launch("yes", false);
    REQUIRE(process_exists(proc->pid()));
}

TEST_CASE("Process::launch no such program", "[process]")
{
    REQUIRE_THROWS_AS(Process::launch("this_program_does_not_exist"), Error);
}

TEST_CASE("Process::attach sucess", "[process]")
{
    auto target = Process::launch("targets/run_endlessly", false);
    auto proc = Process::attach(target->pid());
    REQUIRE(get_process_status(target->pid()) == 't');
}

TEST_CASE("Process::attach invalid pid", "[process]")
{
    REQUIRE_THROWS_AS(Process::attach(-1), Error);
}

TEST_CASE("Process::resume sucess", "[process]")
{
    {
        auto proc = Process::launch("targets/run_endlessly");
        proc->resume();
        auto status = get_process_status(proc->pid());
        auto sucess = status == 'R' || status == 'S';
        REQUIRE(sucess);
    }

    {
        auto target = Process::launch("targets/run_endlessly", false);
        auto proc = Process::attach(target->pid());
        proc->resume();
        auto status = get_process_status(proc->pid());
        auto sucess = status == 'R' || status == 'S';
        REQUIRE(sucess);
    }
}

TEST_CASE("Process::resume already terminated", "[process]")
{
    auto proc = Process::launch("targets/end_immediately");
    proc->resume();
    proc->wait_on_signal();
    REQUIRE_THROWS_AS(proc->resume(), Error);

}