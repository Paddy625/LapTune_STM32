#include "timebase.h"
#include <string.h>
static PPS_Capture pulses[4];
static unsigned sequence;
void PPSTest_Clear(void) { memset(pulses,0,sizeof(pulses)); sequence=0; }
void PPSTest_Capture(uint64_t us)
{ ++sequence; pulses[sequence%4]=(PPS_Capture){us,sequence,0}; }
uint8_t Timebase_PPSGet(uint32_t seq,PPS_Capture *out)
{ *out=pulses[seq%4]; return seq && out->seq==seq; }
uint8_t Timebase_PPSBefore(uint64_t at,PPS_Capture *out)
{
    for (unsigned i=0;i<4 && i<sequence;++i)
        if (Timebase_PPSGet(sequence-i,out) && out->t_us<=at) return 1;
    return 0;
}
