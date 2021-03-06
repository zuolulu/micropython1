/*
 * This file is part of the MicroPython project, http://micropython.org/
 *
 * The MIT License (MIT)
 *
 * Copyright (c) 2020-2021 Damien P. George
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include "py/runtime.h"
#include "py/mphal.h"
#include "modmachine.h"

#include "hardware/clocks.h"
#include "hardware/pwm.h"

/******************************************************************************/
// MicroPython bindings for machine.PWM

typedef struct _machine_pwm_obj_t {
    mp_obj_base_t base;
    uint8_t slice;
    uint8_t channel;
} machine_pwm_obj_t;

STATIC machine_pwm_obj_t machine_pwm_obj[] = {
    {{&machine_pwm_type}, 0, PWM_CHAN_A},
    {{&machine_pwm_type}, 0, PWM_CHAN_B},
    {{&machine_pwm_type}, 1, PWM_CHAN_A},
    {{&machine_pwm_type}, 1, PWM_CHAN_B},
    {{&machine_pwm_type}, 2, PWM_CHAN_A},
    {{&machine_pwm_type}, 2, PWM_CHAN_B},
    {{&machine_pwm_type}, 3, PWM_CHAN_A},
    {{&machine_pwm_type}, 3, PWM_CHAN_B},
    {{&machine_pwm_type}, 4, PWM_CHAN_A},
    {{&machine_pwm_type}, 4, PWM_CHAN_B},
    {{&machine_pwm_type}, 5, PWM_CHAN_A},
    {{&machine_pwm_type}, 5, PWM_CHAN_B},
    {{&machine_pwm_type}, 6, PWM_CHAN_A},
    {{&machine_pwm_type}, 6, PWM_CHAN_B},
    {{&machine_pwm_type}, 7, PWM_CHAN_A},
    {{&machine_pwm_type}, 7, PWM_CHAN_B},
};

STATIC void mp_machine_pwm_print(const mp_print_t *print, mp_obj_t self_in, mp_print_kind_t kind) {
    machine_pwm_obj_t *self = MP_OBJ_TO_PTR(self_in);
    mp_printf(print, "<PWM slice=%u channel=%u>", self->slice, self->channel);
}

// PWM(pin)
STATIC mp_obj_t mp_machine_pwm_make_new(const mp_obj_type_t *type, size_t n_args, size_t n_kw, const mp_obj_t *all_args) {
    // Check number of arguments
    mp_arg_check_num(n_args, n_kw, 1, 1, false);

    // Get GPIO to connect to PWM.
    uint32_t gpio = mp_hal_get_pin_obj(all_args[0]);

    // Get static peripheral object.
    uint slice = pwm_gpio_to_slice_num(gpio);
    uint8_t channel = pwm_gpio_to_channel(gpio);
    const machine_pwm_obj_t *self = &machine_pwm_obj[slice * 2 + channel];

    // Select PWM function for given GPIO.
    gpio_set_function(gpio, GPIO_FUNC_PWM);

    return MP_OBJ_FROM_PTR(self);
}

STATIC void mp_machine_pwm_deinit(machine_pwm_obj_t *self) {
    pwm_set_enabled(self->slice, false);
}

STATIC mp_obj_t mp_machine_pwm_freq_get(machine_pwm_obj_t *self) {
    uint32_t source_hz = clock_get_hz(clk_sys);
    uint32_t div16 = pwm_hw->slice[self->slice].div;
    uint32_t top = pwm_hw->slice[self->slice].top;
    uint32_t pwm_freq = 16 * source_hz / div16 / top;
    return MP_OBJ_NEW_SMALL_INT(pwm_freq);
}

STATIC void mp_machine_pwm_freq_set(machine_pwm_obj_t *self, mp_int_t freq) {
    // Set the frequency, making "top" as large as possible for maximum resolution.
    // Maximum "top" is set at 65534 to be able to achieve 100% duty with 65535.
    #define TOP_MAX 65534
    uint32_t source_hz = clock_get_hz(clk_sys);
    uint32_t div16_top = 16 * source_hz / freq;
    uint32_t top = 1;
    for (;;) {
        // Try a few small prime factors to get close to the desired frequency.
        if (div16_top >= 16 * 5 && div16_top % 5 == 0 && top * 5 <= TOP_MAX) {
            div16_top /= 5;
            top *= 5;
        } else if (div16_top >= 16 * 3 && div16_top % 3 == 0 && top * 3 <= TOP_MAX) {
            div16_top /= 3;
            top *= 3;
        } else if (div16_top >= 16 * 2 && top * 2 <= TOP_MAX) {
            div16_top /= 2;
            top *= 2;
        } else {
            break;
        }
    }
    if (div16_top < 16) {
        mp_raise_ValueError(MP_ERROR_TEXT("freq too large"));
    } else if (div16_top >= 256 * 16) {
        mp_raise_ValueError(MP_ERROR_TEXT("freq too small"));
    }
    pwm_hw->slice[self->slice].div = div16_top;
    pwm_hw->slice[self->slice].top = top;
}

STATIC mp_obj_t mp_machine_pwm_duty_get_u16(machine_pwm_obj_t *self) {
    uint32_t top = pwm_hw->slice[self->slice].top;
    uint32_t cc = pwm_hw->slice[self->slice].cc;
    cc = (cc >> (self->channel ? PWM_CH0_CC_B_LSB : PWM_CH0_CC_A_LSB)) & 0xffff;
    return MP_OBJ_NEW_SMALL_INT(cc * 65535 / (top + 1));
}

STATIC void mp_machine_pwm_duty_set_u16(machine_pwm_obj_t *self, mp_int_t duty_u16) {
    uint32_t top = pwm_hw->slice[self->slice].top;
    uint32_t cc = duty_u16 * (top + 1) / 65535;
    pwm_set_chan_level(self->slice, self->channel, cc);
    pwm_set_enabled(self->slice, true);
}

STATIC mp_obj_t mp_machine_pwm_duty_get_ns(machine_pwm_obj_t *self) {
    uint32_t source_hz = clock_get_hz(clk_sys);
    uint32_t slice_hz = 16 * source_hz / pwm_hw->slice[self->slice].div;
    uint32_t cc = pwm_hw->slice[self->slice].cc;
    cc = (cc >> (self->channel ? PWM_CH0_CC_B_LSB : PWM_CH0_CC_A_LSB)) & 0xffff;
    return MP_OBJ_NEW_SMALL_INT((uint64_t)cc * 1000000000ULL / slice_hz);
}

STATIC void mp_machine_pwm_duty_set_ns(machine_pwm_obj_t *self, mp_int_t duty_ns) {
    uint32_t source_hz = clock_get_hz(clk_sys);
    uint32_t slice_hz = 16 * source_hz / pwm_hw->slice[self->slice].div;
    uint32_t cc = (uint64_t)duty_ns * slice_hz / 1000000000ULL;
    if (cc > 65535) {
        mp_raise_ValueError(MP_ERROR_TEXT("duty larger than period"));
    }
    pwm_set_chan_level(self->slice, self->channel, cc);
    pwm_set_enabled(self->slice, true);
}
