#pragma once

// Every string the disc shows (code-rules, 11). Text is UTF-8. Draw it with a font built with rv_font_block_cyrillic:
// that atlas has the Cyrillic block. The ASCII punctuation of rv_font_data.hpp is in every atlas, so no dashes, middle
// dots or typographic quotes here.

#include <string_view>

namespace squid
{

// Phase labels, in squid_phase order.
constexpr std::string_view SQUID_TEXT_PHASE_PREPARE = "подготовка";
constexpr std::string_view SQUID_TEXT_PHASE_READY = "готовность";
constexpr std::string_view SQUID_TEXT_PHASE_PLAY = "игра";
constexpr std::string_view SQUID_TEXT_PHASE_PAUSE = "пауза";

// Hints. Buttons are the pad's names; the keys in brackets are the console window's (rv_pcwindow_sdl3.cpp).
constexpr std::string_view SQUID_TEXT_INTRO = "Наклоняй контроллер, чтобы менять орбиту. Коснись отмеченного шарика.";
constexpr std::string_view SQUID_TEXT_HINT_PREPARE =
    "SOUTH (Space/Z): калибровка  EAST (X): свободный режим  WEST (C): выровнять";
constexpr std::string_view SQUID_TEXT_HINT_READY =
    "SOUTH (Space/Z): старт  NORTH (V): перекалибровка  VIEW (Tab): рестарт  EAST (X): свободный режим  WEST (C): выровнять";
constexpr std::string_view SQUID_TEXT_HINT_PLAY = "Правый бампер (E): пауза";
constexpr std::string_view SQUID_TEXT_HINT_PAUSE =
    "Правый бампер (E): продолжить  NORTH (V): перекалибровка  VIEW (Tab): рестарт";

// Debug panel. Axis names and source names are the labels the tilt readout uses.
constexpr std::string_view SQUID_TEXT_DEBUG_TITLE = "Отладка";
constexpr std::string_view SQUID_TEXT_AXIS_X = "X";
constexpr std::string_view SQUID_TEXT_AXIS_Z = "Z";
constexpr std::string_view SQUID_TEXT_SOURCE_SENSOR = "датчик";
constexpr std::string_view SQUID_TEXT_SOURCE_RESERVE = "резерв";
constexpr std::string_view SQUID_TEXT_DEBUG_TARGET = "Наклон цель: {} {:+.1f}  {} {:+.1f}";
constexpr std::string_view SQUID_TEXT_DEBUG_ACTUAL = "Наклон факт: {} {:+.1f}  {} {:+.1f}";
constexpr std::string_view SQUID_TEXT_DEBUG_SOURCE = "Источник: {}";
constexpr std::string_view SQUID_TEXT_DEBUG_PHASE = "Фаза игры: {}";

} // namespace squid
