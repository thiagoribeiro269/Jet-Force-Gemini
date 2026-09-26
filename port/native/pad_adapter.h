#pragma once
#include "original_input.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <map>
#include <sstream>

namespace jfg_native {

// Host controller adapter: an XInput pad becomes the raw N64 controller data
// (OSContPad) that the original code reads through joyRead. This is a port
// platform service, not game logic: the game's own dead zone and range stay in
// joyClamp. Pure code, so the mapping is tested off Windows too.

// XINPUT_GAMEPAD fields, mirrored.
struct HostGamepad {
    uint16_t buttons = 0;
    uint8_t leftTrigger = 0, rightTrigger = 0;
    int16_t thumbLX = 0, thumbLY = 0, thumbRX = 0, thumbRY = 0;
};
namespace Xbox {
constexpr uint16_t DpadUp = 0x0001, DpadDown = 0x0002, DpadLeft = 0x0004, DpadRight = 0x0008, Start = 0x0010, Back = 0x0020,
                   LeftThumb = 0x0040, RightThumb = 0x0080, LB = 0x0100, RB = 0x0200, A = 0x1000, B = 0x2000, X = 0x4000, Y = 0x8000;
}

// Source controls that can be remapped, and what each one produces: N64
// buttons, or a host action (restart at the entry point).
enum class HostSource : uint8_t { A, B, X, Y, LB, RB, LT, RT, Start, Back, LeftThumb, RightThumb, Count };
struct SourceTarget { uint16_t n64 = 0; bool restart = false; };
struct PadMapping {
    ControlMode mode = ControlMode::Normal;  // the original menu option
    float deadZone = 0.10f;                  // radial, fraction of the XInput stick range
    int stickRange = 80;                     // raw N64 value at full tilt (joyClamp saturates at 70)
    float cThreshold = 0.5f;                 // right stick deflection that presses a C button
    uint8_t triggerThreshold = 30;           // XINPUT_GAMEPAD_TRIGGER_THRESHOLD
    std::array<SourceTarget, size_t(HostSource::Count)> target{};
};

inline PadMapping defaultMapping() {
    PadMapping m;
    auto set = [&](HostSource s, uint16_t n64, bool restart = false) { m.target[size_t(s)] = {n64, restart}; };
    set(HostSource::A, Pad::A);
    set(HostSource::B, Pad::B);
    set(HostSource::X, Pad::B);
    set(HostSource::LB, Pad::L);
    set(HostSource::RB, Pad::R);
    set(HostSource::LT, Pad::R);
    set(HostSource::RT, Pad::Z);
    set(HostSource::Start, Pad::Start);
    set(HostSource::Back, 0, true);
    return m;
}

struct AdaptedPad { PadState pad; bool restart = false; };

inline int8_t stickValue(float unit, int range) {
    const long value = std::lround(double(unit) * double(range));
    return int8_t(std::clamp(value, -128L, 127L));
}

inline AdaptedPad adaptGamepad(const HostGamepad &g, const PadMapping &m) {
    AdaptedPad out;
    // Left stick: radial dead zone for worn hardware, then the full N64 range.
    float x = std::clamp(float(g.thumbLX) / 32767.0f, -1.0f, 1.0f), y = std::clamp(float(g.thumbLY) / 32767.0f, -1.0f, 1.0f);
    const float magnitude = std::sqrt((x * x) + (y * y));
    if (magnitude <= m.deadZone) x = y = 0.0f;
    else {
        const float scaled = std::min(1.0f, (magnitude - m.deadZone) / (1.0f - m.deadZone));
        x = x * (scaled / magnitude); y = y * (scaled / magnitude);
    }
    out.pad.stickX = stickValue(x, m.stickRange);
    out.pad.stickY = stickValue(y, m.stickRange);
    // Right stick: C buttons.
    const float rx = float(g.thumbRX) / 32767.0f, ry = float(g.thumbRY) / 32767.0f;
    if (rx >= m.cThreshold) out.pad.button |= Pad::CRight;
    if (rx <= -m.cThreshold) out.pad.button |= Pad::CLeft;
    if (ry >= m.cThreshold) out.pad.button |= Pad::CUp;
    if (ry <= -m.cThreshold) out.pad.button |= Pad::CDown;
    // D-pad keeps its place on the N64 controller.
    if (g.buttons & Xbox::DpadUp) out.pad.button |= Pad::Up;
    if (g.buttons & Xbox::DpadDown) out.pad.button |= Pad::Down;
    if (g.buttons & Xbox::DpadLeft) out.pad.button |= Pad::Left;
    if (g.buttons & Xbox::DpadRight) out.pad.button |= Pad::Right;
    auto apply = [&](HostSource s, bool held) {
        if (!held) return;
        const auto &t = m.target[size_t(s)];
        out.pad.button |= t.n64;
        out.restart = out.restart || t.restart;
    };
    apply(HostSource::A, g.buttons & Xbox::A);
    apply(HostSource::B, g.buttons & Xbox::B);
    apply(HostSource::X, g.buttons & Xbox::X);
    apply(HostSource::Y, g.buttons & Xbox::Y);
    apply(HostSource::LB, g.buttons & Xbox::LB);
    apply(HostSource::RB, g.buttons & Xbox::RB);
    apply(HostSource::Start, g.buttons & Xbox::Start);
    apply(HostSource::Back, g.buttons & Xbox::Back);
    apply(HostSource::LeftThumb, g.buttons & Xbox::LeftThumb);
    apply(HostSource::RightThumb, g.buttons & Xbox::RightThumb);
    apply(HostSource::LT, g.leftTrigger > m.triggerThreshold);
    apply(HostSource::RT, g.rightTrigger > m.triggerThreshold);
    return out;
}

// controles.ini: "chave = valor" lines, '#' or ';' comments, keys in any case.
inline PadMapping parseMapping(const std::string &text) {
    PadMapping m = defaultMapping();
    auto upper = [](std::string s) {
        for (auto &c : s) c = char(std::toupper(static_cast<unsigned char>(c)));
        return s;
    };
    auto trim = [](std::string s) {
        const auto first = s.find_first_not_of(" \t\r"), last = s.find_last_not_of(" \t\r");
        return first == std::string::npos ? std::string() : s.substr(first, last - first + 1);
    };
    static const std::map<std::string, HostSource> sources = {
        {"A", HostSource::A}, {"B", HostSource::B}, {"X", HostSource::X}, {"Y", HostSource::Y}, {"LB", HostSource::LB},
        {"RB", HostSource::RB}, {"LT", HostSource::LT}, {"RT", HostSource::RT}, {"START", HostSource::Start},
        {"BACK", HostSource::Back}, {"L3", HostSource::LeftThumb}, {"R3", HostSource::RightThumb}};
    static const std::map<std::string, uint16_t> targets = {
        {"A", Pad::A}, {"B", Pad::B}, {"Z", Pad::Z}, {"L", Pad::L}, {"R", Pad::R}, {"START", Pad::Start},
        {"C_CIMA", Pad::CUp}, {"C_BAIXO", Pad::CDown}, {"C_ESQUERDA", Pad::CLeft}, {"C_DIREITA", Pad::CRight},
        {"DIRECIONAL_CIMA", Pad::Up}, {"DIRECIONAL_BAIXO", Pad::Down}, {"DIRECIONAL_ESQUERDA", Pad::Left},
        {"DIRECIONAL_DIREITA", Pad::Right}, {"NADA", 0}};
    std::istringstream input(text);
    std::string line;
    for (unsigned number = 1; std::getline(input, line); ++number) {
        const auto comment = line.find_first_of("#;");
        if (comment != std::string::npos) line = line.substr(0, comment);
        line = trim(line);
        if (line.empty()) continue;
        const auto equals = line.find('=');
        auto fail = [&](const std::string &why) {
            throw std::runtime_error("controles.ini, linha " + std::to_string(number) + ": " + why);
        };
        if (equals == std::string::npos) fail("esperado chave = valor");
        const std::string key = upper(trim(line.substr(0, equals))), value = upper(trim(line.substr(equals + 1)));
        auto number_of = [&](float low, float high) {
            char *end = nullptr;
            const float parsed = std::strtof(value.c_str(), &end);
            if (value.empty() || *end || !std::isfinite(parsed) || parsed < low || parsed > high) fail("valor fora do intervalo");
            return parsed;
        };
        if (key == "MODO") {
            if (value == "NORMAL") m.mode = ControlMode::Normal;
            else if (value == "EXPERT") m.mode = ControlMode::Expert;
            else fail("modo deve ser normal ou expert");
        } else if (key == "ZONA_MORTA") m.deadZone = number_of(0.0f, 0.9f);
        else if (key == "ALCANCE") m.stickRange = int(number_of(10.0f, 127.0f));
        else if (key == "LIMIAR_C") m.cThreshold = number_of(0.1f, 1.0f);
        else if (key == "LIMIAR_GATILHO") m.triggerThreshold = uint8_t(number_of(0.0f, 254.0f));
        else if (sources.count(key)) {
            auto &slot = m.target[size_t(sources.at(key))];
            slot = {};
            std::istringstream parts(value);
            std::string part;
            while (std::getline(parts, part, '+')) {
                part = trim(part);
                if (part == "REINICIAR") slot.restart = true;
                else if (targets.count(part)) slot.n64 = uint16_t(slot.n64 | targets.at(part));
                else fail("destino desconhecido: " + part);
            }
        } else fail("chave desconhecida: " + key);
    }
    return m;
}
}
