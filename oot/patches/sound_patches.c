#include "patches.h"
#include "../../patches/sound.h"
#include "audio.h"
#include "audiothread_cmd.h"
#include "seqcmd.h"
#include "sequence.h"
#include "sfx.h"
#include "stack_pad.h"
#include "lifemeter.h"
#include "play_state.h"
#include "player.h"
#include "save.h"
#include "z_lib.h"

void Audio_ProcessSeqCmd(u32 cmd);
extern u8 sNumSeqRequests[4];

static bool is_bgm_player(u8 player_index) {
    return player_index == SEQ_PLAYER_BGM_MAIN || player_index == SEQ_PLAYER_BGM_SUB;
}

// @recomp Patched to apply the BGM volume setting.
RECOMP_PATCH void Audio_UpdateActiveSequences(void) {
    u32 tempoCmd;
    u8 tempoOp;
    u16 tempoPrev;
    u16 tempoTarget;
    u8 setupOp;
    u8 targetSeqPlayerIndex;
    u8 setupVal2;
    u8 setupVal1;
    u16 seqId;
    STACK_PADS(s32, 2);
    u16 channelMask;
    u32 retMsg;
    f32 volume;
    u8 tempoTimer;
    u8 seqPlayerIndex;
    u8 j;
    u8 channelIndex;

    for (seqPlayerIndex = 0; seqPlayerIndex < 4; seqPlayerIndex++) {

        // The setup for this block of code was not fully implemented until Majora's Mask.
        // The intent was to load soundfonts asynchronously before playing a
        // sequence in `Audio_StartSequence` using `(seqArgs & 0x80)`.
        // Checks if the requested sequence is finished loading fonts
        if (gActiveSeqs[seqPlayerIndex].isWaitingForFonts) {
            switch (func_800E5E20(&retMsg)) {
                case SEQ_PLAYER_BGM_MAIN + 1:
                case SEQ_PLAYER_FANFARE + 1:
                case SEQ_PLAYER_SFX + 1:
                case SEQ_PLAYER_BGM_SUB + 1:
                    // The fonts have been loaded successfully.
                    gActiveSeqs[seqPlayerIndex].isWaitingForFonts = false;
                    // Queue the same command that was stored previously
                    // The code to store this command is missing in OoT, so no command is executed
                    Audio_ProcessSeqCmd(gActiveSeqs[seqPlayerIndex].startSeqCmd);
                    break;
            }
        }

        // Update global volume
        if (gActiveSeqs[seqPlayerIndex].fadeVolUpdate) {
            volume = 1.0f;
            for (j = 0; j < VOL_SCALE_INDEX_MAX; j++) {
                volume *= (gActiveSeqs[seqPlayerIndex].volScales[j] / 127.0f);
            }
            SEQCMD_SET_SEQPLAYER_VOLUME(seqPlayerIndex, gActiveSeqs[seqPlayerIndex].volFadeTimer,
                                        (u8)(volume * 127.0f));
            gActiveSeqs[seqPlayerIndex].fadeVolUpdate = false;
        }

        if (gActiveSeqs[seqPlayerIndex].volTimer != 0) {
            gActiveSeqs[seqPlayerIndex].volTimer--;

            if (gActiveSeqs[seqPlayerIndex].volTimer != 0) {
                gActiveSeqs[seqPlayerIndex].volCur -= gActiveSeqs[seqPlayerIndex].volStep;
            } else {
                gActiveSeqs[seqPlayerIndex].volCur = gActiveSeqs[seqPlayerIndex].volTarget;
            }
        }

        // @recomp Send a volume scale command regardless of whether volTimer is active and scale it for background music players.
        {
            f32 cur_volume = gActiveSeqs[seqPlayerIndex].volCur;
            if (is_bgm_player(seqPlayerIndex)) {
                cur_volume *= recomp_get_bgm_volume();
            }
            AUDIOCMD_SEQPLAYER_FADE_VOLUME_SCALE((u32)seqPlayerIndex, cur_volume);
        }

        // Process tempo
        if (gActiveSeqs[seqPlayerIndex].tempoCmd != 0) {
            tempoCmd = gActiveSeqs[seqPlayerIndex].tempoCmd;
            tempoTimer = (tempoCmd & 0xFF0000) >> 15;
            tempoTarget = tempoCmd & 0xFFF;
            if (tempoTimer == 0) {
                tempoTimer++;
            }

            // Process tempo commands
            if (gAudioCtx.seqPlayers[seqPlayerIndex].enabled) {
                tempoPrev = gAudioCtx.seqPlayers[seqPlayerIndex].tempo / SEQTICKS_PER_BEAT;
                tempoOp = (tempoCmd & 0xF000) >> 12;
                switch (tempoOp) {
                    case SEQCMD_SUB_OP_TEMPO_SPEED_UP:
                        // Speed up tempo by `tempoTarget` amount
                        tempoTarget += tempoPrev;
                        break;

                    case SEQCMD_SUB_OP_TEMPO_SLOW_DOWN:
                        // Slow down tempo by `tempoTarget` amount
                        if (tempoTarget < tempoPrev) {
                            tempoTarget = tempoPrev - tempoTarget;
                        }
                        break;

                    case SEQCMD_SUB_OP_TEMPO_SCALE:
                        // Scale tempo by a multiplicative factor
                        tempoTarget = tempoPrev * (tempoTarget / 100.0f);
                        break;

                    case SEQCMD_SUB_OP_TEMPO_RESET:
                        // Reset tempo to original tempo
                        tempoTarget = (gActiveSeqs[seqPlayerIndex].tempoOriginal != 0)
                                          ? gActiveSeqs[seqPlayerIndex].tempoOriginal
                                          : tempoPrev;
                        break;

                    default: // `SEQCMD_SUB_OP_TEMPO_SET`
                        // `tempoTarget` is the new tempo
                        break;
                }

                if (tempoTarget > 300) {
                    tempoTarget = 300;
                }

                if (gActiveSeqs[seqPlayerIndex].tempoOriginal == 0) {
                    gActiveSeqs[seqPlayerIndex].tempoOriginal = tempoPrev;
                }

                gActiveSeqs[seqPlayerIndex].tempoTarget = tempoTarget;
                gActiveSeqs[seqPlayerIndex].tempoCur = gAudioCtx.seqPlayers[seqPlayerIndex].tempo / SEQTICKS_PER_BEAT;
                gActiveSeqs[seqPlayerIndex].tempoStep =
                    (gActiveSeqs[seqPlayerIndex].tempoCur - gActiveSeqs[seqPlayerIndex].tempoTarget) / tempoTimer;
                gActiveSeqs[seqPlayerIndex].tempoTimer = tempoTimer;
                gActiveSeqs[seqPlayerIndex].tempoCmd = 0;
            }
        }

        // Step tempo to target
        if (gActiveSeqs[seqPlayerIndex].tempoTimer != 0) {
            gActiveSeqs[seqPlayerIndex].tempoTimer--;
            if (gActiveSeqs[seqPlayerIndex].tempoTimer != 0) {
                gActiveSeqs[seqPlayerIndex].tempoCur -= gActiveSeqs[seqPlayerIndex].tempoStep;
            } else {
                gActiveSeqs[seqPlayerIndex].tempoCur = gActiveSeqs[seqPlayerIndex].tempoTarget;
            }

            AUDIOCMD_SEQPLAYER_SET_TEMPO((u32)seqPlayerIndex, gActiveSeqs[seqPlayerIndex].tempoCur);
        }

        // Update channel volumes
        if (gActiveSeqs[seqPlayerIndex].volChannelFlags != 0) {
            for (channelIndex = 0; channelIndex < SEQ_NUM_CHANNELS; channelIndex++) {
                if (gActiveSeqs[seqPlayerIndex].channelData[channelIndex].volTimer != 0) {
                    gActiveSeqs[seqPlayerIndex].channelData[channelIndex].volTimer--;
                    if (gActiveSeqs[seqPlayerIndex].channelData[channelIndex].volTimer != 0) {
                        gActiveSeqs[seqPlayerIndex].channelData[channelIndex].volCur -=
                            gActiveSeqs[seqPlayerIndex].channelData[channelIndex].volStep;
                    } else {
                        gActiveSeqs[seqPlayerIndex].channelData[channelIndex].volCur =
                            gActiveSeqs[seqPlayerIndex].channelData[channelIndex].volTarget;
                        gActiveSeqs[seqPlayerIndex].volChannelFlags ^= (1 << channelIndex);
                    }

                    AUDIOCMD_CHANNEL_SET_VOL_SCALE(seqPlayerIndex, (u32)channelIndex,
                                                   gActiveSeqs[seqPlayerIndex].channelData[channelIndex].volCur);
                }
            }
        }

        // Update frequencies
        if (gActiveSeqs[seqPlayerIndex].freqScaleChannelFlags != 0) {
            for (channelIndex = 0; channelIndex < SEQ_NUM_CHANNELS; channelIndex++) {
                if (gActiveSeqs[seqPlayerIndex].channelData[channelIndex].freqScaleTimer != 0) {
                    gActiveSeqs[seqPlayerIndex].channelData[channelIndex].freqScaleTimer--;
                    if (gActiveSeqs[seqPlayerIndex].channelData[channelIndex].freqScaleTimer != 0) {
                        gActiveSeqs[seqPlayerIndex].channelData[channelIndex].freqScaleCur -=
                            gActiveSeqs[seqPlayerIndex].channelData[channelIndex].freqScaleStep;
                    } else {
                        gActiveSeqs[seqPlayerIndex].channelData[channelIndex].freqScaleCur =
                            gActiveSeqs[seqPlayerIndex].channelData[channelIndex].freqScaleTarget;
                        gActiveSeqs[seqPlayerIndex].freqScaleChannelFlags ^= (1 << channelIndex);
                    }

                    AUDIOCMD_CHANNEL_SET_FREQ_SCALE(seqPlayerIndex, (u32)channelIndex,
                                                    gActiveSeqs[seqPlayerIndex].channelData[channelIndex].freqScaleCur);
                }
            }
        }

        // Process setup commands
        if (gActiveSeqs[seqPlayerIndex].setupCmdNum != 0) {
            // If there is a SeqCmd to reset the audio heap queued, then drop all setup commands
            if (!Audio_IsSeqCmdNotQueued(SEQCMD_OP_RESET_AUDIO_HEAP << 28, SEQCMD_OP_MASK)) {
                gActiveSeqs[seqPlayerIndex].setupCmdNum = 0;
                return;
            }

            // Only process setup commands once the timer reaches zero
            if (gActiveSeqs[seqPlayerIndex].setupCmdTimer != 0) {
                gActiveSeqs[seqPlayerIndex].setupCmdTimer--;
                continue;
            }

            // Only process setup commands if `seqPlayerIndex` if no longer playing
            // i.e. the `seqPlayer` is no longer enabled
            if (gAudioCtx.seqPlayers[seqPlayerIndex].enabled) {
                continue;
            }

            for (j = 0; j < gActiveSeqs[seqPlayerIndex].setupCmdNum; j++) {
                setupOp = (gActiveSeqs[seqPlayerIndex].setupCmd[j] & 0x00F00000) >> 20;
                targetSeqPlayerIndex = (gActiveSeqs[seqPlayerIndex].setupCmd[j] & 0x000F0000) >> 16;
                setupVal2 = (gActiveSeqs[seqPlayerIndex].setupCmd[j] & 0xFF00) >> 8;
                setupVal1 = gActiveSeqs[seqPlayerIndex].setupCmd[j] & 0xFF;

                switch (setupOp) {
                    case SEQCMD_SUB_OP_SETUP_RESTORE_SEQPLAYER_VOLUME:
                        // Restore `targetSeqPlayerIndex` volume back to normal levels
                        Audio_SetVolumeScale(targetSeqPlayerIndex, VOL_SCALE_INDEX_FANFARE, 0x7F, setupVal1);
                        break;

                    case SEQCMD_SUB_OP_SETUP_RESTORE_SEQPLAYER_VOLUME_IF_QUEUED:
                        // Restore `targetSeqPlayerIndex` volume back to normal levels,
                        // but only if the number of sequence queue requests from `sSeqRequests`
                        // exactly matches the argument to the command
                        if (setupVal1 == sNumSeqRequests[seqPlayerIndex]) {
                            Audio_SetVolumeScale(targetSeqPlayerIndex, VOL_SCALE_INDEX_FANFARE, 0x7F, setupVal2);
                        }
                        break;

                    case SEQCMD_SUB_OP_SETUP_SEQ_UNQUEUE:
                        // Unqueue `seqPlayerIndex` from sSeqRequests
                        //! @bug this command does not work as intended as unqueueing
                        //! the sequence relies on `gActiveSeqs[seqPlayerIndex].seqId`
                        //! However, `gActiveSeqs[seqPlayerIndex].seqId` is reset before the sequence on
                        //! `seqPlayerIndex` is requested to stop, i.e. before the sequence is disabled and setup
                        //! commands (including this command) can run. A simple fix would have been to unqueue based on
                        //! `gActiveSeqs[seqPlayerIndex].prevSeqId` instead
                        SEQCMD_UNQUEUE_SEQUENCE(seqPlayerIndex, 0, gActiveSeqs[seqPlayerIndex].seqId);
                        break;

                    case SEQCMD_SUB_OP_SETUP_RESTART_SEQ:
                        // Restart the currently active sequence on `targetSeqPlayerIndex` with full volume.
                        // Sequence on `targetSeqPlayerIndex` must still be active to play (can be muted)
                        SEQCMD_PLAY_SEQUENCE(targetSeqPlayerIndex, 1, 0, gActiveSeqs[targetSeqPlayerIndex].seqId);
                        gActiveSeqs[targetSeqPlayerIndex].fadeVolUpdate = true;
                        gActiveSeqs[targetSeqPlayerIndex].volScales[VOL_SCALE_INDEX_FANFARE] = 0x7F;
                        break;

                    case SEQCMD_SUB_OP_SETUP_TEMPO_SCALE:
                        // Scale tempo by a multiplicative factor
                        SEQCMD_SCALE_TEMPO(targetSeqPlayerIndex, setupVal2, setupVal1);
                        break;

                    case SEQCMD_SUB_OP_SETUP_TEMPO_RESET:
                        // Reset tempo to previous tempo
                        SEQCMD_RESET_TEMPO(targetSeqPlayerIndex, setupVal1);
                        break;

                    case SEQCMD_SUB_OP_SETUP_PLAY_SEQ:
                        // Play the requested sequence
                        // Uses the fade timer set by `SEQCMD_SUB_OP_SETUP_SET_FADE_TIMER`
                        seqId = gActiveSeqs[seqPlayerIndex].setupCmd[j] & 0xFFFF;
                        SEQCMD_PLAY_SEQUENCE(targetSeqPlayerIndex, gActiveSeqs[targetSeqPlayerIndex].setupFadeTimer, 0,
                                             seqId);
                        Audio_SetVolumeScale(targetSeqPlayerIndex, VOL_SCALE_INDEX_FANFARE, 0x7F, 0);
                        gActiveSeqs[targetSeqPlayerIndex].setupFadeTimer = 0;
                        break;

                    case SEQCMD_SUB_OP_SETUP_SET_FADE_TIMER:
                        // A command specifically to support `SEQCMD_SUB_OP_SETUP_PLAY_SEQ`
                        // Sets the fade timer for the sequence requested in `SEQCMD_SUB_OP_SETUP_PLAY_SEQ`
                        gActiveSeqs[seqPlayerIndex].setupFadeTimer = setupVal2;
                        break;

                    case SEQCMD_SUB_OP_SETUP_RESTORE_SEQPLAYER_VOLUME_WITH_SCALE_INDEX:
                        // Restore the volume back to default levels
                        // Allows a `scaleIndex` to be specified.
                        Audio_SetVolumeScale(targetSeqPlayerIndex, setupVal2, 0x7F, setupVal1);
                        break;

                    case SEQCMD_SUB_OP_SETUP_POP_PERSISTENT_CACHE:
                        // Discard audio data by popping one more audio caches from the audio heap
                        if (setupVal1 & (1 << SEQUENCE_TABLE)) {
                            AUDIOCMD_GLOBAL_POP_PERSISTENT_CACHE(SEQUENCE_TABLE);
                        }
                        if (setupVal1 & (1 << FONT_TABLE)) {
                            AUDIOCMD_GLOBAL_POP_PERSISTENT_CACHE(FONT_TABLE);
                        }
                        if (setupVal1 & (1 << SAMPLE_TABLE)) {
                            AUDIOCMD_GLOBAL_POP_PERSISTENT_CACHE(SAMPLE_TABLE);
                        }
                        break;

                    case SEQCMD_SUB_OP_SETUP_SET_CHANNEL_DISABLE_MASK:
                        // Disable (or reenable) specific channels of `targetSeqPlayerIndex`
                        channelMask = gActiveSeqs[seqPlayerIndex].setupCmd[j] & 0xFFFF;
                        SEQCMD_SET_CHANNEL_DISABLE_MASK(targetSeqPlayerIndex, channelMask);
                        break;

                    case SEQCMD_SUB_OP_SETUP_SET_SEQPLAYER_FREQ:
                        // Scale all channels of `targetSeqPlayerIndex`
                        SEQCMD_SET_SEQPLAYER_FREQ(targetSeqPlayerIndex, setupVal2, (setupVal1 * 10) & 0xFFFF);
                        break;
                }
            }

            gActiveSeqs[seqPlayerIndex].setupCmdNum = 0;
        }
    }
}

// @recomp Patched to add the ability to turn off low health beeps.
RECOMP_PATCH void Health_UpdateBeatingHeart(PlayState* play) {
    InterfaceContext* interfaceCtx = &play->interfaceCtx;

    if (interfaceCtx->beatingHeartOscillatorDirection != 0) {
        interfaceCtx->beatingHeartOscillator--;
        if (interfaceCtx->beatingHeartOscillator <= 0) {
            interfaceCtx->beatingHeartOscillator = 0;
            interfaceCtx->beatingHeartOscillatorDirection = 0;
            // @recomp Additional check for whether low health beeps are enabled.
            if (recomp_get_low_health_beeps_enabled() && !Player_InCsMode(play) && !IS_PAUSED(&play->pauseCtx) && Health_IsCritical() && !Play_InCsMode(play)) {
                Sfx_PlaySfxCentered(NA_SE_SY_HITPOINT_ALARM);
            }
        }
    } else {
        interfaceCtx->beatingHeartOscillator++;
        if (interfaceCtx->beatingHeartOscillator >= 10) {
            interfaceCtx->beatingHeartOscillator = 10;
            interfaceCtx->beatingHeartOscillatorDirection = 1;
        }
    }
}
