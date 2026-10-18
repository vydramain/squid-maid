#pragma once

// The ambient track: the disc's baked music streamed on its own voice for the whole game, looping. Two sound slots hold
// the stream: the voice plays one and repeats into the other, whose start is marked; each mark refills the slot the
// voice has left with the track's next chunk.

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <new>
#include <vector>

#include "pdk/ca/rv_ca.h"
#include "pdk/ca/rv_loop.h"
#include "pdk/ca/rv_sample.h"
#include "pdk/ca/rv_voice_conf.h"
#include "pdk/cd/rv_cd.h"
#include "pdk/rv_err.h"
#include "pdk/rv_pdko.h"
#include "pdklib/rv_logs/rv_logs.hpp"
#include "squid-sound.hpp"

namespace squid
{

// Log tag of ambient messages.
inline constexpr const char *squid_ambient_tag = "ambient";

// The track as the burner bakes it: disc.toml [music] turns assets/music/ambient.mp3 into this drive asset.
inline constexpr const char *SQUID_AMBIENT_ASSET = "ambient.pcm";

// The baked format: 44100 Hz mono, 16-bit samples.
constexpr int64_t SQUID_AMBIENT_RATE = 44100;
constexpr int64_t SQUID_AMBIENT_SAMPLE_BYTES = 2;

// One chunk is one second of the track. The voice reads a chunk past the mark only after the disc has refilled it, and the
// refill must beat a game frame plus about 60 ms (rv_ca.h): a second is far more. Two chunks take 176400 bytes of the
// 512 KiB sound memory, beside the nine effect wavs, 200556 bytes on disk (du -cb assets/snd/*.wav).
constexpr int64_t SQUID_AMBIENT_CHUNK_BYTES = SQUID_AMBIENT_RATE * SQUID_AMBIENT_SAMPLE_BYTES;

// Its own voice: the effects keep voice 1.
constexpr int64_t SQUID_AMBIENT_VOICE = 2;

// The voice plays one slot while the other is refilled.
constexpr int SQUID_AMBIENT_SLOTS = 2;

// Under the effects, which play at full level: 10000 of 32767, the music level of example-cpp.
constexpr int16_t SQUID_AMBIENT_VOLUME = 10000;

// A mark at address 0 clears the mark (rv_ca.h).
constexpr int64_t SQUID_AMBIENT_NO_MARK = 0;

// The stream's state. track is the whole track in main RAM, about 132 MB for this 25-minute track: rv_cd_asset_read takes
// whole entries only, and this memory is outside the console's sound budget. next_slot is the slot the voice reads after
// the current one; position is the track byte the next chunk starts at. slots_taken and voice_playing record what init
// took, so a failed init can undo it. refill_failing is set while refills fail, so the first failure logs once.
struct squid_ambient {
    std::vector<uint8_t> track;
    std::vector<uint8_t> chunk;
    int64_t slot_addr[SQUID_AMBIENT_SLOTS] = {};
    int next_slot = 0;
    std::size_t position = 0;
    int slots_taken = 0;
    bool voice_playing = false;
    bool refill_failing = false;
};

inline int squid_ambient_other(int slot)
{
    return (slot + 1) % SQUID_AMBIENT_SLOTS;
}

// Copies the chunk at the position into the staging buffer, wrapping at the track's end so the stream loops with no gap.
// Returns the position after the chunk; the caller commits it once the chunk is in a slot.
inline std::size_t squid_ambient_fill(squid_ambient &ambient)
{
    std::size_t pos = ambient.position;
    std::size_t filled = 0;
    while (filled < ambient.chunk.size()) {
        const std::size_t run = std::min(ambient.chunk.size() - filled, ambient.track.size() - pos);
        std::copy_n(ambient.track.data() + pos, run, ambient.chunk.data() + filled);
        filled += run;
        pos = (pos + run) % ambient.track.size();
    }
    return pos;
}

// Writes the staging buffer into `slot`. Returns RV_OK or the failing call's code; the caller logs it.
inline int squid_ambient_write(rv_pdko *pdk, const squid_ambient &ambient, int slot)
{
    rv_sample sample = {};
    sample.data = ambient.chunk.data();
    sample.size = static_cast<int64_t>(ambient.chunk.size());
    const int64_t write_code = rv_ca_sound_asset_write(rv_pdko_ca(pdk), ambient.slot_addr[slot], &sample);
    return write_code < 0 ? static_cast<int>(write_code) : RV_OK;
}

// Sets the voice to read `slot` after the current one, and marks the slot's start so the next frame sees the voice arrive.
// Returns RV_OK or the failing call's code; the caller logs it.
inline int squid_ambient_arm(rv_pdko *pdk, const squid_ambient &ambient, int slot)
{
    rv_ca *ca = rv_pdko_ca(pdk);
    const int64_t repeat_code = rv_ca_voice_repeat(ca, SQUID_AMBIENT_VOICE, ambient.slot_addr[slot]);
    if (repeat_code < 0) {
        return static_cast<int>(repeat_code);
    }
    const int64_t mark_code = rv_ca_voice_mark(ca, SQUID_AMBIENT_VOICE, ambient.slot_addr[slot]);
    return mark_code < 0 ? static_cast<int>(mark_code) : RV_OK;
}

// Sizes a sample buffer. A bad_alloc is RV_ERR_NOMEM, logged with the buffer's name.
inline int squid_ambient_resize(std::vector<uint8_t> &bytes, int64_t size, const char *name)
{
    try {
        bytes.resize(static_cast<std::size_t>(size));
    } catch (const std::bad_alloc &) {
        RV_LOG_ERR(squid_ambient_tag, "{} of {} bytes does not fit in main RAM", name, size);
        return RV_ERR_NOMEM;
    }
    return RV_OK;
}

// Reads the whole track into main RAM. Returns RV_OK, or the failing call's code with the cause in the log.
inline int squid_ambient_load(rv_pdko *pdk, squid_ambient &ambient)
{
    rv_cd *cd = rv_pdko_cd(pdk);
    const int64_t handle = rv_cd_asset_open(cd, SQUID_AMBIENT_ASSET);
    if (handle < 0) {
        RV_LOG_ERR(squid_ambient_tag, "track {} is not on the disc: code {}", SQUID_AMBIENT_ASSET, handle);
        return static_cast<int>(handle);
    }
    const int64_t size = rv_cd_asset_size(cd, handle);
    if (size < 0) {
        RV_LOG_ERR(squid_ambient_tag, "track {} has no size: code {}", SQUID_AMBIENT_ASSET, size);
        return static_cast<int>(size);
    }
    // The wrap in squid_ambient_fill needs a non-empty track of whole samples.
    if (size == 0 || size % SQUID_AMBIENT_SAMPLE_BYTES != 0) {
        RV_LOG_ERR(squid_ambient_tag, "track {} is {} bytes: not whole 16-bit samples", SQUID_AMBIENT_ASSET, size);
        return RV_ERR_INVAL;
    }
    const int resize_code = squid_ambient_resize(ambient.track, size, SQUID_AMBIENT_ASSET);
    if (resize_code != RV_OK) {
        return resize_code;
    }
    const int64_t read = rv_cd_asset_read(cd, handle, ambient.track.data(), size);
    if (read != size) {
        RV_LOG_ERR(squid_ambient_tag, "track {} read {} of {} bytes", SQUID_AMBIENT_ASSET, read, size);
        return RV_ERR_IO;
    }
    return RV_OK;
}

// Takes the slots, fills them with the first two chunks, starts the voice on slot 0 and arms slot 1. Records what it took
// in the state. Returns RV_OK, or the failing call's code; the caller logs it.
inline int squid_ambient_start(rv_pdko *pdk, squid_ambient &ambient)
{
    rv_ca *ca = rv_pdko_ca(pdk);
    const int chunk_code = squid_ambient_resize(ambient.chunk, SQUID_AMBIENT_CHUNK_BYTES, "chunk");
    if (chunk_code != RV_OK) {
        return chunk_code;
    }
    for (int slot = 0; slot < SQUID_AMBIENT_SLOTS; ++slot) {
        const int64_t addr = rv_ca_sound_asset_malloc(ca, SQUID_AMBIENT_CHUNK_BYTES);
        if (addr < 0) {
            RV_LOG_ERR(squid_ambient_tag, "slot {} malloc failed: code {}", slot, addr);
            return static_cast<int>(addr);
        }
        ambient.slot_addr[slot] = addr;
        ambient.slots_taken = slot + 1;
    }
    for (int slot = 0; slot < SQUID_AMBIENT_SLOTS; ++slot) {
        const std::size_t next = squid_ambient_fill(ambient);
        const int write_code = squid_ambient_write(pdk, ambient, slot);
        if (write_code != RV_OK) {
            return write_code;
        }
        ambient.position = next;
    }
    rv_voice_conf conf = {};
    conf.voice = SQUID_AMBIENT_VOICE;
    conf.loop_type = RV_LOOP_FOREVER;
    conf.sample_address = ambient.slot_addr[0];
    conf.ar = SQUID_SOUND_ENVELOPE;
    conf.dr = SQUID_SOUND_ENVELOPE;
    conf.sr = SQUID_SOUND_ENVELOPE;
    conf.rr = SQUID_SOUND_ENVELOPE;
    conf.sl = SQUID_SOUND_MAX_LEVEL;
    conf.volume = SQUID_AMBIENT_VOLUME;
    conf.volume_l = SQUID_SOUND_MAX_LEVEL;
    conf.volume_r = SQUID_SOUND_MAX_LEVEL;
    const int64_t setup_code = rv_ca_voice_setup(ca, &conf);
    if (setup_code < 0) {
        RV_LOG_ERR(squid_ambient_tag, "voice setup failed: code {}", setup_code);
        return static_cast<int>(setup_code);
    }
    const int64_t play_code = rv_ca_voice_play(ca, SQUID_AMBIENT_VOICE);
    if (play_code < 0) {
        RV_LOG_ERR(squid_ambient_tag, "voice play failed: code {}", play_code);
        return static_cast<int>(play_code);
    }
    ambient.voice_playing = true;
    ambient.next_slot = squid_ambient_other(0);
    return squid_ambient_arm(pdk, ambient, ambient.next_slot);
}

// Undoes a failed init: drops the track, clears the mark and stops the voice if it played, then frees the slots taken.
// A voice that will not stop keeps its slots, since the disc refuses to free a busy voice's slot; that case is logged.
inline void squid_ambient_unwind(rv_pdko *pdk, squid_ambient &ambient)
{
    rv_ca *ca = rv_pdko_ca(pdk);
    ambient.track = {};
    ambient.chunk = {};
    if (ambient.voice_playing) {
        const int64_t mark_code = rv_ca_voice_mark(ca, SQUID_AMBIENT_VOICE, SQUID_AMBIENT_NO_MARK);
        if (mark_code < 0) {
            RV_LOG_WARN(squid_ambient_tag, "mark clear failed: code {}", mark_code);
        }
        // One retry: a stop that fails twice leaves voice 2 on its chunks.
        int64_t stop_code = rv_ca_voice_stop(ca, SQUID_AMBIENT_VOICE);
        if (stop_code < 0) {
            stop_code = rv_ca_voice_stop(ca, SQUID_AMBIENT_VOICE);
        }
        if (stop_code < 0) {
            RV_LOG_ERR(squid_ambient_tag,
                "voice {} still plays after a failed stop: code {}; its slots are kept",
                SQUID_AMBIENT_VOICE,
                stop_code);
            return;
        }
    }
    for (int slot = 0; slot < ambient.slots_taken; ++slot) {
        const int64_t free_code = rv_ca_sound_asset_free(ca, ambient.slot_addr[slot]);
        if (free_code < 0) {
            RV_LOG_WARN(squid_ambient_tag, "slot {} not freed: code {}", slot, free_code);
        }
    }
}

// Loads the track and starts the stream. Returns RV_OK, or the failing call's code with the cause in the log. A failure is
// undone here, except a voice that will not stop: it keeps playing its last two chunks, and its slots are kept.
inline int squid_ambient_init(rv_pdko *pdk, squid_ambient &ambient)
{
    if (pdk == nullptr) {
        RV_LOG_ERR(squid_ambient_tag, "init refused: pdk is null");
        return RV_ERR_INVAL;
    }
    int code = squid_ambient_load(pdk, ambient);
    if (code == RV_OK) {
        code = squid_ambient_start(pdk, ambient);
    }
    if (code != RV_OK) {
        RV_LOG_ERR(squid_ambient_tag, "init failed: code {}", code);
        squid_ambient_unwind(pdk, ambient);
        return code;
    }
    RV_LOG_INFO(squid_ambient_tag,
        "track {} bytes, streamed in {} byte chunks, looping on voice {}",
        ambient.track.size(),
        SQUID_AMBIENT_CHUNK_BYTES,
        SQUID_AMBIENT_VOICE);
    return RV_OK;
}

// Logs the first failure of a run of refill failures; the step still returns the code every frame, for the caller's count.
inline int squid_ambient_refill_failed(squid_ambient &ambient, int code)
{
    if (!ambient.refill_failing) {
        RV_LOG_ERR(squid_ambient_tag, "refill failed: code {}; logged once until a refill succeeds", code);
        ambient.refill_failing = true;
    }
    return code;
}

// Refills the slot the voice has left once it reaches the armed slot. Runs every frame; a frame with no mark does nothing.
// A failed step is retried on the next frame: the mark stays raised until the arm sets a new one.
inline int squid_ambient_step(rv_pdko *pdk, squid_ambient &ambient)
{
    const int64_t marked = rv_ca_voice_marked(rv_pdko_ca(pdk), SQUID_AMBIENT_VOICE);
    if (marked < 0) {
        return squid_ambient_refill_failed(ambient, static_cast<int>(marked));
    }
    if ((marked & SQUID_AMBIENT_VOICE) == 0) {
        return RV_OK;
    }
    const int free_slot = squid_ambient_other(ambient.next_slot);
    const std::size_t next = squid_ambient_fill(ambient);
    const int write_code = squid_ambient_write(pdk, ambient, free_slot);
    if (write_code != RV_OK) {
        return squid_ambient_refill_failed(ambient, write_code);
    }
    const int arm_code = squid_ambient_arm(pdk, ambient, free_slot);
    if (arm_code != RV_OK) {
        return squid_ambient_refill_failed(ambient, arm_code);
    }
    ambient.next_slot = free_slot;
    ambient.position = next;
    if (ambient.refill_failing) {
        ambient.refill_failing = false;
        RV_LOG_INFO(squid_ambient_tag, "refill recovered at track position {}", ambient.position);
    }
    RV_LOG_DBG(squid_ambient_tag, "refilled slot {}, track position {}", free_slot, ambient.position);
    return RV_OK;
}

} // namespace squid
