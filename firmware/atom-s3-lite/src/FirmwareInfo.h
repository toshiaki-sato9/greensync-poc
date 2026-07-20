#pragma once

#ifndef GREENSYNC_FIRMWARE_VERSION
#define GREENSYNC_FIRMWARE_VERSION "0.0.0-dev"
#endif

namespace FirmwareInfo {
constexpr char Version[] = GREENSYNC_FIRMWARE_VERSION;
constexpr char Hardware[] = "m5stack-atoms3-lite";
}
