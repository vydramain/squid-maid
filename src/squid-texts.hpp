#pragma once

// Every string the disc shows (code-rules, 11), one set per language. Text is UTF-8. Draw it with a font built with
// rv_font_block_cyrillic: that atlas has the Cyrillic block. The ASCII punctuation of rv_font_data.hpp is in every atlas,
// so no dashes, middle dots or typographic quotes here.
// The font is 8 px a character and the frame 320 px wide: a line of the round holds 39 characters, so the round's screens
// are one line each. The calibration screen wraps at spaces; '\n' is a hard break (squid_hud_wrap in squid-hud.hpp).

#include <array>
#include <cstddef>
#include <string_view>

namespace squid
{

enum class squid_lang {
    ru,
    en,
};
constexpr std::size_t SQUID_LANG_COUNT = static_cast<std::size_t>(squid_lang::en) + 1;

// The language pick, shown before a language is chosen: the same in both.
constexpr std::string_view SQUID_TEXT_SCREEN_LANGUAGE = "Язык / Language\n(A) Русский   (B) English";

// Axis names of the tilt readout.
constexpr std::string_view SQUID_TEXT_AXIS_X = "X";
constexpr std::string_view SQUID_TEXT_AXIS_Z = "Z";

// One language. The screens are format strings: {0} is the beer points, {1} the goal (SQUID_BEER_GOAL); a screen may use
// either or none. Buttons are their pad labels in parentheses: (A) (B) (X) (Y) (View).
struct squid_texts {
    // Phase labels of the debug panel.
    std::string_view phase_language;
    std::string_view phase_ready;
    std::string_view phase_play;
    std::string_view phase_pause;
    std::string_view phase_descent;
    std::string_view phase_landed;
    // Screens by phase.
    std::string_view screen_ready;
    std::string_view screen_play;
    std::string_view screen_pause;
    std::string_view screen_descent;
    std::string_view screen_landed;
    // Debug panel.
    std::string_view debug_title;
    std::string_view source_sensor;
    std::string_view source_reserve;
    std::string_view debug_target;
    std::string_view debug_actual;
    std::string_view debug_source;
    std::string_view debug_phase;
    std::string_view debug_balls;
    std::string_view debug_mass;
    std::string_view debug_round;
    std::string_view debug_split;
};

// One row per language, in squid_lang order.
constexpr auto SQUID_TEXTS = std::to_array<squid_texts>({
    {
        .phase_language = "язык",
        .phase_ready = "калибровка",
        .phase_play = "игра",
        .phase_pause = "пауза",
        .phase_descent = "спуск",
        .phase_landed = "финал",
        .screen_ready = "КАЛИБРОВКА НАКЛОНА\n"
                        "Земля наклоняется вслед за контроллером. Держи его так, как будешь играть, и жми (Y): эта поза "
                        "станет ровной.\n"
                        "Катай предметы, бей Алколдуном холодильники и пей пиво: жёлтое +1, фиолетовое +2. Набери {1}.\n"
                        "(A) старт  (Y) ровно  (X) выровнять",
        .screen_play = "Пиво {0}/{1}   (View) пауза",
        .screen_pause = "(View) играть  (Y) ровно  (B) заново",
        .screen_descent = "Пиво {0}/{1}  Алколдун летит вниз",
        .screen_landed = "Победа!  (B) заново",
        .debug_title = "Отладка",
        .source_sensor = "датчик",
        .source_reserve = "резерв",
        .debug_target = "Наклон цель: {} {:+.1f}  {} {:+.1f}",
        .debug_actual = "Наклон факт: {} {:+.1f}  {} {:+.1f}",
        .debug_source = "Источник: {}",
        .debug_phase = "Фаза игры: {}",
        .debug_balls = "Шары по размеру: 1: {}  2: {}  3: {}  4: {}",
        .debug_mass = "Масса всего: {}",
        .debug_round = "За раунд: разбиений {}, поглощений {}",
        .debug_split = "(L5) разбить шары от 2 размера: в игре и на паузе",
    },
    {
        .phase_language = "language",
        .phase_ready = "calibration",
        .phase_play = "play",
        .phase_pause = "pause",
        .phase_descent = "descent",
        .phase_landed = "finale",
        .screen_ready = "TILT CALIBRATION\n"
                        "The Earth tilts with the controller. Hold it the way you will play and press (Y): that pose "
                        "becomes level.\n"
                        "Roll the items, smash fridges with Alkoldun and drink the beer: yellow +1, violet +2. Get {1}.\n"
                        "(A) start  (Y) level  (X) reset keys",
        .screen_play = "Beer {0}/{1}   (View) pause",
        .screen_pause = "(View) resume  (Y) level  (B) restart",
        .screen_descent = "Beer {0}/{1}  Alkoldun flies down",
        .screen_landed = "You win!  (B) restart",
        .debug_title = "Debug",
        .source_sensor = "sensor",
        .source_reserve = "reserve",
        .debug_target = "Tilt target: {} {:+.1f}  {} {:+.1f}",
        .debug_actual = "Tilt actual: {} {:+.1f}  {} {:+.1f}",
        .debug_source = "Source: {}",
        .debug_phase = "Game phase: {}",
        .debug_balls = "Balls by size: 1: {}  2: {}  3: {}  4: {}",
        .debug_mass = "Total mass: {}",
        .debug_round = "This round: {} splits, {} absorbs",
        .debug_split = "(L5) split balls of size 2+: in play and pause",
    },
});
static_assert(SQUID_TEXTS.size() == SQUID_LANG_COUNT, "SQUID_TEXTS needs one row per language");

inline const squid_texts &squid_texts_of(squid_lang lang)
{
    return SQUID_TEXTS[static_cast<std::size_t>(lang)];
}

} // namespace squid
