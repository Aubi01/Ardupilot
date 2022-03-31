/*
 * This file is free software: you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This file is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * Author: Dmitry Ponomarev
 */
#pragma once

#include <AP_HAL/AP_HAL.h>
#include <SRV_Channel/SRV_Channel.h>

#include "reg/udral/service/actuator/common/sp/Scalar_0_1.h"
#include "reg/udral/service/actuator/common/sp/Vector4_0_1.h"
#include "reg/udral/service/common/Readiness_0_1.h"
#include "reg/udral/physics/electricity/PowerTs_0_1.h"
#include "reg/udral/service/actuator/common/Feedback_0_1.h"
#include "reg/udral/service/actuator/common/Status_0_1.h"
#include "reg/udral/physics/dynamics/rotation/PlanarTs_0_1.h"

#include "AP_UAVCAN_V1_publisher.h"
#include "AP_UAVCAN_V1_subscriber.h"
#include "AP_UAVCAN_V1_registers.h"


#ifndef UAVCAN_V1_SRV_NUMBER
#define UAVCAN_V1_SRV_NUMBER            18
#endif


class UavcanElectricityPowerTsSubscriber;
class UavcanSetpointPublisher;
class UavcanReadinessPublisher;
class UavcanDynamicsSubscriber;
class UavcanStatusSubscriber;


struct SrvConfig {
    uint16_t pulse;
    bool esc_pending;
    bool servo_pending;
};


/**
 * Publishes the following UAVCAN v1 messages:
 *   reg.udral.service.actuator.common.sp.Vector4.0.1
 *   reg.udral.service.actuator.common.Readiness.0.1
 *
 * Subscribes to the following UAVCAN v1 messages:
 *   reg.udral.physics.electricity.PowerTs_0_1
 *   reg.udral.service.actuator.common.Feedback_0_1
 *   reg.udral.service.actuator.common.Status_0_1
 *   reg.udral.physics.dynamics.rotation.PlanarTs_0_1
 */
class UavcanEscController
{
public:
    UavcanEscController(UavcanRegisters& uavcan_registers): _registers(uavcan_registers) {}

    bool init(UavcanSubscriberManager &sub_manager,
              UavcanPublisherManager &pub_manager,
              CanardInstance &ins,
              CanardTxQueue& tx_queue);
    void SRV_push_servos(void);

    float get_voltage(uint8_t esc_idx);
    float get_current(uint8_t esc_idx);
    uint16_t get_rpm(uint8_t esc_idx);
    float get_temperature(uint8_t esc_idx);
    uint16_t get_avaliable_data_mask(uint8_t esc_idx);

private:
    SrvConfig _SRV_conf[UAVCAN_V1_SRV_NUMBER];

    uint8_t _esc_state;
    uint32_t _SRV_last_send_us;
    HAL_Semaphore SRV_sem;

    UavcanElectricityPowerTsSubscriber *_sub_power[4];
    UavcanDynamicsSubscriber *_sub_dynamics[4];
    UavcanStatusSubscriber *_sub_status[4];
    UavcanSetpointPublisher *_pub_setpoint;
    UavcanReadinessPublisher *_pub_readiness;

    bool _is_inited{false};

    uint32_t last_log_ts_ms = {5000};

    UavcanRegisters& _registers;
};


/**
 * @note reg.udral.physics.dynamics.rotation.PlanarTs_0_1
 */
class UavcanDynamicsSubscriber: public UavcanBaseSubscriber
{
public:
    UavcanDynamicsSubscriber(CanardInstance &ins, CanardTxQueue& tx_queue, int16_t port_id) :
        UavcanBaseSubscriber(ins, tx_queue, port_id) { };

    virtual void subscribe() override;
    virtual void handler(const CanardRxTransfer* transfer) override;

    uint32_t get_last_recv_timestamp();
    float get_angular_position();
    float get_angular_velocity();
    float get_angular_acceleration();
    float get_torque();

private:
    reg_udral_physics_dynamics_rotation_PlanarTs_0_1 msg;
    uint32_t _last_recv_ts_ms{0};
};


/**
 * @note reg.udral.physics.electricity.PowerTs_0_1
 */
class UavcanElectricityPowerTsSubscriber: public UavcanBaseSubscriber
{
public:
    UavcanElectricityPowerTsSubscriber(CanardInstance &ins, CanardTxQueue& tx_queue, int16_t port_id) :
        UavcanBaseSubscriber(ins, tx_queue, port_id) { };

    virtual void subscribe() override;
    virtual void handler(const CanardRxTransfer* transfer) override;

    uint32_t get_last_recv_timestamp();
    float get_voltage();
    float get_current();

private:
    reg_udral_physics_electricity_PowerTs_0_1 msg;
    uint32_t _last_recv_ts_ms{0};
};


/**
 * @note reg.udral.service.actuator.common.Status_0_1
 */
class UavcanStatusSubscriber: public UavcanBaseSubscriber
{
public:
    UavcanStatusSubscriber(CanardInstance &ins, CanardTxQueue& tx_queue, int16_t port_id) :
        UavcanBaseSubscriber(ins, tx_queue, port_id) { };

    virtual void subscribe() override;
    virtual void handler(const CanardRxTransfer* transfer) override;

    uint32_t get_last_recv_timestamp();
    float get_motor_temperature();

private:
    reg_udral_service_actuator_common_Status_0_1 _msg;
    uint32_t _last_recv_ts_ms{0};
};


/**
 * @note reg.udral.service.actuator.common.sp.*
 */
class UavcanSetpointPublisher : public UavcanBasePublisher
{
public:
    UavcanSetpointPublisher(CanardInstance &ins, CanardTxQueue& tx_queue, CanardPortID port_id);
    virtual void update() override;
    void set_setpoint(SrvConfig *src_config);

private:
    static constexpr uint32_t publish_period_ms = 5;
    uint32_t next_publish_time_ms{publish_period_ms};

    reg_udral_service_actuator_common_sp_Vector4_0_1 _vector4_sp;

    void publish();
};


/**
 * @note reg.udral.service.common.Readiness.0.1
 */
class UavcanReadinessPublisher : public UavcanBasePublisher
{
public:
    UavcanReadinessPublisher(CanardInstance &ins, CanardTxQueue& tx_queue, CanardPortID port_id);
    virtual void update() override;
    void update_readiness();

private:
    static constexpr uint32_t publish_period_ms = 50;
    uint32_t next_publish_time_ms{publish_period_ms};

    reg_udral_service_common_Readiness_0_1 _readiness;

    void publish();
};
