#include "../Native/PadInputScript.hpp"
#include "../Native/LinuxVideoPresenter.hpp"
#include <cassert>
#include <iostream>

int main() {
    using maxps4::PadInputScript;
    PadInputScript s; std::string error;
    assert(s.parse("120-125:OPTIONS;300-340:RIGHT+CROSS;400:SQUARE", error));
    assert(s.buttonsAt(119) == 0 && s.buttonsAt(120) == 0x8 && s.buttonsAt(125) == 0x8 && s.buttonsAt(126) == 0);
    assert(s.buttonsAt(300) == (0x20 | 0x4000) && s.buttonsAt(400) == 0x8000 && s.buttonsAt(401) == 0);
    assert(s.parse("1-2:UP;2-3:DOWN", error) && s.buttonsAt(2) == (0x10 | 0x40)); // overlapping steps combine
    for (const char* bad : {"5-3:UP", "0:UP", "x:UP", "4-:UP", "4", "4:JUMP", "4:UP+", "4-5x:UP"}) assert(!s.parse(bad, error));
    assert(s.parse("", error) && s.steps.empty());
    assert(s.parse("1-5:RS_UP+SQUARE;3:LS_RIGHT", error));
    uint8_t lx, ly, rx, ry; PadInputScript::stickAxes(s.buttonsAt(3), lx, ly, rx, ry);
    assert(ry == 0 && rx == 0x80 && lx == 0xff && ly == 0x80 && (s.buttonsAt(3) & ~PadInputScript::stickMask) == 0x8000);
    PadInputScript::stickAxes(0, lx, ly, rx, ry); assert(lx == 0x80 && ly == 0x80 && rx == 0x80 && ry == 0x80);
    // Keyboard mapping used by the Ubuntu presenter.
    using P = maxps4::LinuxVideoPresenter;
    assert(P::buttonForKeysym(0xff52) == 0x10 && P::buttonForKeysym(0xff51) == 0x80 && P::buttonForKeysym('z') == 0x4000);
    assert(P::buttonForKeysym('x') == 0x8000 && P::buttonForKeysym(0xff0d) == 0x8 && P::buttonForKeysym('m') == 0);
    std::cout << "PASS pad input script (ranges, combinations, overlaps, malformed scripts) and keyboard mapping\n";
}
