#include "pad_func.h"
#include "main.h"
#include "cl_log.h"
#include "adc.h"
#include "systime.h"
#include "cl_serialize.h"
#include "tim.h"
#include "led.h"
#include "cali.h"
#include "usb_device.h"
#include "usbd_hid.h"
#include "math.h"
#include "board.h"
#include "vector2.h"
#include "button.h"

static PadReport_t padReport = {
    .leftX = 0, // -32767 ~ 32767
    .leftY = 0,

    .rightX = 0,
    .rightY = 0,

    .leftTrigger = 0, // 0 ~ 255
    .rightTrigger = 0,

    .button[0] = 0,
    .button[1] = 0,
};

uint8_t vibration[PadVbrtIdx_Max] = {0};

void PadFunc_Init(void)
{
    Cali_Init();
}

typedef struct
{
    GPIO_TypeDef *port;
    uint32_t pin;
} BtnPinDef_t;
// button[0]: R3 L3 LM RM 右 左 下 上 bit7~bit0
// BtnPinDef_t btn0PinDef[8] = {
//     [7] = {BTN_RSTICK_PORT, BTN_RSTICK_PIN},
//     [6] = {BTN_LSTICK_PORT, BTN_LSTICK_PIN},
//     [5] = {BTN_VIEW_PORT, BTN_VIEW_PIN},
//     [4] = {BTN_MENU_PORT, BTN_MENU_PIN},
//     [3] = {BTN_RIGHT_PORT, BTN_RIGHT_PIN},
//     [2] = {BTN_LEFT_PORT, BTN_LEFT_PIN},
//     [1] = {BTN_DOWN_PORT, BTN_DOWN_PIN},
//     [0] = {BTN_UP_PORT, BTN_UP_PIN},
// };
// button[1]: Y X B A PAIR XBOX RB LB
// BtnPinDef_t btn1PinDef[8] = {
//     [7] = {BTN_Y_PORT, BTN_Y_PIN},
//     [6] = {BTN_X_PORT, BTN_X_PIN},
//     [5] = {BTN_B_PORT, BTN_B_PIN},
//     [4] = {BTN_A_PORT, BTN_A_PIN},
//     [3] = {BTN_PAIR_PORT, BTN_PAIR_PIN},
//     [2] = {BTN_XBOX_PORT, BTN_XBOX_PIN},
//     [1] = {BTN_RB_PORT, BTN_RB_PIN},
//     [0] = {BTN_LB_PORT, BTN_LB_PIN},
// };

// static inline bool IsButtonPressed(GPIO_TypeDef *port, uint32_t pin)
// {
//     return LL_GPIO_IsInputPinSet(port, pin);
// }

static uint8_t HallAdcToHid(uint16_t adc, uint16_t min, uint16_t max)
{ // uint8_t
    if (adc < min)
    {
        return 0;
    }
    else if (adc < max)
    { // 平方根插值
        uint16_t total = max - min;
        float ratio = (float)(adc - min) / total;
        if (ratio < 0.025f)
            return 0;

        ratio = sqrtf(ratio);
        return ratio * 255;
    }
    else
    {
        return 255;
    }
}

void PadFunc_Process(void)
{
    static uint32_t lastTime = 0;
    if (USBD_UploadIdle(&hUsbDeviceFS) && SysTimeSpan(lastTime) >= 2)
    {
        lastTime = GetSysTime();

        // button0
        padReport.button[0] = 0;
        padReport.button[0] |= Button_IsPress(BtnIdx_Up) << 0;
        padReport.button[0] |= Button_IsPress(BtnIdx_Down) << 1;
        padReport.button[0] |= Button_IsPress(BtnIdx_Left) << 2;
        padReport.button[0] |= Button_IsPress(BtnIdx_Right) << 3;
        padReport.button[0] |= Button_IsPress(BtnIdx_Menu) << 4;
        padReport.button[0] |= Button_IsPress(BtnIdx_View) << 5;
        padReport.button[0] |= Button_IsPress(BtnIdx_LStick) << 6;
        padReport.button[0] |= Button_IsPress(BtnIdx_RStick) << 7;
        

        // button1
        padReport.button[1] = 0;
        padReport.button[1] |= Button_IsPress(BtnIdx_LB) << 0;
        padReport.button[1] |= Button_IsPress(BtnIdx_RB) << 1;
        padReport.button[1] |= Button_IsPress(BtnIdx_Xbox) << 2;
        padReport.button[1] |= Button_IsPress(BtnIdx_Pair) << 3;
        padReport.button[1] |= Button_IsPress(BtnIdx_A) << 4;
        padReport.button[1] |= Button_IsPress(BtnIdx_B) << 5;
        padReport.button[1] |= Button_IsPress(BtnIdx_X) << 6;
        padReport.button[1] |= Button_IsPress(BtnIdx_Y) << 7;

        // CL_LOG_INFO("button: %02x, %02x", padReport.button[0], padReport.button[1]);

        if (GetCaliStatus() == CaliSta_None)
        {
            const CaliParams_t *caliParams = GetCaliParams();
            // sticks
            Vector2 leftStick, rightStick;
            leftStick.x = GetAdcResult(AdcChan_LeftX);
            leftStick.y = GetAdcResult(AdcChan_LeftY);
            StickCorrect(&leftStick, true);

            padReport.leftX = leftStick.x;
            padReport.leftY = leftStick.y;

            rightStick.x = GetAdcResult(AdcChan_RightX);
            rightStick.y = GetAdcResult(AdcChan_RightY);
            StickCorrect(&rightStick, false);

            padReport.rightX = rightStick.x;
            padReport.rightY = rightStick.y;
            // hall
            padReport.leftTrigger = HallAdcToHid(GetAdcResult(AdcChan_LeftHall),
                                                 caliParams->leftTrigger[0], caliParams->leftTrigger[1]);
            padReport.rightTrigger = HallAdcToHid(GetAdcResult(AdcChan_RightHall),
                                                  caliParams->rightTrigger[0], caliParams->rightTrigger[1]);
        }
        else
        {
            padReport.leftX = ((int16_t)GetAdcResult(AdcChan_LeftX) - 2048) / 2048.0f * 32768;
            padReport.leftY = ((int16_t)GetAdcResult(AdcChan_LeftY) - 2048) / 2048.0f * 32768;
            padReport.rightX = ((int16_t)GetAdcResult(AdcChan_RightX) - 2048) / 2048.0f * 32768;
            padReport.rightY = ((int16_t)GetAdcResult(AdcChan_RightY) - 2048) / 2048.0f * 32768;
            padReport.leftTrigger = GetAdcResult(AdcChan_LeftHall) / 16;
            padReport.rightTrigger = GetAdcResult(AdcChan_RightHall) / 16;
        }

        USBD_SendPadReport(&hUsbDeviceFS, &padReport);

        PwmSetDuty(PwmChan_MotorLeft, vibration[PadVbrtIdx_LeftBottom]);
        PwmSetDuty(PwmChan_MotorRight, vibration[PadVbrtIdx_RightBottom]);
    }

    Cali_Process();
}

void SetPadVibration(PadVbrtIdx_t idx, uint8_t vbrt)
{
    // 原始值0~255,不要超100
    if (vbrt > 0)
    {
        vbrt = CL_CLAMP(vbrt, 0, 250);
        vibration[idx] = vbrt / 5 + 50;
    }
    else
    {
        vibration[idx] = 0;
    }
}
