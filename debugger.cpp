#include "include/debugger.h"
#include <sys/wait.h>
void debugger::run()
{
    int wait_status;
    auto options = 0;
    waitpid(m_pid, &wait_status, options);

    char *line = nullptr;
}