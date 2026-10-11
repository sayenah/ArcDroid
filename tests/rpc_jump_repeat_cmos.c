/* D-155: repeated actual CMOS fault path. GPL-2.0-only. */
#define main component_main
#include "rpc_cmos_test.c"
#undef main
static unsigned iteration;
int main(void)
{
    cmos_init();
    for(iteration=0;iteration<1000;++iteration) {
        reseti2c(I2C_PCF8583); epoch=UINT64_MAX;
        if(!setjmp(boundary)) { (void)read_reg(1); assert(0); }
        reseti2c(I2C_PCF8583); epoch=elapsed=0; assert(read_reg(2)==0);
    }
    puts("jump repeat: CMOS invalid RTC fault 1000 times"); return 0;
}
