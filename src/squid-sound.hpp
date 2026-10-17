#pragma once

// Squid-maid sound effects on a single voice: a calibrate one-shot, a split (seven variants, one each time in turn) and a
// bounce one-shot. A split or a calibrate cuts the one still playing; a bounce is dropped while the voice is busy.

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
    split = 1,
    bounce = 2,
};

// One entry per event, in enum order.
constexpr int SQUID_SOUND_COUNT = 3;

// Log tag of sound messages.
inline constexpr const char *squid_sound_tag = "sound";

// The split variants: sfx_can_hit_1..7 on the disc.
constexpr int SQUID_SPLIT_VARIANTS = 7;

// A fixed shuffle of the variants: a split plays SQUID_SPLIT_ORDER[turn], turn counting the splits. It is a permutation,
// so no two splits in a row repeat a sound, and it is the same in every run.
constexpr int SQUID_SPLIT_ORDER[SQUID_SPLIT_VARIANTS] = { 3, 0, 6, 1, 5, 2, 4 };

constexpr bool squid_split_order_is_permutation()
{
    bool seen[SQUID_SPLIT_VARIANTS] = {};
    for (const int variant : SQUID_SPLIT_ORDER) {
        if (variant < 0 || variant >= SQUID_SPLIT_VARIANTS || seen[variant]) {
            return false;
        }
        seen[variant] = true;
    }
    return true;
}
static_assert(squid_split_order_is_permutation(), "SQUID_SPLIT_ORDER must list each split variant once");

// Asset names on the disc: each .wav under assets/snd is baked to the .pcm name, as example-cpp names its clack.
constexpr int SQUID_SOUND_ASSET_COUNT = 1 + SQUID_SPLIT_VARIANTS + 1;
constexpr const char *SQUID_SOUND_ASSET[SQUID_SOUND_ASSET_COUNT] = {
    "sfx_calibrate.pcm",
    "sfx_can_hit_1.pcm",
    "sfx_can_hit_2.pcm",
    "sfx_can_hit_3.pcm",
    "sfx_can_hit_4.pcm",
    "sfx_can_hit_5.pcm",
    "sfx_can_hit_6.pcm",
    "sfx_can_hit_7.pcm",
    "sfx_can_flat.pcm",
};

// The first asset of each event: calibrate and bounce have one, a split has the seven variants from its first.
constexpr int SQUID_SOUND_FIRST_ASSET[SQUID_SOUND_COUNT] = { 0, 1, SQUID_SOUND_ASSET_COUNT - 1 };

// Whether the event may cut a sound still playing on the voice. A split and a calibrate cut it; a bounce is dropped while
// the voice is busy, so a pile of bounces never cuts a split.
constexpr bool SQUID_SOUND_CUTS[SQUID_SOUND_COUNT] = { true, true, false };

// The one voice: bit 0 of the voice mask. Every event is played on it. A split or a calibrate cuts the sound playing; a
// bounce is dropped while it plays.
constexpr int64_t SQUID_SOUND_VOICE = 1;

// Full level of the sustain and of the volume: the sound module's maximum.
constexpr int16_t SQUID_SOUND_MAX_LEVEL = 32767;

// No envelope: the sample's own shape carries the event, the voice holds full level until the sample ends.
constexpr int16_t SQUID_SOUND_ENVELOPE = 0;

// Sound RAM address of each asset; set by squid_sound_init. rv_cd.h: an address is valid until its resource is reloaded,
// so the table is only good while the disc keeps its audio loaded. split_turn is the next split's place in the order.
struct squid_sound {
    int64_t addr[SQUID_SOUND_ASSET_COUNT] = {};
    bool loaded = false;
    int split_turn = 0;
};

// Resolves the assets on the disc. Returns RV_OK, or the failing lookup's code with its asset named in the log.
inline int squid_sound_init(rv_pdko *pdk, squid_sound &sound)
{
    sound.loaded = false;
    if (pdk == nullptr) {
        RV_LOG_ERR(squid_sound_tag, "init refused: pdk is null");
        return RV_ERR_INVAL;
    }
    for (int i = 0; i < SQUID_SOUND_ASSET_COUNT; ++i) {
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

// The asset the event plays now: a split takes the variant at split_turn, the other events their only asset.
inline int squid_sound_asset(const squid_sound &sound, squid_sound_event event)
{
    const int variant = event == squid_sound_event::split ? SQUID_SPLIT_ORDER[sound.split_turn] : 0;
    return SQUID_SOUND_FIRST_ASSET[static_cast<int>(event)] + variant;
}

// The voice's busy mask (bit 0 set while a sound plays), or the negative code of the status call.
inline int squid_sound_voice_busy(rv_pdko *pdk)
{
    return static_cast<int>(rv_ca_voice_status(rv_pdko_ca(pdk), SQUID_SOUND_VOICE));
}

// Plays the event's sound once on the voice, unless a non-cutting event finds the voice busy: then nothing starts. started
// tells whether a sound started. Returns RV_OK, or the failing call's code with the cause in the log.
inline int squid_sound_play(rv_pdko *pdk, squid_sound &sound, squid_sound_event event, bool &started)
{
    started = false;
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
    const int busy = SQUID_SOUND_CUTS[index] ? 0 : squid_sound_voice_busy(pdk);
    if (busy < 0) {
        RV_LOG_ERR(squid_sound_tag, "voice status failed: code {}", busy);
        return busy;
    }
    if (busy != 0) {
        return RV_OK;
    }
    const int asset = squid_sound_asset(sound, event);
    rv_voice_conf conf = {};
    conf.voice = SQUID_SOUND_VOICE;
    conf.loop_type = RV_LOOP_NONE;
    conf.sample_address = sound.addr[asset];
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
        RV_LOG_ERR(squid_sound_tag, "voice setup for {} failed: code {}", SQUID_SOUND_ASSET[asset], setup_code);
        return static_cast<int>(setup_code);
    }
    const int64_t play_code = rv_ca_voice_play(rv_pdko_ca(pdk), SQUID_SOUND_VOICE);
    if (play_code < 0) {
        RV_LOG_ERR(squid_sound_tag, "voice play for {} failed: code {}", SQUID_SOUND_ASSET[asset], play_code);
        return static_cast<int>(play_code);
    }
    // The split turn moves on only once its variant has started, so a failed play does not skip one.
    sound.split_turn = event == squid_sound_event::split ? (sound.split_turn + 1) % SQUID_SPLIT_VARIANTS : sound.split_turn;
    started = true;
    return RV_OK;
}

} // namespace squid
