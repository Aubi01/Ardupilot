#pragma once

#include <AP_HAL/AP_HAL.h>
#include "AP_HAL_SITL_Namespace.h"
#include "Semaphores.h"

class HALSITL::Util : public AP_HAL::Util {
public:
    explicit Util(SITL_State *sitlState) :
        _sitlState(sitlState) {}

    bool run_debug_shell(AP_HAL::BetterStream *stream) {
        return false;
    }

    /**
       how much free memory do we have in bytes. 
     */
    uint32_t available_memory(void) override {
        // SITL is assumed to always have plenty of memory. Return 128k for now
        return 0x20000;
    }

    // create a new semaphore
    AP_HAL::Semaphore *new_semaphore(void) override { return new HALSITL::Semaphore; }

    // get path to custom defaults file for AP_Param
    const char* get_custom_defaults_file() const override {
        return _sitlState->defaults_path;
    }

private:
    SITL_State *_sitlState;
};
