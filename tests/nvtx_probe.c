/* Phase 1 gate P1.5 helper.

   Emits three NVTX ranges with names that are unlikely to collide with anything
   else in a trace, so scripts/verify_nvtx.{sh,ps1} can grep an nsys report for
   them. This binary proves nothing on its own -- it must be run under
   `nsys profile`. Running it bare only confirms the calls do not crash.

   Exits 77 (CTest "skipped") when NVTX was not compiled in. */

#include "gpuseal/telemetry.h"

#include <stdio.h>

#define SKIP_EXIT 77

/* Ranges must be long enough that the sampler in nsys cannot miss them, and
   named distinctly enough to grep for. */
#define RANGE_SECONDS 0.25

static void busy_wait(double seconds)
{
    const double end = gpuseal_now() + seconds;
    while (gpuseal_now() < end) { /* spin: nsys needs wall time inside the range */ }
}

int main(void)
{
#if !defined(GPUSEAL_HAVE_NVTX)
    printf("SKIP: built without NVTX headers, nothing to trace\n");
    return SKIP_EXIT;
#else
    printf("emitting NVTX ranges: gpuseal_p15_outer / _inner / _sibling\n");

    gpuseal_nvtx_push("gpuseal_p15_outer");
    busy_wait(RANGE_SECONDS);

    /* Nested, to prove push/pop pairing survives depth. */
    gpuseal_nvtx_push("gpuseal_p15_inner");
    busy_wait(RANGE_SECONDS);
    gpuseal_nvtx_pop();

    gpuseal_nvtx_pop();

    /* Sequential sibling, to prove the stack unwound rather than leaking depth. */
    gpuseal_nvtx_push("gpuseal_p15_sibling");
    busy_wait(RANGE_SECONDS);
    gpuseal_nvtx_pop();

    printf("done\n");
    return 0;
#endif
}
