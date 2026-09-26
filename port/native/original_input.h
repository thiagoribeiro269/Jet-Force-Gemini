#pragma once
#include <array>
#include <cstdint>
#include <stdexcept>
#include <string>

namespace jfg_native {

// A behaviour the original game performs here but the port does not yet
// execute. The session stops instead of continuing on a different path. The
// explanation is shown to the player; what() stays technical for logs/tests.
class NotPortedError : public std::runtime_error {
    std::string explanation_;
public:
    NotPortedError(const std::string &technical, std::string explanation)
        : std::runtime_error(technical), explanation_(std::move(explanation)) {}
    const std::string &explanation() const { return explanation_; }
};

// N64 controller bits (os_cont.h). 0x0080 and 0x0040 are not buttons of the
// standard controller and never reach the game from a host adapter.
namespace Pad {
constexpr uint16_t A = 0x8000, B = 0x4000, Z = 0x2000, Start = 0x1000, Up = 0x0800, Down = 0x0400, Left = 0x0200,
                   Right = 0x0100, L = 0x0020, R = 0x0010, CUp = 0x0008, CDown = 0x0004, CLeft = 0x0002, CRight = 0x0001;
constexpr uint16_t Buttons = 0xFF3F;
}

// OSContPad as osContGetReadData delivers it: buttons and raw stick values.
struct PadState {
    uint16_t button = 0;
    int8_t stickX = 0, stickY = 0;
    bool operator==(const PadState &other) const {
        return button == other.button && stickX == other.stickX && stickY == other.stickY;
    }
    bool operator!=(const PadState &other) const { return !(*this == other); }
};
inline void validatePad(const PadState &pad) {
    if (pad.button & ~Pad::Buttons) throw std::runtime_error("Controller bits outside the standard N64 buttons");
}

// controller.c joyRead for one enabled controller: previous/current data and
// the pressed/released XOR masks. joySecurity stays 0xFFFF on a legitimate
// ROM, so no bit is masked. One call per original frame.
struct JoypadFrame { PadState pad; uint16_t pressed = 0, released = 0; };
class JoypadReader {
    PadState current_, previous_;
public:
    JoypadFrame read(const PadState &pad) {
        validatePad(pad);
        previous_ = current_;
        current_ = pad;
        constexpr uint16_t joySecurity = 0xFFFF;
        const auto changed = uint16_t(current_.button ^ previous_.button);
        return {current_, uint16_t(changed & current_.button & joySecurity), uint16_t(changed & previous_.button & joySecurity)};
    }
    const PadState &current() const { return current_; }
};

// controller.c joyClamp: dead zone 5, offset by 5, range 65.
inline int32_t joyClamp(int32_t raw) {
    auto magnitude = int8_t(raw);
    if (magnitude < 5 && magnitude > -5) return 0;
    if (magnitude > 0) { magnitude = int8_t(magnitude - 5); if (magnitude > 65) magnitude = 65; }
    else { magnitude = int8_t(magnitude + 5); if (magnitude < -65) magnitude = -65; }
    return magnitude;
}

// charControl.c controlReadJoypad: controlXjoy/Yjoy through joyClamp, the
// absolute values, held/pressed/released keys; everything zero while
// disablejoy is set.
struct ControlInput {
    int32_t xjoy = 0, yjoy = 0, absX = 0, absY = 0;
    uint16_t keys = 0, dkeys = 0, released = 0;
};
inline ControlInput controlReadJoypad(const JoypadFrame &frame, bool disabled) {
    if (disabled) return {};
    return {joyClamp(frame.pad.stickX), joyClamp(frame.pad.stickY), frame.pad.stickX, frame.pad.stickY,
            frame.pad.button, frame.pressed, frame.released};
}

// ControlModeNormal/ControlModeExpert (charControl .data 0x800A18B4 and
// 0x800A18D8), chosen each frame by controlPlayer from frontGetTargetControl.
// Words by offset: +0x0 fire, +0x4/+0x8 next/previous weapon, +0xC jump,
// +0x10 crouch, +0x14/+0x18 strafe left/right; +0x1C/+0x20 are read only by
// the aim states 5 and 0xB in Expert mode.
enum class ControlMode : uint8_t { Normal = 0, Expert = 1 };
struct ControlModeKeys {
    std::array<uint32_t, 9> word{};
    uint32_t fire() const { return word[0]; }
    uint32_t nextWeapon() const { return word[1]; }
    uint32_t previousWeapon() const { return word[2]; }
    uint32_t jump() const { return word[3]; }
    uint32_t crouch() const { return word[4]; }
    uint32_t strafeLeft() const { return word[5]; }
    uint32_t strafeRight() const { return word[6]; }
};
}
