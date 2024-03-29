#include "cali.h"
#include "main.h"
#include "cl_log.h"
#include "crc.h"
#include "string.h"
#include "flash_layout.h"
#include "iflash_stm32.h"
#include "cl_event_system.h"
#include "button.h"
#include "led.h"
#include "cl_queue.h"
#include "systime.h"
#include "adc.h"
#include "mathex.h"

static float GetRadian(const Vector2 *v);

static CaliParams_t caliParams = {0};

const CaliParams_t *GetCaliParams(void)
{
    return &caliParams;
}

static void PrintParams(CaliParams_t *params)
{
    CL_LOG_INFO("**********");
    CL_LOG_INFO("cali params:");
    CL_LOG_INFO("left trigger: %d, %d", caliParams.leftTrigger[0], caliParams.leftTrigger[1]);
    CL_LOG_INFO("right trigger: %d, %d", caliParams.rightTrigger[0], caliParams.rightTrigger[1]);
    CL_LOG_INFO("----------");
}

static void ResetParams(void)
{
    caliParams.leftMidX = 2048;
    caliParams.leftMidY = 2048;
    for (int i = 0; i < CL_ARRAY_LENGTH(caliParams.leftMag); i++)
    {
        caliParams.leftMag[i] = 2048;
    }

    caliParams.rightMidX = 2048;
    caliParams.rightMidY = 2048;
    for (int i = 0; i < CL_ARRAY_LENGTH(caliParams.rightMag); i++)
    {
        caliParams.rightMag[i] = 2048;
    }

    caliParams.leftTrigger[0] = 0;
    caliParams.leftTrigger[1] = 4096;

    caliParams.rightTrigger[0] = 0;
    caliParams.rightTrigger[1] = 4096;
}

static void LoadCalibration(void)
{
    memcpy((void *)&caliParams, (void *)PAD_PARAM_ADDR, sizeof(caliParams));
    uint32_t crc = Ethernet_CRC32((const uint8_t *)&caliParams, CL_OFFSET_OF(CaliParams_t, crc));
    if (crc != caliParams.crc)
    { // use default params
        CL_LOG_INFO("use default params");
        ResetParams();
    }
    else
    {
        CL_LOG_INFO("use saved params");
        PrintParams(&caliParams);
    }
}

static void SaveCalibration(void)
{
    caliParams.crc = Ethernet_CRC32((const uint8_t *)&caliParams, CL_OFFSET_OF(CaliParams_t, crc));
    HAL_FLASH_Unlock();
    IFlashStm32_ErasePages(PAD_PARAM_ADDR, 1);
    IFlashStm32_Write(PAD_PARAM_ADDR, (const uint8_t *)&caliParams, sizeof(caliParams));
    HAL_FLASH_Lock();
}

static CaliStatus_t caliStatus = CaliSta_None;

static void ToCaliNone(void)
{
    SetPadLedStyle(PadLedStyle_On);
    caliStatus = CaliSta_None;
    CL_LOG_INFO("cali done");
    PrintParams(&caliParams);
}

typedef struct
{
    uint16_t leftX, leftY, rightX, rightY;
    uint16_t leftHall, rightHall;
} MidVal_t;
CL_QUEUE_DEF_INIT(middleQueue, 20, MidVal_t, static);
static void ToCaliMiddle(void)
{
    SetPadLedStyle(PadLedStyle_Breath);
    CL_QueueClear(&middleQueue);
    caliStatus = CaliSta_Middle;
    CL_LOG_INFO("start cali middle");
}

static void ToCaliMargin(void)
{
    for (int i = 0; i < CL_ARRAY_LENGTH(caliParams.leftMag); i++)
        caliParams.leftMag[i] = 0;

    for (int i = 0; i < CL_ARRAY_LENGTH(caliParams.rightMag); i++)
        caliParams.rightMag[i] = 0;

    caliParams.leftTrigger[1] = 0;
    caliParams.rightTrigger[1] = 0;

    SetPadLedStyle(PadLedStyle_Blink);
    caliStatus = CaliSta_Margin;
    CL_LOG_INFO("start cali margin");
}

static bool OnBtnPairEvent(void *eventArg)
{ // pair长按,进入中间值校准状态
    ButtonEvent_t *pEvt = (ButtonEvent_t *)eventArg;
    if (pEvt[0] == ButtonEvent_LongPress)
    {
        if (caliStatus == CaliSta_None)
        {
            ToCaliMiddle();
        }
    }
    return true;
}

static bool OnBtnAEvent(void *eventArg)
{ // A短按,切换校准步骤
    ButtonEvent_t *pEvt = (ButtonEvent_t *)eventArg;
    if (pEvt[0] == ButtonEvent_Click)
    {
        if (caliStatus == CaliSta_Margin)
        {
            SaveCalibration();
            ToCaliNone();
        }
    }
    return true;
}

static bool OnBtnYEvent(void *eventArg)
{ // Y长按,参数复位
    ButtonEvent_t *pEvt = (ButtonEvent_t *)eventArg;
    if (pEvt[0] == ButtonEvent_LongPress)
    {
        if (caliStatus == CaliSta_Margin)
        {
            ResetParams();
            SaveCalibration();
            ToCaliNone();
        }
    }
    return true;
}

void Cali_Init(void)
{
    SetPadLedStyle(PadLedStyle_On);
    LoadCalibration();
    CL_EventSysAddListener(OnBtnPairEvent, CL_Event_Button, BtnIdx_Pair);
    CL_EventSysAddListener(OnBtnAEvent, CL_Event_Button, BtnIdx_A);
    CL_EventSysAddListener(OnBtnYEvent, CL_Event_Button, BtnIdx_Y);
}

#define MID_MAX_DIFF (50)
static void MiddleProc(void)
{
    // 摇杆和扳机值稳定后,记录下来作为摇杆的中间值,扳机的最小值
    static uint32_t lastTime = 0;
    if (SysTimeSpan(lastTime) >= 100)
    {
        lastTime = GetSysTime();

        MidVal_t temp;
        temp.leftX = GetAdcResult(AdcChan_LeftX);
        temp.leftY = GetAdcResult(AdcChan_LeftY);
        temp.rightX = GetAdcResult(AdcChan_RightX);
        temp.rightY = GetAdcResult(AdcChan_RightY);
        temp.leftHall = GetAdcResult(AdcChan_LeftHall);
        temp.rightHall = GetAdcResult(AdcChan_RightHall);

        if (CL_QueueFull(&middleQueue))
        {
            CL_QueuePoll(&middleQueue, CL_NULL);
        }
        CL_QueueAdd(&middleQueue, &temp);

        if (CL_QueueFull(&middleQueue))
        {
            MidVal_t min, max;
            memset(&min, 0xff, sizeof(MidVal_t));
            memset(&max, 0, sizeof(MidVal_t));

            MidVal_t *pData;
            CL_QUEUE_FOR_EACH(&middleQueue, pData, MidVal_t)
            {
                min.leftX = CL_MIN(min.leftX, pData->leftX);
                min.leftY = CL_MIN(min.leftY, pData->leftY);
                min.rightX = CL_MIN(min.rightX, pData->rightX);
                min.rightY = CL_MIN(min.rightY, pData->rightY);
                min.leftHall = CL_MIN(min.leftHall, pData->leftHall);
                min.rightHall = CL_MIN(min.rightHall, pData->rightHall);

                max.leftX = CL_MAX(max.leftX, pData->leftX);
                max.leftY = CL_MAX(max.leftY, pData->leftY);
                max.rightX = CL_MAX(max.rightX, pData->rightX);
                max.rightY = CL_MAX(max.rightY, pData->rightY);
                max.leftHall = CL_MAX(max.leftHall, pData->leftHall);
                max.rightHall = CL_MAX(max.rightHall, pData->rightHall);
            }

            MidVal_t diff;
            diff.leftX = max.leftX - min.leftX;
            diff.leftY = max.leftY - min.leftY;
            diff.rightX = max.rightX - min.rightX;
            diff.rightY = max.rightY - min.rightY;
            diff.leftHall = max.leftHall - min.leftHall;
            diff.rightHall = max.rightHall - min.rightHall;

            if (diff.leftX < MID_MAX_DIFF &&
                diff.leftY < MID_MAX_DIFF &&
                diff.rightX < MID_MAX_DIFF &&
                diff.rightY < MID_MAX_DIFF &&
                diff.leftHall < MID_MAX_DIFF &&
                diff.rightHall < MID_MAX_DIFF)
            {
                caliParams.leftMidX = (min.leftX + max.leftX) / 2;
                caliParams.leftMidY = (min.leftY + max.leftY) / 2;
                caliParams.rightMidX = (min.rightX + max.rightX) / 2;
                caliParams.rightMidY = (min.rightY + max.rightY) / 2;
                caliParams.leftTrigger[0] = (min.leftHall + max.leftHall) / 2;
                caliParams.rightTrigger[0] = (min.rightHall + max.rightHall) / 2;

                CL_LOG_INFO("middle: %d, %d, %d, %d, %d, %d",
                            caliParams.leftMidX,
                            caliParams.leftMidY,
                            caliParams.rightMidX,
                            caliParams.rightMidY,
                            caliParams.leftTrigger[0],
                            caliParams.rightTrigger[0]);
                ToCaliMargin();
            }
        }
    }
}

static void StickMarginProc(Vector2 *stick, bool left)
{
    const float magrinThreshold = 90000.0f;

    uint16_t *mags;
    uint8_t len = CL_ARRAY_LENGTH(caliParams.leftMag);
    if (left)
    {
        stick->x -= caliParams.leftMidX;
        stick->y -= caliParams.leftMidY;
        mags = caliParams.leftMag;
    }
    else
    {
        stick->x -= caliParams.rightMidX;
        stick->y -= caliParams.rightMidY;
        mags = caliParams.rightMag;
    }

    float sqrMag = Vector2_SqrMagnitude(stick);
    if (sqrMag > magrinThreshold)
    {
        float rad = GetRadian(stick);
        rad /= (M_PI * 2 / len);
        int pos = roundf(rad);
        if (FLOAT_NEAR(rad, pos, 0.1f))
        {
            pos %= len;

            if (sqrMag > mags[pos] * mags[pos])
                mags[pos] = sqrtf(sqrMag);
        }
    }
}

static void MarginProc(void)
{
    // 摇杆记录60个角度的向量长度
    Vector2 leftStick;
    leftStick.x = GetAdcResult(AdcChan_LeftX);
    leftStick.y = GetAdcResult(AdcChan_LeftY);
    StickMarginProc(&leftStick, true);

    Vector2 rightStick;
    rightStick.x = GetAdcResult(AdcChan_RightX);
    rightStick.y = GetAdcResult(AdcChan_RightY);
    StickMarginProc(&rightStick, false);

    // 记录扳机的最大值
    caliParams.leftTrigger[1] = CL_MAX(caliParams.leftTrigger[1], GetAdcResult(AdcChan_LeftHall));
    caliParams.rightTrigger[1] = CL_MAX(caliParams.rightTrigger[1], GetAdcResult(AdcChan_RightHall));

    bool allFound = true;
    for (int i = 0; i < CL_ARRAY_LENGTH(caliParams.leftMag); i++)
    {
        if (caliParams.leftMag[i] == 0 || caliParams.rightMag[i] == 0)
        {
            allFound = false;
            break;
        }
    }

    if (caliParams.leftTrigger[1] < caliParams.leftTrigger[0] + 500)
        allFound = false;
    if (caliParams.rightTrigger[1] < caliParams.rightTrigger[0] + 500)
        allFound = false;

    if (allFound)
    { // 每个点都至少采集到数据了,设置为呼吸灯效果
        SetPadLedStyle(PadLedStyle_Breath);
    }
}

void Cali_Process(void)
{
    switch (caliStatus)
    {
    case CaliSta_None:
        break;
    case CaliSta_Middle:
        MiddleProc();
        break;
    case CaliSta_Margin:
        MarginProc();
        break;
    }
}

CaliStatus_t GetCaliStatus(void)
{
    return caliStatus;
}

static float GetRadian(const Vector2 *v)
{
    float cos = v->y / Vector2_Magnitude(v);
    cos = CL_CLAMP(cos, -1.0, 1.0f);
    float rad = acosf(cos);
    // rad = CL_CLAMP(rad, 0, M_PI);

    if (v->x < 0)
        rad = M_PI * 2 - rad;

    return rad;
}

void StickCorrect(Vector2 *stick, bool left)
{
    uint16_t *caliMags;
    uint8_t len = CL_ARRAY_LENGTH(caliParams.leftMag); // 边界值数组长度
    // 减去中心点值,获取不同角度的边界值数组
    if (left)
    {
        stick->x -= caliParams.leftMidX;
        stick->y -= caliParams.leftMidY;
        caliMags = caliParams.leftMag;
    }
    else
    {
        stick->x -= caliParams.rightMidX;
        stick->y -= caliParams.rightMidY;
        caliMags = caliParams.rightMag;
    }

    // Vector2 vecOld; // for test
    // vecOld.x = stick->x;
    // vecOld.y = stick->y;

    // 计算得到弧度 0~2PI
    float rad = GetRadian(stick);
    rad /= (M_PI * 2 / len);

    // 从相邻两个点的边界值,插值得到当前的边界值
    int before, next;
    before = floorf(rad);
    next = ceilf(rad);

    float mag;
    if (before == next)
    {
        mag = caliMags[before % len];
    }
    else
    {
        mag = (rad - before) * caliMags[next % len] + (next - rad) * caliMags[before % len];
    }

    stick->x = stick->x / mag; // 计算x轴的值
    stick->y = stick->y / mag; // 计算y轴的值
    if ((stick->x * stick->x + stick->y * stick->y) < 0.007f)
    { // 死区
        stick->x = 0;
        stick->y = 0;
    }
    else
    {
        stick->x = stick->x * 33000.0f;                     // 换算成USB协议值
        stick->x = CL_CLAMP(stick->x, -32767.0f, 32767.0f); // x值范围限制

        stick->y = stick->y * 33000.0f;                     // 换算成USB协议值
        stick->y = CL_CLAMP(stick->y, -32767.0f, 32767.0f); // y值范围限制

        // float cos = Vector2_Cos(&vecOld, stick); // for test
        // if (cos < 0.939f)
        // {
        //     SetPadLedStyle(PadLedStyle_Off);
        // }
    }
}
