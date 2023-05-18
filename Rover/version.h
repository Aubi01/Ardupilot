#pragma once

#ifndef FORCE_VERSION_H_INCLUDE
#error version.h should never be included directly. You probably want to include AP_Common/AP_FWVersion.h
#endif

#include "ap_version.h"

<<<<<<< Updated upstream
#define THISFIRMWARE "ArduRover V4.4.0-dev"

// the following line is parsed by the autotest scripts
#define FIRMWARE_VERSION 4,4,0,FIRMWARE_VERSION_TYPE_DEV
=======
#define THISFIRMWARE "ArduRover V4.4.0-beta1"

// the following line is parsed by the autotest scripts
#define FIRMWARE_VERSION 4,4,0,FIRMWARE_VERSION_TYPE_BETA
>>>>>>> Stashed changes

#define FW_MAJOR 4
#define FW_MINOR 4
#define FW_PATCH 0
#define FW_TYPE FIRMWARE_VERSION_TYPE_BETA

#include <AP_Common/AP_FWVersionDefine.h>
