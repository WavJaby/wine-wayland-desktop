# Scroll input timing probe

Build with `x86_64-w64-mingw32-gcc -Wall -Wextra -O2 -municode -mwindows scroll-input.c -o scroll-input.exe`.
Run the executable on native Windows or Wine. Focus the window, move the pointer
inside it, and perform a two-finger flick. Press Space as close to finger release
as possible. Close the window to finish. A TSV appears beside the executable.

The log contains high resolution receipt times, message timestamps, wheel deltas,
and input device/origin information when GetCurrentInputMessageSource succeeds.
Space is a manual timing marker, not a hardware finger-release event. Compare
short flicks, slow scrolling, interrupted inertia, and a physical mouse wheel.
The probe does not opt into direct touchpad input; it measures the default legacy
window route. It cannot establish which APIs LINE actually uses. RDP may change
input delivery, so label RDP results separately from native touchpad measurements.

The executable has been compiled, but physical input has not yet been measured.
