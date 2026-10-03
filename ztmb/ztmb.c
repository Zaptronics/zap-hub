#include "ztmb/ztmb.h"
#include "zaptronics/boards/ztpico2/board.h"

static ztpico2_t g_board;
int ztmb_init(void)
{
    const ztpico2_config_t config = { .adc_reference_mv = 3300u };
    int rc = ztpico2_init(&g_board, &config);
    log_printf("ZTPICO2 initialization: stage=%u rc=%d\n", (unsigned)g_board.stage, rc);
    return rc;
}
