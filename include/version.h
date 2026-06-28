#pragma once

// Bump the PATCH on every flash during debug sessions so the boot/About
// screen confirms the new binary actually took (avoids chasing ghosts
// when an upload silently fails to stick).
#define FIRMWARE_VERSION "0.10.1"
