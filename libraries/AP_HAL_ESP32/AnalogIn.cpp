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
 * Code by Charles Villard
 */
#include <AP_HAL/AP_HAL.h>
#include <AP_HAL_ESP32/Semaphores.h>

#include <stdlib.h>
#include <stdio.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "driver/adc.h"
#include "esp_adc_cal.h"
#include "soc/adc_channel.h"

#if HAL_USE_ADC
//== TRUE && !defined(HAL_DISABLE_ADC_DRIVER)

#include "AnalogIn.h"

#define ESP32_ADC_MAVLINK_DEBUG 1

#ifndef ESP32_ADC_MAVLINK_DEBUG
// this allows the first 6 analog channels to be reported by mavlink for debugging purposes
#define ESP32_ADC_MAVLINK_DEBUG 0
#endif

#include <GCS_MAVLink/GCS_MAVLink.h>

#define ANALOGIN_DEBUGGING 1


// base voltage scaling for 12 bit 3.3V ADC
#define VOLTAGE_SCALING (3.3f/4096.0f)

#if ANALOGIN_DEBUGGING
# define Debug(fmt, args ...)  do {printf("%s:%d: " fmt "\n", __FUNCTION__, __LINE__, ## args); } while(0)
#else
# define Debug(fmt, args ...)
#endif

// we are limited to using adc1, and it supports 8 channels max, on gpio, in this order:
// ADC1_CH0=D36,ADC1_CH1=D37,ADC1_CH2=D38,ADC1_CH3=D39,ADC1_CH4=D32,ADC1_CH5=D33,ADC1_CH6=D34,ADC1_CH7=D35
// this driver will only configure the ADCs from a subset of these that the board exposes on pins.


extern const AP_HAL::HAL &hal;

using namespace ESP32;

/*
   scaling table between ADC count and actual input voltage, to account
   for voltage dividers on the board.
   */
const AnalogIn::pin_info AnalogIn::pin_config[] = HAL_ESP32_ADC_PINS;

#define ADC_GRP1_NUM_CHANNELS   ARRAY_SIZE(AnalogIn::pin_config)


#define DEFAULT_VREF    1100       //Use adc2_vref_to_gpio() to obtain a better estimate
#define NO_OF_SAMPLES   256        //Multisampling

static const adc_atten_t atten = ADC_ATTEN_DB_12;

int adc_gpio_pin_lookup(int adc_unit, int adc_channel)
{
    struct t {
        int ch;
        int pin;
    } m[] = {
        {ADC1_CHANNEL_0, ADC1_CHANNEL_0_GPIO_NUM},
        {ADC1_CHANNEL_1, ADC1_CHANNEL_1_GPIO_NUM},
        {ADC1_CHANNEL_2, ADC1_CHANNEL_2_GPIO_NUM},
        {ADC1_CHANNEL_3, ADC1_CHANNEL_3_GPIO_NUM},
        {ADC1_CHANNEL_4, ADC1_CHANNEL_4_GPIO_NUM},
        {ADC1_CHANNEL_5, ADC1_CHANNEL_5_GPIO_NUM},
        {ADC1_CHANNEL_6, ADC1_CHANNEL_6_GPIO_NUM},
        {ADC1_CHANNEL_7, ADC1_CHANNEL_7_GPIO_NUM},
        {-1,-1}
    };

    if (adc_unit != 1) {
        printf("AnalogIn: Only ADC1 is usable on ESP32 device\n");
        return -1;
    }
    for (t *p = m; p->ch != -1; p++)
    {
        if (p->ch == adc_channel) {
            return p->pin;
        }
    }
    printf("AnalogIn: ADC1 pin lookup failed for channel %d\n", adc_channel);
    return -1;
}


/*---------------------------------------------------------------
        ADC Calibration
---------------------------------------------------------------*/
bool adc_calibration_init(adc_unit_t unit, adc_channel_t channel, adc_atten_t atten, adc_cali_handle_t *out_handle)
{
    adc_cali_handle_t handle = NULL;
    esp_err_t ret = ESP_FAIL;
    bool calibrated = false;

#if ADC_CALI_SCHEME_CURVE_FITTING_SUPPORTED
    if (!calibrated) {
        printf("AnalogIn: calibration scheme version is %s", "Curve Fitting");
        adc_cali_curve_fitting_config_t cali_config = {
            .unit_id = unit,
            .chan = channel,
            .atten = atten,
            .bitwidth = ADC_BITWIDTH_DEFAULT,
        };
        ret = adc_cali_create_scheme_curve_fitting(&cali_config, &handle);
        if (ret == ESP_OK) {
            calibrated = true;
        }
    }
#endif

#if ADC_CALI_SCHEME_LINE_FITTING_SUPPORTED
    if (!calibrated) {
        printf("AnalogIn: calibration scheme version is %s", "Line Fitting");
        adc_cali_line_fitting_config_t cali_config = {
            .unit_id = unit,
            .atten = atten,
            .bitwidth = ADC_BITWIDTH_DEFAULT,
        };
        ret = adc_cali_create_scheme_line_fitting(&cali_config, &handle);
        if (ret == ESP_OK) {
            calibrated = true;
        }
    }
#endif

    *out_handle = handle;
    if (ret == ESP_OK) {
        printf("AnalogIn: Calibration Success");
    } else if (ret == ESP_ERR_NOT_SUPPORTED || !calibrated) {
        printf("AnalogIn: eFuse not burnt, skip software calibration");
    } else {
        printf("AnalogIn: Invalid arg or no memory");
    }

    return calibrated;
}

void adc_calibration_deinit(adc_cali_handle_t handle)
{
#if ADC_CALI_SCHEME_CURVE_FITTING_SUPPORTED
    printf("AnalogIn: deregister %s calibration scheme", "Curve Fitting");
    adc_cali_delete_scheme_curve_fitting(handle);
#elif ADC_CALI_SCHEME_LINE_FITTING_SUPPORTED
    printf("AnalogIn: deregister %s calibration scheme", "Line Fitting");
    adc_cali_delete_scheme_line_fitting(handle);
#endif
}

//ardupin is the ardupilot assigned number, starting from 1-8(max)
// 'pin' and _pin is a macro like 'ADC1_GPIO35_CHANNEL' from board config .h
AnalogSource::AnalogSource(adc_oneshot_unit_handle_t adc1_handle, int16_t ardupin, int16_t pin, float scaler, float initial_value, uint8_t unit) :

    _unit(unit),
    _ardupin(c),
    _pin(pin),
    _scaler(scaler),
    _value(initial_value),
    _latest_value(initial_value),
    _sum_count(0),
    _sum_value(0),
    _adc1_cali_ch_handle(0)
{
    _adc1_handle = adc1_handle;

    set_pin(ardupin);
}


float AnalogSource::read_average()
{
    if ( _ardupin == ANALOG_INPUT_NONE ) {
        return 0.0f;
    }

    WITH_SEMAPHORE(_semaphore);

    if (_sum_count == 0) {
        uint32_t adc_reading = 0;
        //Multisampling
        for (int i = 0; i < NO_OF_SAMPLES; i++) {
            adc_reading += adc_read();
        }
        adc_reading /= NO_OF_SAMPLES;
        return adc_reading;
    }

    _value = _sum_value / _sum_count;
    _sum_value = 0;
    _sum_count = 0;

    return _value;
}

float AnalogSource::read_latest()
{
    return _latest_value;
}

//_scaler scaling from ADC count to Volts

/*
   return voltage in Volts
   */
float AnalogSource::voltage_average()
{
    return _scaler * read_average();
}

/*
   return voltage in Volts
   */
float AnalogSource::voltage_latest()
{
    return _scaler * read_latest();
}

float AnalogSource::voltage_average_ratiometric()
{
    return _scaler * read_latest();
}

// ardupin
bool AnalogSource::set_pin(uint8_t ardupin)
{

    if (_ardupin == ardupin) {
        return true;
    }

    int8_t pinconfig_offset = AnalogIn::find_pinconfig(ardupin);

    if (pinconfig_offset == -1 ) {
        DEV_PRINTF("AnalogIn: sorry set_pin() can't determine ADC1 offset from ardupin : %d \n",ardupin);
        return false;
    }
    pin_info *p = AnalogIn::pin_config + pinconfig_offset;

    int16_t newgpioAdcPin = p->channel;
    float newscaler       = p->scaling;

    if (_pin == newgpioAdcPin) {
        return true;
    }

    WITH_SEMAPHORE(_semaphore);

    // init the target pin now if possible
    if ( ardupin == ANALOG_INPUT_NONE ) {
        if (_adc1_cali_ch_handle) {
            adc_calibration_deinit(_adc1_cali_ch_handle);
            _adc1_cali_ch_handle = 0;
        }
    }
    else {

    // determine actual gpio from adc offset and configure it
        gpio_num_t gpio = adc_gpio_pin_lookup(_unit, newgpioAdcPin);

        printf("AnalogIn: determined actual gpio as: %d\n", gpio);

        adc_oneshot_chan_cfg_t config = {
            .bitwidth = ADC_BITWIDTH_DEFAULT,
            .atten = atten,
        };
        if (ESP_OK != adc_oneshot_config_channel(_adc1_handle, gpio, &config)) {
            printf("AnalogIn: adc_oneshot_config_channel failed\n");
            return;
        }

        if (ESP_OK !=  adc_calibration_init(_unit, gpio, atten, &_adc1_cali_ch_handle)){
            printf("AnalogIn: adc_calibration_init failed\n");
            return;
        }

        DEV_PRINTF("AnalogIn: set_pin() FROM (ardupin:%d adc1_offset:%d gpio:%d) TO (ardupin:%d adc1_offset:%d gpio:%d)\n", 
            _ardupin, _pin, _gpio, ardupin, newgpioAdcPin, gpio);

        _pin = newgpioAdcPin;
        _ardupin = ardupin;
        _gpio = gpio;
        _scaler = newscaler;

    }

    _sum_value = 0;
    _sum_count = 0;
    _latest_value = 0;
    _value = 0;

    return true;
}

int AnalogSource::adc_read()
{
    int raw, value = 0;

    if (ESP_OK != adc_oneshot_read(_adc1_handle, _pin, &raw)) {
        printf("AnalogIn: adc_oneshot_read failed\n");
        return 0;
    }

    if (_adc1_cali_handle) {
        if(ESP_OK != adc_cali_raw_to_voltage(_adc1_cali_handle, raw, &value)) {
            printf("AnalogIn: adc_oneshot_read failed\n");
            return 0;
        }
    }
    else {
        value = raw;
    }
    return value;
}

/*
   apply a reading in ADC counts
   */
void AnalogSource::_add_value()
{
    if ( _ardupin == ANALOG_INPUT_NONE ) {
        return;
    }

    WITH_SEMAPHORE(_semaphore);

    int value = adc_read();

    _latest_value = value;
    _sum_value += value;
    _sum_count++;

    if (_sum_count == 254) {
        _sum_value /= 2;
        _sum_count /= 2;
    }
}

/*
   setup adc peripheral to capture samples with DMA into a buffer
   */
void AnalogIn::init()
{
   adc_oneshot_unit_init_cfg_t init_config = { .unit_id = ADC_UNIT_1 };
    if (ESP_OK != adc_oneshot_new_unit(&init_config, &_adc1_handle)) {
        printf("AnalogIn: adc_oneshot_new_unit failed\n");
        return;
    }
}

/*
   called at 1kHz
*/
void AnalogIn::_timer_tick()
{
    double buf_adc[ANALOG_MAX_CHANNELS] {};

    for (uint8_t j = 0; j < ANALOG_MAX_CHANNELS; j++) {
        ESP32::AnalogSource *c = _channels[j];
        if (c != nullptr) {
            // add a value
            c->_add_value();
            buf_adc[j] = c->voltage_latest();
        }
    }

#if ESP32_ADC_MAVLINK_DEBUG
    static int count;
    if (AP_HAL::millis() > 5000 && count++ == 1000) {
        count = 0;
        printf("ADC: %f, %f, %f, %f, %f, %f\n", buf_adc[0], buf_adc[1], buf_adc[2], buf_adc[3], buf_adc[4], buf_adc[5]);
    }
#endif

}

//positive array index (zero is ok), or -1 on error
int8_t AnalogIn::find_pinconfig(int16_t ardupin)
{
    // from ardupin, lookup which adc gpio that is..
    for (uint8_t j = 0; j < ADC_GRP1_NUM_CHANNELS; j++) {
        if (pin_config[j].ardupin == ardupin) {
            return j;
        }
    }
    // can't find a match in definitons
    return -1;

}

//
AP_HAL::AnalogSource *AnalogIn::channel(int16_t ardupin)
{
    int8_t pinconfig_offset = find_pinconfig(ardupin);

    int16_t gpioAdcPin = -1;
    float scaler = -1;

    if ((ardupin != ANALOG_INPUT_NONE) && (pinconfig_offset == -1 )) {
        DEV_PRINTF("AnalogIn: sorry channel() can't determine ADC1 offset from ardupin : %d \n",ardupin);
        ardupin = ANALOG_INPUT_NONE; // default it to this not terrible value and allow to continue
    }
    // although ANALOG_INPUT_NONE=255 is not a valid pin, we let it through here as
    //  a special case, so that it can be changed with set_pin(..) later.
    if (ardupin != ANALOG_INPUT_NONE) {
        gpioAdcPin = pin_config[(uint8_t)pinconfig_offset].channel;
        scaler = pin_config[(uint8_t)pinconfig_offset].scaling;
    }

    for (uint8_t j = 0; j < ANALOG_MAX_CHANNELS; j++) {
        if (_channels[j] == nullptr) {
            _channels[j] = NEW_NOTHROW AnalogSource(ardupin,gpioAdcPin, scaler,0.0f,1);

            if (ardupin != ANALOG_INPUT_NONE) {
                DEV_PRINTF("AnalogIn: channel:%d attached to ardupin:%d at adc1_offset:%d on gpio:%d\n",\
                                    j,ardupin, gpioAdcPin, _channels[j]->_gpio);
            }

            if (ardupin == ANALOG_INPUT_NONE) {
                DEV_PRINTF("AnalogIn: channel:%d created but using delayed adc and gpio pin configuration\n",j );
            }

            return _channels[j];
        }
    }
    DEV_PRINTF("AnalogIn: out of channels\n");
    return nullptr;
}

#endif // HAL_USE_ADC
