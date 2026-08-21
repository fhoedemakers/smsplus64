#ifndef _PROFILE_H_
#define _PROFILE_H_
#include <stdint.h>
typedef enum { PROF_Z80=0, PROF_RENDER, PROF_BLIT, PROF_AUDIO, PROF_IDLE, PROF_SYNC, PROF_COUNT } prof_slot_t;
extern uint32_t prof_acc[PROF_COUNT];
#define PROF_BEGIN(slot) do{}while(0)
#define PROF_END(slot)   do{}while(0)
#endif
