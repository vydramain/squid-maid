#pragma once

// Squid-maid sound effects: three one-shots, one per event, on a single voice.
// A new event cuts the one still playing.

#include <cstdint>

#include "pdk/ca/rv_ca.h"
#include "pdk/ca/rv_loop.h"
#include "pdk/ca/rv_voice_conf.h"
#include "pdk/cd/rv_cd.h"
#include "pdk/rv_err.h"
#include "pdk/rv_pdko.h"
#include "pdklib/rv_logs/rv_logs.hpp"

namespace squid
{

enum class squid_sound_event : int {
    calibrate = 0,
    start = 1,
    split = 2,
};

// One entry per event, in enum order.
constexpr int SQUID_SOUND_COUNT = 3;

// Log tag of sound messages.
inline constexpr const char *squid_sound_tag = "sound";

// Asset names on the disc: each .wav under assets/snd is baked to the .pcm name, as example-cpp names its clack.
constexpr const char *SQUID_SOUND_ASSET[SQUID_SOUND_COUNT] = {
    "sfx_calibrate.pcm",
    "sfx_start.pcm",
    "sfx_hit.pcm",
};

// The one voice: bit 0 of the voice mask. Every event is played on it, so a new event cuts the previous one.
constexpr int64_t SQUID_SOUND_VOICE = 1;

// Full level of the sustain and of the volume: the sound module's maximum.
constexpr int16_t SQUID_SOUND_MAX_LEVEL = 32767;

// No envelope: the sample's own shape carries the event, the voice holds full level until the sample ends.
constexpr int16_t SQUID_SOUND_ENVELOPE = 0;

// Sound RAM address of each event's asset; set by squid_sound_init. rv_cd.h: an address is valid until its resource
// is reloaded, so the table is only good while the disc keeps its audio loaded.
struct squid_sound {
    int64_t addr[SQUID_SOUND_COUNT] = {};
    bool loaded = false;
};

// Resolves the four assets on the disc. Returns RV_OK, or the failing lookup's code with its asset named in the log.
inline int squid_sound_init(rv_pdko *pdk, squid_sound &sound)
{
    sound.loaded = false;
    if (pdk == nullptr) {
        RV_LOG_ERR(squid_sound_tag, "init refused: pdk is null");
        return RV_ERR_INVAL;
    }
    for (int i = 0; i < SQUID_SOUND_COUNT; ++i) {
        const int64_t addr = rv_cd_resource_addr(rv_pdko_cd(pdk), RV_CD_RESOURCE_AUDIO, SQUID_SOUND_ASSET[i]);
        if (addr < 0) {
            RV_LOG_ERR(squid_sound_tag, "asset {} is not on the disc: code {}", SQUID_SOUND_ASSET[i], addr);
            return static_cast<int>(addr);
        }
        sound.addr[i] = addr;
    }
    sound.loaded = true;
    return RV_OK;
}

// Plays the event's sound once on the voice. Returns RV_OK, or the failing call's code with the cause in the log.
inline int squid_sound_play(rv_pdko *pdk, const squid_sound &sound, squid_sound_event event)
{
    const int index = static_cast<int>(event);
    if (pdk == nullptr) {
        RV_LOG_ERR(squid_sound_tag, "play refused: pdk is null");
        return RV_ERR_INVAL;
    }
    if (!sound.loaded) {
        RV_LOG_ERR(squid_sound_tag, "play refused: the sounds are not loaded; squid_sound_init must run first");
        return RV_ERR_INVAL;
    }
    if (index < 0 || index >= SQUID_SOUND_COUNT) {
        RV_LOG_ERR(squid_sound_tag, "play refused: event {} is not a sound event", index);
        return RV_ERR_INVAL;
    }
    rv_voice_conf conf = {};
    conf.voice = SQUID_SOUND_VOICE;
    conf.loop_type = RV_LOOP_NONE;
    conf.sample_address = sound.addr[index];
    conf.ar = SQUID_SOUND_ENVELOPE;
    conf.dr = SQUID_SOUND_ENVELOPE;
    conf.sr = SQUID_SOUND_ENVELOPE;
    conf.rr = SQUID_SOUND_ENVELOPE;
    conf.sl = SQUID_SOUND_MAX_LEVEL;
    conf.volume = SQUID_SOUND_MAX_LEVEL;
    conf.volume_l = SQUID_SOUND_MAX_LEVEL;
    conf.volume_r = SQUID_SOUND_MAX_LEVEL;
    const int64_t setup_code = rv_ca_voice_setup(rv_pdko_ca(pdk), &conf);
    if (setup_code < 0) {
        RV_LOG_ERR(squid_sound_tag, "voice setup for {} failed: code {}", SQUID_SOUND_ASSET[index], setup_code);
        return static_cast<int>(setup_code);
    }
    const int64_t play_code = rv_ca_voice_play(rv_pdko_ca(pdk), SQUID_SOUND_VOICE);
    if (play_code < 0) {
        RV_LOG_ERR(squid_sound_tag, "voice play for {} failed: code {}", SQUID_SOUND_ASSET[index], play_code);
        return static_cast<int>(play_code);
    }
    return RV_OK;
}

} // namespace squid
