/* Full-context equivalence for outlined FP32 operations with register aliases.
   The unmodified inline ABI 5 translation is the reference. */
#include "rt_soft.h"
#include "outline_ops.h"
#include <stdio.h>
#include <string.h>

typedef void (*FpxOpFn)(GuestContext*);
extern const FpxOpFn g_outline_soft[], g_outline_fpx[];
extern int fpx_env_shim(void);
unsigned long long g_fpx_probe[3];

static uint64_t random_state = UINT64_C(0x9267ad0185e43b6f);
static uint32_t Next(void) {
    random_state ^= random_state << 13;
    random_state ^= random_state >> 7;
    random_state ^= random_state << 17;
    return (uint32_t)random_state;
}

int main(void) {
    const uint64_t modes[] = {0, UINT64_C(1)<<24, UINT64_C(1)<<25,
        UINT64_C(1)<<22, UINT64_C(2)<<22, UINT64_C(3)<<22,
        (UINT64_C(1)<<24)|(UINT64_C(1)<<25), UINT64_C(1)<<32};
    const uint32_t edges[] = {0, 0x80000000u, 1, 0x807fffffu,
        0x00800000u, 0x00800001u, 0x3f800000u, 0x3f800001u, 0x33800000u,
        0xbf800000u, 0x7f7fffffu, 0xff7fffffu, 0x7f800000u, 0xff800000u,
        0x7fc01234u, 0x7f801234u, 0xff801234u};
    unsigned long long checks = 0;
    fpx_env_shim(); // satisfy the production FPX1 host contract
    for (unsigned op=0; op<OUTLINE_NOPS; ++op) {
        for (unsigned mode=0; mode<sizeof(modes)/sizeof(modes[0]); ++mode) {
            for (unsigned initial=0; initial<2; ++initial) {
                for (unsigned test=0; test<2048; ++test) {
                    GuestContext soft, fpx;
                    memset(&soft, 0x5a, sizeof soft);
                    uint32_t values[16];
                    for (unsigned lane=0; lane<16; ++lane) {
                        values[lane] = test < 289
                            ? edges[(test/(lane%2?17:1)+lane/2)%17] : Next();
                    }
                    // Different lanes can set sticky IXC and open the inline
                    // native gate partway through one vector operation.
                    memcpy(soft.vreg, values, sizeof values);
                    soft.fpcr = modes[mode];
                    soft.fpsr = 0x08000001u | (initial ? 16u : 0u);
                    fpx = soft;
                    g_outline_soft[op](&soft);
                    g_outline_fpx[op](&fpx);
                    ++checks;
                    if (memcmp(&soft, &fpx, sizeof soft)) {
                        fprintf(stderr, "OUTLINE alias mismatch op=%u mode=%u initial=%u case=%u\n",
                                op, mode, initial, test);
                        return 1;
                    }
                }
            }
        }
    }
    if (!g_fpx_probe[1] || !g_fpx_probe[2]) {
        fprintf(stderr, "OUTLINE alias test did not exercise both native and exact paths\n");
        return 1;
    }
    printf("OUTLINE aliases: %llu whole-context checks passed; native=%llu exact=%llu\n",
           checks, g_fpx_probe[1], g_fpx_probe[2]);
    return 0;
}
