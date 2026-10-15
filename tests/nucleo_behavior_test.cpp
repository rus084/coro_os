#include "../examples/nucleo_wb55/behavior.hpp"
#include <cassert>
#include <cstdint>

int main() {
    assert(nucleo::half_period(0, false) == 100);
    assert(nucleo::half_period(1, false) == 250);
    assert(nucleo::half_period(2, false) == 500);
    assert(nucleo::half_period(0, true) == 50);
    assert(nucleo::half_period(1, true) == 125);
    assert(nucleo::half_period(2, true) == 250);
    nucleo::debounce d;
    d.start(0, true);
    assert(d.active());
    assert(!d.sample(29, true));
    assert(d.sample(30, true));
    d.start(40, true); // Repeated falling IRQ cannot rearm a held button.
    assert(!d.sample(500, true));
    assert(!d.sample(501, false));
    assert(!d.sample(530, false));
    assert(d.active());
    assert(!d.sample(531, false));
    assert(!d.active());
    d.start(600, true);
    assert(!d.sample(610, false));
    assert(!d.sample(620, true));
    assert(!d.sample(649, true));
    assert(d.sample(650, true));
    assert(!d.sample(651, false));
    assert(!d.sample(681, false));
    assert(!d.active());
    d.start(UINT32_MAX - 10, true);
    assert(!d.sample(18, true)); // elapsed29 across wrap
    assert(d.sample(19, true));
    assert(!d.sample(UINT32_MAX - 5, false));
    assert(!d.sample(24, false)); // elapsed30 across wrap
    assert(!d.active());
    nucleo::debounce short_press;
    short_press.start(0, true);
    assert(!short_press.sample(10, false));
    assert(!short_press.sample(40, false));
    assert(!short_press.active());
    nucleo::debounce released;
    released.start(0, false);
    assert(!released.sample(30, false));
    assert(!released.active());
}
