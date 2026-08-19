#ifndef _PROFILE_H_
#define _PROFILE_H_

/*
    Lightweight per-phase CPU profiler.

    Costs roughly 1000 COP0 count reads per frame (<0.2%), so it is always
    compiled in; only the on-screen display is toggled at runtime (Z + C-Up).
    Slots accumulate VR4300 ticks for one second, then frameratecalc() folds
    them into whole-percent figures and clears the accumulators.
*/

#include <stdint.h>
#include <n64sys.h>

#ifdef __cplusplus
extern "C"
{
#endif

    typedef enum
    {
        PROF_Z80 = 0,  /* z80_execute()                                  */
        PROF_RENDER,   /* render_line() - background + sprites           */
        PROF_BLIT,     /* cache writeback + RDP display list build       */
        PROF_AUDIO,    /* SN76496Update() + audio_push()                 */
        PROF_IDLE,     /* blocked pacing: audio_push, or the tick limiter    */
        PROF_SYNC,     /* blocked on display_get / rspq_wait                  */
        PROF_COUNT
    } prof_slot_t;

    extern uint32_t prof_acc[PROF_COUNT];   /* ticks accumulated this second */
    extern uint8_t prof_shown[PROF_COUNT];  /* percent of the last full second */

/* PROF_BEGIN declares a local holding the start tick, so a matching pair must
   sit in the same block. Wrap each region in braces when the same slot is
   timed more than once inside one function. */
#define PROF_BEGIN(slot) uint32_t __pt_##slot = TICKS_READ()
#define PROF_END(slot) prof_acc[slot] += (uint32_t)TICKS_SINCE(__pt_##slot)

#ifdef __cplusplus
}
#endif

#endif /* _PROFILE_H_ */
