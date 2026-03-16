#include "dfu.h"
#include "flash_layout.h"
#include "cl_log.h"
#include "systime.h"
#include "cl_event_system.h"
#include "sgp_protocol.h"
#include "sgp_cmd.h"
#include "comm.h"
#include "firmware_info.h"
#include "cl_serialize.h"
#include "crc.h"
#include "string.h"
#include "board.h"
#include "sign_check.h"

static inline void ToggleLed(void)
{
    LL_GPIO_TogglePin(STA_LED_PROT, STA_LED_PIN);
    LL_GPIO_TogglePin(GPIOA, LL_GPIO_PIN_8);
}

extern const FirmwareInfo_t bootFwInfo;

typedef enum
{
    DfuStatus_Idle = 0,
    DfuStatus_WaitReq,
    DfuStatus_RecvFile,
    DfuStatus_CheckApp,
    DfuStatus_Jump,
    DfuStatus_Error,
} DfuStatus_t;

typedef struct
{
    DfuStatus_t status;
    uint32_t fileSize;
    uint32_t recvSize;
    uint32_t lastCommTime;
    uint16_t packCount;
} DfuContext_t;

static DfuContext_t dfuContext = {
    .status = DfuStatus_Idle,
    .fileSize = 0,
    .recvSize = 0,
    .packCount = 0,
    .lastCommTime = 0,
};

static bool OnRecvSgpMsg(void *eventArg);

static inline void SetLastCommTime(void)
{
    dfuContext.lastCommTime = GetSysTime();
}

static inline bool IsCommTimeout(void)
{
    return SysTimeSpan(dfuContext.lastCommTime) >= SYSTIME_SECOND(10);
}

static void ToIdle(void)
{
    dfuContext.status = DfuStatus_Idle;
}

static void ToWaitReq(void)
{
    dfuContext.status = DfuStatus_WaitReq;
}

static void ToRecvFile(uint32_t fileSize)
{
    dfuContext.fileSize = fileSize;
    dfuContext.recvSize = 0;
    dfuContext.packCount = 0;
    dfuContext.status = DfuStatus_RecvFile;
}

static void ToCheckApp(void)
{
    dfuContext.status = DfuStatus_CheckApp;
}

static void ToJump(void)
{
    dfuContext.status = DfuStatus_Jump;
    HAL_FLASH_Lock();
}

static void ToError(void)
{
    Comm_SendMsg(SpgCmd_Dfu, SgpSubCmd_DfuError, NULL, 0);
    dfuContext.status = DfuStatus_Error;
}

void Dfu_Init(void)
{
    SignCheck_Init();

    CL_EventSysAddListener(OnRecvSgpMsg, CL_Event_SgpRecvMsg, 0);
    ToIdle();
}

void Dfu_Process(void)
{
    switch (dfuContext.status)
    {
    case DfuStatus_Idle:
        if (NeedDfu())
        {
            CL_LOG_INFO("need dfu, to wait dfu request");
            ToWaitReq();
        }
        else
        {
            CL_LOG_INFO("don't need dfu, to check app");
            ToCheckApp();
        }
        break;
    case DfuStatus_WaitReq:
    {
        static uint32_t lastTime = 0;
        if (SysTimeSpan(lastTime) >= 1000)
        {
            lastTime = GetSysTime();

            ToggleLed();
        }
    }
    break;
    case DfuStatus_RecvFile:
        if (IsCommTimeout())
        {
            Comm_SendMsg(SpgCmd_Dfu, SgpSubCmd_DfuError, NULL, 0);
            ToCheckApp();
        }
        break;
    case DfuStatus_CheckApp:
        if (IsAppValid())
        {
            CL_LOG_INFO("app valid, prepare to jump");
            ToJump();
        }
        else
        {
            CL_LOG_INFO("no valid app, need dfu");
            ToWaitReq();
        }
        break;
    case DfuStatus_Jump:
        UnmarkDfu();
        DelayOnSysTime(100);
        NVIC_SystemReset();
        break;
    case DfuStatus_Error:
        ToCheckApp();
        break;
    default:
        break;
    }
}

static void SendDfuReady(void)
{
    Comm_SendMsg(SpgCmd_Dfu, SgpSubCmd_DfuReady, NULL, 0);
}

static void SendDfuDataRsp(uint16_t packCount, uint8_t result)
{
    uint8_t data[3];
    CL_Uint16ToBytes(packCount, data, CL_BigEndian);
    data[2] = result;
    Comm_SendMsg(SpgCmd_Dfu, SgpSubCmd_DfuDataRsp, data, 3);
}

static void SendDfuVerifyRsp(uint8_t result)
{
    Comm_SendMsg(SpgCmd_Dfu, SgpSubCmd_DfuVerifyRsp, &result, 1);
}

static void OnRecvBootVerReq(void)
{
    uint8_t data[14];
    memcpy(data, PRODUCT_BOOT_STR, 10);
    data[10] = bootFwInfo.verMajor;
    data[11] = bootFwInfo.verMinor;
    CL_Uint16ToBytes(bootFwInfo.verPatch, data + 12, CL_BigEndian);

    Comm_SendMsg(SpgCmd_Dfu, SgpSubCmd_DfuBootVerRsp, data, sizeof(data));

    CL_LOG_INFO("send boot version");
}

static void OnRecvAppVerReq(void)
{
    const FirmwareInfo_t *pAppInfo = (const FirmwareInfo_t *)0x8013800;
    uint8_t data[14];
    memcpy(data, PRODUCT_APP_STR, 10);
    data[10] = pAppInfo->verMajor;
    data[11] = pAppInfo->verMinor;
    CL_Uint16ToBytes(pAppInfo->verPatch, data + 12, CL_BigEndian);

    Comm_SendMsg(SpgCmd_Dfu, SgpSubCmd_AppVerRsp, data, sizeof(data));

    CL_LOG_INFO("send app version");
}

void OnRecvDfuRequest(const SgpPacket_t *pack)
{
    if (dfuContext.status == DfuStatus_WaitReq)
    {
        if (pack->length != 4)
        {
            CL_LOG_INFO("dfu req pack len error");
            return;
        }
        uint32_t fileSize = CL_BytesToUint32(pack->data, CL_BigEndian);
        if (fileSize > APP_MAX_SIZE || fileSize == 0)
        {
            CL_LOG_INFO("dfu file length error");
            return;
        }
        HAL_FLASH_Unlock();
        EraseAppSection();
        ToRecvFile(fileSize);
        SendDfuReady();
        SetLastCommTime();
    }
    else if (dfuContext.status == DfuStatus_RecvFile && dfuContext.recvSize == 0)
    {
        SendDfuReady();
        SetLastCommTime();
    }
}

static void OnRecvDfuData(const SgpPacket_t *pack)
{
    if (dfuContext.status == DfuStatus_RecvFile)
    {
        if (pack->length < 2)
            return;

        uint16_t packCount = CL_BytesToUint16(pack->data, CL_BigEndian);
        if (packCount == dfuContext.packCount)
        {
            dfuContext.packCount++;
            uint16_t bytesInPack = pack->length - 2;
            if (dfuContext.recvSize + bytesInPack > dfuContext.fileSize)
            {
                ToError();
                return;
            }

            ToggleLed();
            CL_Result_t res = WriteFlash(APP_START_ADDR + dfuContext.recvSize, pack->data + 2, bytesInPack);
            dfuContext.recvSize += bytesInPack;
            CL_LOG_INFO("dfu pack: %hu--%hu, recv size: %u", packCount, bytesInPack, dfuContext.recvSize);
            SendDfuDataRsp(packCount, res == CL_ResSuccess ? 1 : 0);
            SetLastCommTime();
        }
        else if (dfuContext.packCount == (packCount + 1) && dfuContext.packCount > 0)
        {
            CL_LOG_INFO("rsp last pack");
            SendDfuDataRsp(packCount, 1);
            SetLastCommTime();
        }
    }
}

CL_Result_t VerifyApp(const SgpPacket_t *pack)
{
    if (dfuContext.fileSize != dfuContext.recvSize)
        return CL_ResFailed;

    if (pack->length != 64)
        return CL_ResFailed;

    return SingCheck((const uint8_t *)APP_START_ADDR, dfuContext.recvSize, (const uint8_t *)pack->data, pack->length);
}

static void OnRecvDfuVerify(const SgpPacket_t *pack)
{
    if (dfuContext.status == DfuStatus_RecvFile)
    {
        if (dfuContext.fileSize != dfuContext.recvSize)
        {
            ToError();
            return;
        }

        CL_Result_t res = VerifyApp(pack);
        uint8_t rsp = 1;
        if (res == CL_ResSuccess)
        {
            SaveAppInfo(APP_START_ADDR, dfuContext.fileSize);
            CL_LOG_INFO("dfu verity ok");
        }
        else
        {
            rsp = 0;
            CL_LOG_INFO("dfu verity failed");
        }
        SendDfuVerifyRsp(rsp);
        ToCheckApp();
    }
}

static bool OnRecvSgpMsg(void *eventArg)
{
    const SgpPacket_t *pack = (const SgpPacket_t *)eventArg;
    if (pack->cmd == SpgCmd_Dfu)
    {
        switch (pack->subCmd)
        {
        case SgpSubCmd_DfuReq:
            OnRecvDfuRequest(pack);
            break;
        case SgpSubCmd_DfuData:
            OnRecvDfuData(pack);
            break;
        case SgpSubCmd_DfuVerify:
            OnRecvDfuVerify(pack);
            break;
        case SgpSubCmd_DfuBootVer:
            OnRecvBootVerReq();
            break;
        case SgpSubCmd_AppVer:
            OnRecvAppVerReq();
            break;
        }
    }

    return true;
}
