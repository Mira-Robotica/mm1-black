// Included by the vendored sh2.c after its private types.
#include "sh2_lab.h"
static sh2_LabBootDiagnostics labBoot;
static sh2_LabCommandDiagnostics labCommand;
static uint32_t labControlBaseline, labUnknownBaseline, labTruncatedBaseline;
static bool labActive;
static uint32_t labStarted, labTimeout;
static uint8_t labMask;
static void labExpire(sh2_t *pSh2)
{
    if (labActive && pSh2->pOp &&
        (uint32_t)(pSh2->pHal->getTimeUs(pSh2->pHal)-labStarted) >= labTimeout) {
        pSh2->pOp = 0;
        pSh2->opStatus = SH2_ERR_TIMEOUT;
    }
}
