#ifndef __GRAPH_PATCHES_H__
#define __GRAPH_PATCHES_H__

#include "patches.h"
#include "gfx.h"

#define GFXPOOL_HEAD_MAGIC 0x1234
#define GFXPOOL_TAIL_MAGIC 0x5678

extern OSTime sGraphPrevUpdateEndTime;
extern volatile OSTime gRSPGfxTimeTotal;
extern volatile OSTime gRSPGfxTimeAcc;
extern volatile OSTime gRSPAudioTimeTotal;
extern volatile OSTime gRSPAudioTimeAcc;
extern volatile OSTime gRDPTimeTotal;
extern volatile OSTime gRDPTimeAcc;
extern volatile OSTime gGraphUpdatePeriod;

void Graph_InitTHGA(GraphicsContext* gfxCtx);
void Graph_TaskSet00(GraphicsContext* gfxCtx);
void GameState_ReqPadData(GameState* gameState);
void GameState_Update(GameState* gameState);

// The number of VIs to wait for the current frame.
extern s32 recomp_frame_vis;
// Extra VIs to wait for the current frame.
extern s32 recomp_extra_vis;
// Lag VIs to stall for after the current frame, before the next input poll. Used to emulate console lag frames.
extern s32 recomp_lag_vis;

// Called every frame after the game state has been updated and before the display lists are finalized.
void graph_pre_submit_hook(GraphicsContext* gfxCtx, GameState* gameState);

#endif
