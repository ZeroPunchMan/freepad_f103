#include "button.h"
#include "board.h"
#include "main.h"
#include "systime.h"
#include "string.h"
#include "cl_log.h"
#include "cl_event_system.h"
#include "pad_func.h"

//********************button define*****************************

typedef bool (*BtnGetPress)(void);

typedef struct
{
    BtnGetPress getPress;
} ButtonDef_t;

static bool IsPairPress(void)
{ 
    return LL_GPIO_IsInputPinSet(BTN_PAIR_PORT, BTN_PAIR_PIN);
}

static bool IsXPress(void)
{ 
    return LL_GPIO_IsInputPinSet(BTN_X_PORT, BTN_X_PIN);
}

static bool IsYPress(void)
{
    return LL_GPIO_IsInputPinSet(BTN_Y_PORT, BTN_Y_PIN);
}

static bool IsAPress(void)
{ 
    return LL_GPIO_IsInputPinSet(BTN_A_PORT, BTN_A_PIN);
}

static bool IsBPress(void)
{ 
    return LL_GPIO_IsInputPinSet(BTN_B_PORT, BTN_B_PIN);
}

static bool IsUpPress(void)
{ 
    return LL_GPIO_IsInputPinSet(BTN_UP_PORT, BTN_UP_PIN);
}

static bool IsDownPress(void)
{ 
    return LL_GPIO_IsInputPinSet(BTN_DOWN_PORT, BTN_DOWN_PIN);
}

static bool IsLeftPress(void)
{ 
    return LL_GPIO_IsInputPinSet(BTN_LEFT_PORT, BTN_LEFT_PIN);
}

static bool IsRightPress(void)
{ 
    return LL_GPIO_IsInputPinSet(BTN_RIGHT_PORT, BTN_RIGHT_PIN);
}

static bool IsViewPress(void)
{ 
    return LL_GPIO_IsInputPinSet(BTN_VIEW_PORT, BTN_VIEW_PIN);
}

static bool IsMenuPress(void)
{ 
    return LL_GPIO_IsInputPinSet(BTN_MENU_PORT, BTN_MENU_PIN);
}

static bool IsLStickPress(void)
{ 
    return LL_GPIO_IsInputPinSet(BTN_LSTICK_PORT, BTN_LSTICK_PIN);
}

static bool IsRStickPress(void)
{ 
    return LL_GPIO_IsInputPinSet(BTN_RSTICK_PORT, BTN_RSTICK_PIN);
}

static bool IsLbPress(void)
{ 
    return LL_GPIO_IsInputPinSet(BTN_LB_PORT, BTN_LB_PIN);
}

static bool IsRbPress(void)
{ 
    return LL_GPIO_IsInputPinSet(BTN_RB_PORT, BTN_RB_PIN);
}

static bool IsXboxPress(void)
{ 
    return LL_GPIO_IsInputPinSet(BTN_XBOX_PORT, BTN_XBOX_PIN);
}

const ButtonDef_t buttonDef[BtnIdx_Max] =
    {
        [BtnIdx_Pair] = {
            .getPress = IsPairPress,
        },
        [BtnIdx_X] = {
            .getPress = IsXPress,
        },
        [BtnIdx_Y] = {
            .getPress = IsYPress,
        },
        [BtnIdx_A] = {
            .getPress = IsAPress,
        },
        [BtnIdx_B] = {
            .getPress = IsBPress,
        },
        [BtnIdx_Up] = {
            .getPress = IsUpPress,
        },
        [BtnIdx_Down] = {
            .getPress = IsDownPress,
        },
        [BtnIdx_Left] = {
            .getPress = IsLeftPress,
        },
        [BtnIdx_Right] = {
            .getPress = IsRightPress,
        },
        [BtnIdx_View] = {
            .getPress = IsViewPress,
        },
        [BtnIdx_Menu] = {
            .getPress = IsMenuPress,
        },
        [BtnIdx_LStick] = {
            .getPress = IsLStickPress,
        },
        [BtnIdx_RStick] = {
            .getPress = IsRStickPress,
        },
        [BtnIdx_LB] = {
            .getPress = IsLbPress,
        },
        [BtnIdx_RB] = {
            .getPress = IsRbPress,
        },
        [BtnIdx_Xbox] = {
            .getPress = IsXboxPress,
        },
};

//********************button context*************************
typedef enum
{
    BtnSta_Up,
    BtnSta_Press,
    BtnSta_LongPress,
} ButtonStatus_t;

typedef struct
{
    ButtonStatus_t status;
    uint32_t downTime;
    uint32_t upTime;
} ButtonContext_t;

ButtonContext_t buttonContext[BtnIdx_Max];

void Button_Init(void)
{
    for (int i = 0; i < BtnIdx_Max; i++)
    {
        buttonContext[i].status = BtnSta_Up;
        buttonContext[i].downTime = 0;
    }
}

void Button_Process(void)
{
    static uint32_t lastTime = 0;
    if (lastTime == 0)
    {
        lastTime = GetSysTime();
    }
    else
    {
        uint32_t span = SysTimeSpan(lastTime);
        lastTime = GetSysTime();

        for (int i = 0; i < BtnIdx_Max; i++)
        {
            ButtonContext_t *bc = &buttonContext[i];
            if (bc->status == BtnSta_Up)
            { // 此时状态是未按下
                if (buttonDef[i].getPress())
                { // 持续按下
                    bc->downTime += span;
                }
                else
                { // 松开
                    bc->downTime = 0;
                }

                if (bc->downTime >= BUTTON_BOUNCE_TIME)
                { // 持续超过去抖时间,按键改为按下状态
                    bc->status = BtnSta_Press;
                    CL_LOG_INFO("button down");
                    ButtonEvent_t arg = ButtonEvent_Down;
                    CL_EventSysRaise(CL_Event_Button, i, &arg);
                }
            }
            else if (bc->status == BtnSta_Press)
            { // 此时状态是已按下
                if (buttonDef[i].getPress())
                { // 持续按下
                    bc->downTime += span;
                }
                else
                {
                    bc->downTime = 0;
                    bc->status = BtnSta_Up;
                    // 短按事件
                    CL_LOG_INFO("button %d click", i);
                    
                    ButtonEvent_t arg = ButtonEvent_Click;
                    CL_EventSysRaise(CL_Event_Button, i, &arg);
                }

                if (bc->downTime >= BUTTON_LONG_PRESS_TIME)
                {
                    bc->status = BtnSta_LongPress;
                    CL_LOG_INFO("button %d long press", i);

                    // 长按事件
                    ButtonEvent_t arg = ButtonEvent_LongPress;
                    CL_EventSysRaise(CL_Event_Button, i, &arg);
                }
            }
            else if (bc->status == BtnSta_LongPress)
            { // 此时是长按
                if (buttonDef[i].getPress())
                { // 持续按下
                    bc->downTime += span;
                }
                else
                { // 长按松开
                    bc->downTime = 0;
                    bc->status = BtnSta_Up;
                    CL_LOG_INFO("button up");
                    ButtonEvent_t arg = ButtonEvent_LpUp;
                    CL_EventSysRaise(CL_Event_Button, i, &arg);
                }
            }
        }
    }
}

bool Button_IsPress(ButtonIdx_t idx)
{
    return buttonContext[idx].status > BtnSta_Up;
}
