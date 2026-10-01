#include "patches.h"
#include "padmgr.h"
#include "irqmgr.h"
#include "sched.h"
#include "fault.h"
#include "buffers.h"
#include "sys_ucode.h"
#include "game.h"
#include "audio.h"
#include "audiomgr.h"
#include "rumble.h"
#include "speed_meter.h"
#include "vis.h"
#include "vi_mode.h"
#include "gamealloc.h"
#include "ocarina.h"
#include "../../patches/input.h"

void recomp_set_current_frame_poll_id();
void* osViGetCurrentFramebuffer_recomp();

OSMesgQueue* PadMgr_AcquireSerialEventQueue(PadMgr* padMgr);
void PadMgr_ReleaseSerialEventQueue(PadMgr* padMgr, OSMesgQueue* serialEventQueue);
void PadMgr_LockPadData(PadMgr* padMgr);
void PadMgr_UnlockPadData(PadMgr* padMgr);
void PadMgr_UpdateRumble(PadMgr* padMgr);
void PadMgr_RumbleStop(PadMgr* padMgr);
void PadMgr_UpdateInputs(PadMgr* padMgr);

// @recomp Patched to only handle rumble, as input polling is done when the game requests inputs to minimize latency.
RECOMP_PATCH void PadMgr_HandleRetrace(PadMgr* padMgr) {
    // Execute retrace callback
    if (padMgr->retraceCallback != NULL) {
        padMgr->retraceCallback(padMgr, padMgr->retraceCallbackArg);
    }

    if (FAULT_MSG_ID != 0) {
        // If fault is active, no rumble
        PadMgr_RumbleStop(padMgr);
    } else if (padMgr->rumbleOffTimer > 0) {
        // If the rumble off timer is active, no rumble
        --padMgr->rumbleOffTimer;
        PadMgr_RumbleStop(padMgr);
    } else if (padMgr->rumbleOnTimer == 0) {
        // If the rumble on timer is inactive, no rumble
        PadMgr_RumbleStop(padMgr);
    } else if (!padMgr->isResetting) {
        // If not resetting, update rumble
        PadMgr_UpdateRumble(padMgr);
        --padMgr->rumbleOnTimer;
    }
}

extern u8 sOcarinaInstrumentId;

static void poll_inputs(PadMgr* padMgr) {
    OSMesgQueue* serialEventQueue = PadMgr_AcquireSerialEventQueue(padMgr);

    // Begin reading controller data
    osContStartReadData(serialEventQueue);

    bool needs_right_stick = recomp_get_analog_cam_enabled() || recomp_aiming_override_mode == RECOMP_AIMING_OVERRIDE_FORCE_RIGHT_STICK;
    // Suppress the right analog stick if analog camera is active unless the ocarina is in use.
    recomp_set_right_analog_suppressed(needs_right_stick && sOcarinaInstrumentId == OCARINA_INSTRUMENT_OFF);
    // Resets this flag for the next frame;
    recomp_aiming_override_mode = RECOMP_AIMING_OVERRIDE_OFF;

    // Wait for controller data
    osRecvMesg(serialEventQueue, NULL, OS_MESG_BLOCK);
    osContGetReadData(padMgr->pads);

    // Clear all but controller 1
    bzero(&padMgr->pads[1], sizeof(*padMgr->pads) * (MAXCONTROLLERS - 1));

    // If resetting, clear all controllers
    if (padMgr->isResetting) {
        bzero(padMgr->pads, sizeof(padMgr->pads));
    }

    // Update input data
    PadMgr_UpdateInputs(padMgr);

    // Query controller status for all controllers
    osContStartQuery(serialEventQueue);
    osRecvMesg(serialEventQueue, NULL, OS_MESG_BLOCK);
    osContGetQuery(padMgr->padStatus);

    PadMgr_ReleaseSerialEventQueue(padMgr, serialEventQueue);

    {
        u32 mask = 0;
        s32 i;

        // Update the state of connected controllers
        for (i = 0; i < MAXCONTROLLERS; i++) {
            if (padMgr->padStatus[i].errno == 0) {
                // Only standard N64 controllers are supported
                if (padMgr->padStatus[i].type == CONT_TYPE_NORMAL) {
                    mask |= 1 << i;
                }
            }
        }
        padMgr->validCtrlrsMask = mask;
    }
}

// @recomp Patched to poll inputs when the game requests them instead of on every VI.
RECOMP_PATCH void PadMgr_RequestPadData(PadMgr* padMgr, Input* inputs, s32 gameRequest) {
    s32 i;
    Input* inputIn;
    Input* inputOut;
    s32 buttonDiff;

    // @recomp Do an actual poll if gameRequest is true.
    if (gameRequest) {
        poll_inputs(padMgr);
        // @recomp Tag the current frame's input polling id for latency tracking.
        recomp_set_current_frame_poll_id();
    }

    PadMgr_LockPadData(padMgr);

    for (inputIn = &padMgr->inputs[0], inputOut = &inputs[0], i = 0; i < MAXCONTROLLERS; i++, inputIn++, inputOut++) {
        if (gameRequest) {
            // Copy inputs as-is, press and rel are calculated prior in `PadMgr_UpdateInputs`
            *inputOut = *inputIn;
            // Zero parts of the press and rel inputs in the polled inputs so they are not read more than once
            inputIn->press.button = 0;
            inputIn->press.stick_x = 0;
            inputIn->press.stick_y = 0;
            inputIn->rel.button = 0;
        } else {
            // Take as the previous inputs the inputs that are currently in the destination array
            inputOut->prev = inputOut->cur;
            // Copy current inputs from the polled inputs
            inputOut->cur = inputIn->cur;
            // Calculate press and rel from these
            buttonDiff = inputOut->prev.button ^ inputOut->cur.button;
            inputOut->press.button = inputOut->cur.button & buttonDiff;
            inputOut->rel.button = inputOut->prev.button & buttonDiff;
            PadUtils_UpdateRelXY(inputOut);
            inputOut->press.stick_x += (s8)(inputOut->cur.stick_x - inputOut->prev.stick_x);
            inputOut->press.stick_y += (s8)(inputOut->cur.stick_y - inputOut->prev.stick_y);
        }
    }

    PadMgr_UnlockPadData(padMgr);
}

extern OSTime sGraphPrevTaskTimeStart;
extern IrqMgr gIrqMgr;

// @recomp The number of VIs to wait for the current frame, including extra ones requested by patches.
extern s32 recomp_frame_vis;
// @recomp Lag VIs to stall for after the current frame, requested by patches.
extern s32 recomp_lag_vis;

// @recomp Immediately waits for the graphics task after sending it instead of waiting for it before sending the next one.
RECOMP_PATCH void Graph_TaskSet00(GraphicsContext* gfxCtx) {
    OSTask_t* task = &gfxCtx->task.list.t;
    OSScTask* scTask = &gfxCtx->task;
    OSMesg msg;
    static CfbInfo sGraphCfbInfos[3];
    static s32 sGraphCfbInfoIdx = 0;
    CfbInfo* cfb;

    // @recomp Additional static members for extra scheduling purposes.
    static IrqMgrClient irq_client = {0};
    static OSMesgQueue vi_queue = {0};
    static OSMesg vi_buf[8] = {0};
    static bool created = false;
    if (!created) {
        created = true;
        osCreateMesgQueue(&vi_queue, vi_buf, ARRAY_COUNT(vi_buf));
        IrqMgr_AddClient(&gIrqMgr, &irq_client, &vi_queue);
    }

    gGfxTaskSentToNextReadyMinusAudioThreadUpdateTime =
        osGetTime() - sGraphPrevTaskTimeStart - gAudioThreadUpdateTimeAcc;

    // @recomp The wait for the previous task has been moved to after submitting the task to minimize latency.

    if (gfxCtx->callback != NULL) {
        gfxCtx->callback(gfxCtx, gfxCtx->callbackParam);
    }

    {
        OSTime timeNow = osGetTime();

        if (gAudioThreadUpdateTimeStart != 0) {
            // The audio thread update is running
            // Add the time already spent to the accumulator and leave the rest for the next cycle

            gAudioThreadUpdateTimeAcc += timeNow - gAudioThreadUpdateTimeStart;
            gAudioThreadUpdateTimeStart = timeNow;
        }
        gAudioThreadUpdateTimeTotalPerGfxTask = gAudioThreadUpdateTimeAcc;
        gAudioThreadUpdateTimeAcc = 0;

        sGraphPrevTaskTimeStart = osGetTime();
    }

    task->type = M_GFXTASK;
    task->flags = OS_SC_DRAM_DLIST;
    task->ucode_boot = SysUcode_GetUCodeBoot();
    task->ucode_boot_size = SysUcode_GetUCodeBootSize();
    task->ucode = SysUcode_GetUCode();
    task->ucode_data = SysUcode_GetUCodeData();
    task->ucode_size = SP_UCODE_SIZE;
    task->ucode_data_size = SP_UCODE_DATA_SIZE;
    task->dram_stack = gGfxSPTaskStack;
    task->dram_stack_size = sizeof(gGfxSPTaskStack);
    task->output_buff = gGfxSPTaskOutputBuffer;
    task->output_buff_size = gGfxSPTaskOutputBuffer + ARRAY_COUNT(gGfxSPTaskOutputBuffer);
    task->data_ptr = (u64*)gfxCtx->workBuffer;

    OPEN_DISPS(gfxCtx, "../graph.c", 828);
    task->data_size = (uintptr_t)WORK_DISP - (uintptr_t)gfxCtx->workBuffer;
    CLOSE_DISPS(gfxCtx, "../graph.c", 830);

    task->yield_data_ptr = gGfxSPTaskYieldBuffer;

    task->yield_data_size = sizeof(gGfxSPTaskYieldBuffer);

    scTask->next = NULL;
    scTask->flags = OS_SC_NEEDS_RSP | OS_SC_NEEDS_RDP | OS_SC_SWAPBUFFER | OS_SC_LAST_TASK;
    if (R_GRAPH_TASKSET00_FLAGS & 1) {
        R_GRAPH_TASKSET00_FLAGS &= ~1;
        scTask->flags &= ~OS_SC_SWAPBUFFER;
        gfxCtx->fbIdx--;
    }

    scTask->msgQueue = &gfxCtx->queue;
    scTask->msg = NULL;

    cfb = &sGraphCfbInfos[sGraphCfbInfoIdx];

    sGraphCfbInfoIdx = (sGraphCfbInfoIdx + 1) % ARRAY_COUNT(sGraphCfbInfos);
    cfb->framebuffer = gfxCtx->curFrameBuffer;
    cfb->swapBuffer = gfxCtx->curFrameBuffer;

    cfb->viMode = gfxCtx->viMode;
    cfb->viFeatures = gfxCtx->viFeatures;
    cfb->unk_10 = 0;
    // @recomp Use the frame's VI count, which includes any extra VIs requested by patches.
    cfb->updateRate = recomp_frame_vis;

    scTask->framebuffer = cfb;

    // @recomp Flush any pending messages (such as the one sent by GameState_Init) since the wait now happens after submission.
    while (gfxCtx->queue.validCount != 0) {
        osRecvMesg(&gfxCtx->queue, NULL, OS_MESG_NOBLOCK);
    }

    gfxCtx->schedMsgQueue = &gScheduler.cmdQueue;

    osSendMesg(&gScheduler.cmdQueue, (OSMesg)scTask, OS_MESG_BLOCK);
    Sched_Notify(&gScheduler);

    // @recomp Immediately wait on the task to complete to minimize latency for the next one.
    osRecvMesg(&gfxCtx->queue, &msg, OS_MESG_BLOCK);

    // @recomp Put the completion message back, as the queue normally holds it until the next frame's task is sent.
    // gz waits on it when loading a savestate. It's flushed before the next task is sent.
    osSendMesg(&gfxCtx->queue, msg, OS_MESG_NOBLOCK);

    // @recomp Wait on the VI framebuffer to change if this task has a framebuffer swap.
    if (scTask->flags & OS_SC_SWAPBUFFER) {
        int viCounter = 0;
        while (osViGetCurrentFramebuffer_recomp() != cfb->framebuffer) {
            osRecvMesg(&vi_queue, NULL, OS_MESG_BLOCK);
            viCounter++;
        }

        // If we didn't wait the full number of VIs needed between frames then wait one extra VI afterwards.
        if (viCounter < recomp_frame_vis) {
            osRecvMesg(&vi_queue, NULL, OS_MESG_BLOCK);
        }
    }

    // @recomp Stall for any lag VIs requested by patches (e.g. emulated pause lag). This happens before the next
    // frame's input poll, so inputs made during the stall are read on the next frame like on console.
    // Controllers are polled on every VI of the stall like the console's retrace handler does. Presses and releases
    // accumulate until the game reads them, so a button tapped and released during the lag still registers.
    for (; recomp_lag_vis > 0; recomp_lag_vis--) {
        osRecvMesg(&vi_queue, NULL, OS_MESG_BLOCK);
        poll_inputs(&gPadMgr);
    }

    // @recomp Flush any extra messages from the VI queue.
    while (osRecvMesg(&vi_queue, NULL, OS_MESG_NOBLOCK) == 0) {
        ;
    }
}

extern SpeedMeter D_801664D0;
extern struct VisCvg sGameStateVisCvg;
extern struct VisZBuffer sGameStateVisZBuffer;
extern struct VisMono sGameStateVisMono;
extern ViMode sViMode;

// @recomp Patched to remove the wait for the graphics task, as it's now done directly after submission.
RECOMP_PATCH void GameState_Destroy(GameState* gameState) {
    AudioMgr_StopAllSfx();
    Audio_Update();

    // @recomp The wait for the gfx task was moved to directly after submission, so it's not needed here.
    // osRecvMesg(&gameState->gfxCtx->queue, NULL, OS_MESG_BLOCK);

    if (gameState->destroy != NULL) {
        gameState->destroy(gameState);
    }
    Rumble_Destroy();
    SpeedMeter_Destroy(&D_801664D0);
    VisCvg_Destroy(&sGameStateVisCvg);
    VisZBuffer_Destroy(&sGameStateVisZBuffer);
    VisMono_Destroy(&sGameStateVisMono);
    ViMode_Destroy(&sViMode);
    THA_Destroy(&gameState->tha);
    GameAlloc_Cleanup(&gameState->alloc);
}
