#include <catch2/catch_test_macros.hpp>
#include <libcbg/process.hpp>
#include <libcbg/error.hpp>
#include "test_common.hpp"

using namespace cbg;

// =============================================================================
// Process lifecycle (launch, attach, resume, error paths)
// =============================================================================

TEST_CASE("Process::launch success", "[process]")
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
