// =============================================================================
// run_endlessly.cpp - test target: infinite loop (for attach / stepping tests)
// =============================================================================

int main()   // the whole program is the busy loop
{
    volatile int i = 0;
    while(true) i = 69;
}