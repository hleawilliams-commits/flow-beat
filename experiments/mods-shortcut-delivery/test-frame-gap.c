#include <assert.h>
#include <stdio.h>
#include "frame-gap.h"
int main(void)
{
    assert(!delivery_gap_blocks(0,100,101));
    assert(delivery_gap_blocks(1,100,100));
    assert(delivery_gap_blocks(1,100,1100));
    assert(delivery_gap_blocks(1,100,1349));
    assert(!delivery_gap_blocks(1,100,1350));
    assert(!delivery_gap_blocks(1,100,1351));
    assert(!delivery_gap_blocks(1,100,99));
    puts("PASS: simulated1250ms frame-gap boundaries");
}
