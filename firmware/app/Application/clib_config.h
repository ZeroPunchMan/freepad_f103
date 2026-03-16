#pragma once

#include "cl_common.h"
#include "stdio.h"

#ifdef __cplusplus
extern "C"
{
#endif

    //-------------event type------------------
    typedef enum
    {
        CL_Event_Button = 0, // session = ButtonIdx_t, eventArg = ButtonEvent_t*
        CL_EventMax,
    } CL_Event_t;

//---------------log-------------------------
#include "stdio.h"
#define CL_PRINTF printf

#define CL_LOG_LEVEL_INFO
#define CL_LOG_LEVEL_WARN
#define CL_LOG_LEVEL_ERROR

#ifdef __cplusplus
}
#endif
