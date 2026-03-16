#pragma once

#include "cl_common.h"

typedef enum
{
    BtnIdx_Pair = 0,
    BtnIdx_X,
    BtnIdx_Y,
    BtnIdx_A,
    BtnIdx_B,
    BtnIdx_Up,
    BtnIdx_Down,
    BtnIdx_Left,
    BtnIdx_Right,
    BtnIdx_View,
    BtnIdx_Menu,
    BtnIdx_LStick,
    BtnIdx_RStick,
    BtnIdx_LB,
    BtnIdx_RB,
    BtnIdx_Xbox,
    BtnIdx_Max,
} ButtonIdx_t;

typedef enum
{
    ButtonEvent_Down,      // 按下
    ButtonEvent_Click,     // 点击, 短按松开
    ButtonEvent_LongPress, // 长按
    ButtonEvent_LpUp,      // 长按松开
} ButtonEvent_t;

#define BUTTON_BOUNCE_TIME  (10)
#define BUTTON_LONG_PRESS_TIME  (2000)

void Button_Init(void);

void Button_Process(void);

bool Button_IsPress(ButtonIdx_t idx);
