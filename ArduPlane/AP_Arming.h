#pragma once

#include <AP_Arming/AP_Arming.h>

/*
  a plane specific arming class
 */
class AP_Arming_Plane : public AP_Arming
{
public:
    AP_Arming_Plane()
        : AP_Arming()
    {
        AP_Param::setup_object_defaults(this, var_info);
    }

    /* Do not allow copies */
    AP_Arming_Plane(const AP_Arming_Plane &other) = delete;
    AP_Arming_Plane &operator=(const AP_Arming_Plane&) = delete;

    void _pre_arm_checks() override;

    // var_info for holding Parameter information
    static const struct AP_Param::GroupInfo var_info[];

protected:
    void ins_checks();

};
