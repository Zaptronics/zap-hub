#include "ztmb/io_exp.h"



pcal6416a_t ioexp;


bool io_exp_init(i2c_inst_t *i2c)
{
    bool ok = pcal6416a_init(&ioexp, i2c, PCAL6416A_ADDR_BASE); // ADDR=0 -> 0x20
    if (!ok) return false;
    return true;
}
