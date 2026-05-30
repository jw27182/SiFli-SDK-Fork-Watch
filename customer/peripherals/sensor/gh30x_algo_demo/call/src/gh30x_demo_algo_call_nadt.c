/**
 * @copyright (c) 2003 - 2024, Goodix Co., Ltd. All rights reserved.
 *
 * @file    gh30x_demo_algo_call_nadt.c
 *
 * @brief   gh30x algorithm nadt interface
 *
 * @version ref gh30x_example_common.h
 *
 */

#include <stdio.h>
#include "gh30x_demo_algo_call.h"
#include "gh30x_demo_algorithm_calc.h"
#include "goodix_mem.h"
#include "goodix_nadt.h"
#include <rtthread.h>

#if (__USE_GOODIX_SOFT_ADT_ALGORITHM__)

const GS32 liNadtCfg[32] = {
                        1,				// NADT_CONFIG_SOFT_AUTOLED_TYPE            0
                        5,				// NADT_CONFIG_TURNLIGHT_TYPE               1
                        4,				// NADT_CONFIG_UNWEAR_TIME_TYPE             2
                        60,				// NADT_CONFIG_DETECT_TIMEOUT_TYPE          3
                        25,				// NADT_CONFIG_SAMPLE_RATE_TYPE             4
                        0,				// NADT_CONFIG_SLEEP_STATUS_TYPE            5
                        1,				// NADT_COFIG_UNWEAR_LEVEL_TYPE             6
                        1,				// NADT_CONFIG_LIVE_DETECT_EN_TYPE          7
                        40,				// NADT_CONFIG_HB_LOW_THR_TYPE              8
                        140,			// NADT_CONFIG_HB_HIGH_THR_TYPE             9
                        10,				// NADT_CONFIG_GREEN_SIG_LOW_THR_TYPE       10
                        180,			// NADT_CONFIG_GREEN_SIG_HIGH_THR_TYPE      11
                        1,				// NADT_CONFIG_BASE_RATIO_EN_TYPE           12
                        10,				// NADT_CONFIG_BASE_RATIO_THR_TYPE          13
                        1,				// NADT_CONFIG_LIVE_CONFIRM_EN_TYPE         14
                        10,				// NADT_CONFIG_IR_SIG_LOW_THR_TYPE          15
                        100,			// NADT_CONFIG_IR_SIG_HIGH_THR_TYPE         16
                        1,				// NADT_CONFIG_CTR_EN_TYPE                  17
                        10,				// NADT_CONFIG_CTR_THR_TYPE                 18
                        1,				// NADT_CONFIG_GAIN_EN_TYPE                 19
                        4,				// NADT_CONFIG_GAIN_STABLE_TIME_TYPE        20
                        3,				// NADT_CONFIG_GREEN_GAIN_THR_TYPE          21
                        3,				// NADT_CONFIG_IR_GAIN_THR_TYPE             22
                        1,				// NADT_CONFIG_PERIOD_EN_TYPE               23
                        30,				// NADT_CONFIG_PERIOD_LOW_THR_TYPE          24
                        90,				// NADT_CONFIG_PERIOD_HIGH_THR_TYPE         25
                        3,				// NADT_CONFIG_PERIOD_DIFF_THR_TYPE         26
                        10,				// NADT_CONFIG_TOTAL_CNT_TYPE               27
                        7,				// NADT_CONFIG_UNWEAR_CNT_TYPE              28
                        0,				// NADT_CONFIG_ADT_ONLY_SLEEP_TYPE          29
                        5230,			// NADT_CONFIG_ADT_WEAR_OFF_THR_TYPE        30
                        5				// NADT_CONFIG_ADT_WEAR_OFF_CNT_TYPE        31
};

void NADT_Control_S(GS32 lOptTye, GS32 lConfigValue[])
{
    GH30X_ALGO_LOG_PARAM("[NADT_Control]: %d, %d\r\n", lOptTye, lConfigValue[0]);
    NADT_Control(lOptTye, lConfigValue);
}

GS8 GH30xSoftAdtAlgoInit(const STGh30xFrameInfo * const pstFrameInfo)
{
#if 1 /* [DBG_NADT_INIT] */
    rt_kprintf("[DBG_NADT_INIT] GH30xSoftAdtAlgoInit called\r\n");
#endif
    NADT_ProcInit();

    for (GU8 uchParmCnt = 0; uchParmCnt < 32; uchParmCnt++)
    {
        NADT_Control_S(uchParmCnt, (GS32*)(&(liNadtCfg[uchParmCnt])));
    }

#if 1 /* [DBG_NADT_INIT] */
    rt_kprintf("[DBG_NADT_INIT] NADT configured with 32 params\r\n");
    rt_kprintf("[DBG_NADT_INIT] [0]autoLED=%d [1]turnLight=%d [2]unwearTime=%d [3]detectTimeout=%d\r\n",
               (int)liNadtCfg[0], (int)liNadtCfg[1], (int)liNadtCfg[2], (int)liNadtCfg[3]);
    rt_kprintf("[DBG_NADT_INIT] [4]sampleRate=%d [5]sleep=%d [6]unwearLevel=%d [7]liveDetectEn=%d\r\n",
               (int)liNadtCfg[4], (int)liNadtCfg[5], (int)liNadtCfg[6], (int)liNadtCfg[7]);
    rt_kprintf("[DBG_NADT_INIT] [8]hbLowThr=%d [9]hbHighThr=%d [10]greenSigLow=%d [11]greenSigHigh=%d\r\n",
               (int)liNadtCfg[8], (int)liNadtCfg[9], (int)liNadtCfg[10], (int)liNadtCfg[11]);
    rt_kprintf("[DBG_NADT_INIT] [12]baseRatioEn=%d [13]baseRatioThr=%d [14]liveConfirmEn=%d [15]irSigLow=%d\r\n",
               (int)liNadtCfg[12], (int)liNadtCfg[13], (int)liNadtCfg[14], (int)liNadtCfg[15]);
    rt_kprintf("[DBG_NADT_INIT] [16]irSigHigh=%d [17]ctrEn=%d [18]ctrThr=%d [19]gainEn=%d\r\n",
               (int)liNadtCfg[16], (int)liNadtCfg[17], (int)liNadtCfg[18], (int)liNadtCfg[19]);
    rt_kprintf("[DBG_NADT_INIT] [20]gainStableTime=%d [21]greenGainThr=%d [22]irGainThr=%d [23]periodEn=%d\r\n",
               (int)liNadtCfg[20], (int)liNadtCfg[21], (int)liNadtCfg[22], (int)liNadtCfg[23]);
    rt_kprintf("[DBG_NADT_INIT] [24]periodLow=%d [25]periodHigh=%d [26]periodDiffThr=%d [27]totalCnt=%d\r\n",
               (int)liNadtCfg[24], (int)liNadtCfg[25], (int)liNadtCfg[26], (int)liNadtCfg[27]);
    rt_kprintf("[DBG_NADT_INIT] [28]unwearCnt=%d [29]adtOnlySleep=%d [30]wearOffThr=%d [31]wearOffCnt=%d\r\n",
               (int)liNadtCfg[28], (int)liNadtCfg[29], (int)liNadtCfg[30], (int)liNadtCfg[31]);
#endif
    return GH30X_RET_OK;
}

GS8 GH30xSoftAdtAlgoExe(const STGh30xFrameInfo *const pstFrameInfo)
{
    GS32 lNadtPacketInfoArr[6] = {0};
    GU8 lNadtResult[2] = {0};

    lNadtPacketInfoArr[0] = (pstFrameInfo->punRawdata[0] & 0x00FFFFFF);
    lNadtPacketInfoArr[1] = (pstFrameInfo->punRawdata[1] & 0x00FFFFFF);
    lNadtPacketInfoArr[2] = pstFrameInfo->pusGsensordata[0];
    lNadtPacketInfoArr[3] = pstFrameInfo->pusGsensordata[1];
    lNadtPacketInfoArr[4] = pstFrameInfo->pusGsensordata[2];
    lNadtPacketInfoArr[5] = pstFrameInfo->pchDrvCurrentCode[1];

    // 提取增益和adj标志
#if 1 /* [DBG_NADT_EXE] */
    GU8 uchGain0 = (pstFrameInfo->punRawdata[0] >> 24) & 0x07;
    GU8 uchGain1 = (pstFrameInfo->punRawdata[1] >> 24) & 0x07;
    GU8 uchAdj0 = (pstFrameInfo->punRawdata[0] >> 30) & 0x01;
    GU8 uchAdj1 = (pstFrameInfo->punRawdata[1] >> 30) & 0x01;
#endif

    NADT_Proc(lNadtPacketInfoArr, lNadtResult);

    pstFrameInfo->pstAlgoResult->snResult[0] = (GS32)lNadtResult[0];
    pstFrameInfo->pstAlgoResult->snResult[1] = (GS32)lNadtResult[1];

#if 1 /* [DBG_NADT_CHANGE] */
    // 追踪 NADT 结果状态变化 (当结果改变时立即打印)
    static GU8 s_prevNadtResult0 = 0;
    static GU8 s_prevNadtResult1 = 0;
    static GU32 s_nadtFrameCnt = 0;
    s_nadtFrameCnt++;

    if (lNadtResult[0] != s_prevNadtResult0 || lNadtResult[1] != s_prevNadtResult1)
    {
        rt_kprintf("[DBG_NADT_CHANGE] !!! RESULT CHANGED at frame=%lu: (%d,%d) -> (%d,%d) raw0=%d raw1=%d gain=(%d,%d) adj=(%d,%d) acc=(%d,%d,%d) drvCur=%d\r\n",
                   (unsigned long)pstFrameInfo->punFrameCnt[0],
                   (int)s_prevNadtResult0, (int)s_prevNadtResult1,
                   (int)lNadtResult[0], (int)lNadtResult[1],
                   (int)lNadtPacketInfoArr[0], (int)lNadtPacketInfoArr[1],
                   (int)uchGain0, (int)uchGain1,
                   (int)uchAdj0, (int)uchAdj1,
                   (int)lNadtPacketInfoArr[2], (int)lNadtPacketInfoArr[3], (int)lNadtPacketInfoArr[4],
                   (int)lNadtPacketInfoArr[5]);
        s_prevNadtResult0 = lNadtResult[0];
        s_prevNadtResult1 = lNadtResult[1];
    }

    // Debug: log NADT_Proc inputs/outputs every frame
    rt_kprintf("[DBG_NADT_EXE] frame=%lu in=(%d,%d,%d,%d,%d,%d) out=(%d,%d) g=(%d,%d) adj=(%d,%d)\r\n",
               (unsigned long)pstFrameInfo->punFrameCnt[0],
               (int)lNadtPacketInfoArr[0], (int)lNadtPacketInfoArr[1],
               (int)lNadtPacketInfoArr[2], (int)lNadtPacketInfoArr[3],
               (int)lNadtPacketInfoArr[4], (int)lNadtPacketInfoArr[5],
               (int)lNadtResult[0], (int)lNadtResult[1],
               (int)uchGain0, (int)uchGain1,
               (int)uchAdj0, (int)uchAdj1);

    // 每 25 帧输出一次信号质量摘要 (约 1 秒)
    if ((pstFrameInfo->punFrameCnt[0] % 25) == 0)
    {
        rt_kprintf("[DBG_NADT_QUALITY] frame=%lu Phase0=%d Phase1=%d gain0=%d gain1=%d adj0=%d adj1=%d acc=(%d,%d,%d) drvCur0=%d drvCur1=%d\r\n",
                   (unsigned long)pstFrameInfo->punFrameCnt[0],
                   (int)lNadtPacketInfoArr[0], (int)lNadtPacketInfoArr[1],
                   (int)uchGain0, (int)uchGain1,
                   (int)uchAdj0, (int)uchAdj1,
                   (int)lNadtPacketInfoArr[2], (int)lNadtPacketInfoArr[3], (int)lNadtPacketInfoArr[4],
                   (int)pstFrameInfo->pchDrvCurrentCode[0], (int)pstFrameInfo->pchDrvCurrentCode[1]);
        rt_kprintf("[DBG_NADT_THRESHOLDS] greenSigLow=10 greenSigHigh=180 irSigLow=10 irSigHigh=100 greenGainThr=3 irGainThr=3 hbLow=40 hbHigh=140 ctrThr=10 periodLow=30 periodHigh=90 wearOffThr=5230\r\n");
    }
#endif

    if (lNadtResult[0])
    {
        pstFrameInfo->pstAlgoResult->uchUpdateFlag = 1;
        pstFrameInfo->pstAlgoResult->usResultBit = 0x03;
        pstFrameInfo->pstAlgoResult->uchResultNum = GH30x_BitCount(pstFrameInfo->pstAlgoResult->usResultBit);
        GH30X_SoftAdtAlgorithmResultReport(pstFrameInfo->pstAlgoResult, GH30X_GET_BYTE0_FROM_DWORD(*(pstFrameInfo->punFrameCnt)));
    }

    return GH30X_RET_OK;
}

GS8 GH30xSoftAdtAlgoDeinit(const STGh30xFrameInfo *const pstFrameInfo)
{
    NADT_ProcDeInit();
    return GH30X_RET_OK;
}

#endif /* __USE_GOODIX_SOFT_ADT_ALGORITHM__ */
