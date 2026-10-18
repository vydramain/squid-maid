#pragma once

// Squid-maid sound effects on a single voice: a split and a glass hit rotate through their variants. A cutting event cuts
// the one still playing; a bounce or a glass hit is dropped while the voice is busy.

#include <array>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <span>

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
    bell = 3,
    cheers = 4,
    can_glass = 5,
    glass_hit = 6,
    count,
};

// One entry per event, in enum order.
constexpr int SQUID_SOUND_COUNT = static_cast<int>(squid_sound_event::count);

// Log tag of sound messages.
inline constexpr const char *squid_sound_tag = "sound";

// Asset names on the disc: each .wav under assets/snd is baked to the .pcm name, as example-cpp names its clack.
// The split variants: sfx_can_hit_1..7.
constexpr const char *SQUID_SPLIT_ASSETS[] = {
    "sfx_can_hit_1.pcm",
    "sfx_can_hit_2.pcm",
    "sfx_can_hit_3.pcm",
    "sfx_can_hit_4.pcm",
    "sfx_can_hit_5.pcm",
    "sfx_can_hit_6.pcm",
    "sfx_can_hit_7.pcm",
};
constexpr int SQUID_SPLIT_VARIANTS = static_cast<int>(std::size(SQUID_SPLIT_ASSETS));

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

// The glass hit variants: sfx_glass_hit_1..3; each hit plays the next one in turn.
constexpr const char *SQUID_GLASS_HIT_ASSETS[] = { "sfx_glass_hit_1.pcm", "sfx_glass_hit_2.pcm", "sfx_glass_hit_3.pcm" };

constexpr const char *SQUID_CALIBRATE_ASSETS[] = { "sfx_calibrate.pcm" };
constexpr const char *SQUID_BOUNCE_ASSETS[] = { "sfx_can_flat.pcm" };
constexpr const char *SQUID_BELL_ASSETS[] = { "sfx_bell.pcm" };
constexpr const char *SQUID_CHEERS_ASSETS[] = { "sfx_glass_double_cheers.pcm" };
constexpr const char *SQUID_CAN_GLASS_ASSETS[] = { "sfx_can_glass.pcm" };

// One row per event, in enum order: its assets (a turn picks one) and whether it may cut a sound still playing. A bounce
// and a glass hit are dropped while the voice is busy, so a pile of them never cuts a split.
struct squid_sound_row {
    squid_sound_event event;
    std::span<const char *const> assets;
    bool cuts;
};
constexpr std::array<squid_sound_row, SQUID_SOUND_COUNT> SQUID_SOUND_ROWS = { {
    { squid_sound_event::calibrate, SQUID_CALIBRATE_ASSETS, true },
    { squid_sound_event::split, SQUID_SPLIT_ASSETS, true },
    { squid_sound_event::bounce, SQUID_BOUNCE_ASSETS, false },
    { squid_sound_event::bell, SQUID_BELL_ASSETS, true },
    { squid_sound_event::cheers, SQUID_CHEERS_ASSETS, true },
    { squid_sound_event::can_glass, SQUID_CAN_GLASS_ASSETS, true },
    { squid_sound_event::glass_hit, SQUID_GLASS_HIT_ASSETS, false },
} };

// Row i must be event i and hold at least one asset; a row out of place would play another event's sound.
constexpr bool squid_sound_rows_in_order()
{
    for (int i = 0; i < SQUID_SOUND_COUNT; ++i) {
        const squid_sound_row &row = SQUID_SOUND_ROWS[static_cast<std::size_t>(i)];
        if (static_cast<int>(row.event) != i || row.assets.empty()) {
            return false;
        }
    }
    return true;
}
static_assert(squid_sound_rows_in_order(), "SQUID_SOUND_ROWS must hold each event in enum order, with an asset");

// The variants of an event, and the flat index of its first asset: the assets are numbered row by row.
constexpr int squid_sound_variants(int event)
{
    return static_cast<int>(SQUID_SOUND_ROWS[static_cast<std::size_t>(event)].assets.size());
}
constexpr int squid_sound_first_asset(int event)
{
    int first = 0;
    for (int i = 0; i < event; ++i) {
        first += squid_sound_variants(i);
    }
    return first;
}
constexpr int SQUID_SOUND_ASSET_COUNT = squid_sound_first_asset(SQUID_SOUND_COUNT);

// The one voice: bit 0 of the voice mask. Every event is played on it. A cutting event cuts the sound playing; a bounce or a
// glass hit is dropped while it plays.
constexpr int64_t SQUID_SOUND_VOICE = 1;

// Full level of the sustain and of the volume: the sound module's maximum.
constexpr int16_t SQUID_SOUND_MAX_LEVEL = 32767;

// No envelope: the sample's own shape carries the event, the voice holds full level until the sample ends.
constexpr int16_t SQUID_SOUND_ENVELOPE = 0;

// Sound RAM address of each asset; set by squid_sound_init. rv_cd.h: an address is valid until its resource is reloaded,
// so the table is only good while the disc keeps its audio loaded. turn is each event's next variant: a split's place in
// SQUID_SPLIT_ORDER, a glass hit's index, 0 for an event with one variant.
struct squid_sound {
    int64_t addr[SQUID_SOUND_ASSET_COUNT] = {};
    bool loaded = false;
    int turn[SQUID_SOUND_COUNT] = {};
};

// Resolves the assets on the disc. Returns RV_OK, or the failing lookup's code with its asset named in the log.
inline int squid_sound_init(rv_pdko *pdk, squid_sound &sound)
{
    sound.loaded = false;
    if (pdk == nullptr) {
        RV_LOG_ERR(squid_sound_tag, "init refused: pdk is null");
        return RV_ERR_INVAL;
    }
    int asset = 0;
    for (const squid_sound_row &row : SQUID_SOUND_ROWS) {
        for (const char *name : row.assets) {
            const int64_t addr = rv_cd_resource_addr(rv_pdko_cd(pdk), RV_CD_RESOURCE_AUDIO, name);
            if (addr < 0) {
                RV_LOG_ERR(squid_sound_tag, "asset {} is not on the disc: code {}", name, addr);
                return static_cast<int>(addr);
            }
            sound.addr[asset] = addr;
            ++asset;
        }
    }
    sound.loaded = true;
    return RV_OK;
}

// The variant the event plays now: a split takes its turn in SQUID_SPLIT_ORDER, the other events their turn.
inline int squid_sound_variant(const squid_sound &sound, squid_sound_event event)
{
    const int index = static_cast<int>(event);
    return event == squid_sound_event::split ? SQUID_SPLIT_ORDER[sound.turn[index]] : sound.turn[index];
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
    const squid_sound_row &row = SQUID_SOUND_ROWS[static_cast<std::size_t>(index)];
    const int busy = row.cuts ? 0 : squid_sound_voice_busy(pdk);
    if (busy < 0) {
        RV_LOG_ERR(squid_sound_tag, "voice status failed: code {}", busy);
        return busy;
    }
    if (busy != 0) {
        return RV_OK;
    }
    const int variant = squid_sound_variant(sound, event);
    const int asset = squid_sound_first_asset(index) + variant;
    const char *name = row.assets[static_cast<std::size_t>(variant)];
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
        RV_LOG_ERR(squid_sound_tag, "voice setup for {} failed: code {}", name, setup_code);
        return static_cast<int>(setup_code);
    }
    const int64_t play_code = rv_ca_voice_play(rv_pdko_ca(pdk), SQUID_SOUND_VOICE);
    if (play_code < 0) {
        RV_LOG_ERR(squid_sound_tag, "voice play for {} failed: code {}", name, play_code);
        return static_cast<int>(play_code);
    }
    // The turn moves on only once its variant has started, so a failed play does not skip one.
    sound.turn[index] = (sound.turn[index] + 1) % squid_sound_variants(index);
    started = true;
    return RV_OK;
}

} // namespace squid
