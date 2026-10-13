/**
 ******************************************************************************
 * @file    vl53l9_platform.c
 * @author  IMD Software Team
 ******************************************************************************
 * @attention
 *
 * Copyright (c) 2026 STMicroelectronics.
 * All rights reserved.
 *
 * This software is licensed under terms that can be found in the LICENSE file
 * in the root directory of this software component.
 * If no LICENSE file comes with this software, it is provided AS-IS.
 *
 ******************************************************************************
 */

#include "stm32n6xx_hal.h"

#include "vl53l9.h"
#include "vl53l9_interface.h"
#include "vl53l9_platform.h"
#include "tx_api.h"

#include <string.h>

/* Preserve the first failing write's HAL details for debugger inspection. */
volatile uint32_t vl53_i3c_write_phase; /* 1 = descriptor, 2 = transmit, 3 = done */
volatile uint32_t vl53_i3c_hal_status;
volatile uint32_t vl53_i3c_hal_error;
volatile uint32_t vl53_i3c_write_size;
volatile uint32_t vl53_i3c_tx_remaining;
volatile uint32_t vl53_i3c_write_elapsed_ms;

static int _i3c_read(void *const p_dev, I3C_PrivateTypeDef *aPrivateDescriptor, I3C_XferTypeDef *aContextBuffers);
static int _i3c_read_async(void *const p_dev, I3C_PrivateTypeDef *aPrivateDescriptor, I3C_XferTypeDef *aContextBuffers);
static int _i3c_write(void *const p_dev, I3C_PrivateTypeDef *aPrivateDescriptor, I3C_XferTypeDef *aContextBuffers);

static int _i3c_wait_ready(I3C_HandleTypeDef *hi3c) {
    uint32_t start = HAL_GetTick();
    for (;;) {
        HAL_I3C_StateTypeDef state = HAL_I3C_GetState(hi3c);
        if (state == HAL_I3C_STATE_READY || state == HAL_I3C_STATE_LISTEN)
            return VL53L9_ERROR_NONE;
        if (state == HAL_I3C_STATE_ERROR)
            return VL53L9_ERROR_PLATFORM;
        if ((uint32_t)(HAL_GetTick() - start) >= 100U)
            return VL53L9_ERROR_TIMEOUT;
    }
}

int vl53l9_read(void *const p_dev, uint16_t address, uint8_t *p_values, uint32_t size) {

    int ret = VL53L9_ERROR_NONE;
    vl53l9_device_t *p_device = (vl53l9_device_t *)p_dev;

    if ((p_device == NULL) || (p_values == NULL) || (size == 0)) {
        return VL53L9_ERROR_INVALID_PARAM;
    }

    if (p_device->bus_type & PLATFORM_BUS_I3C) {
        uint8_t data_write[2];
        data_write[0] = (address >> 8) & 0xFF;
        data_write[1] = address & 0xFF;

        uint32_t cb[2];
        uint32_t sb[2];
        I3C_PrivateTypeDef pd[2] = { { p_device->address, { data_write, 2 }, { NULL, 0 }, HAL_I3C_DIRECTION_WRITE },
                                     { p_device->address, { NULL, 0 }, { p_values, size }, HAL_I3C_DIRECTION_READ } };
        I3C_XferTypeDef ctxtb[2] = { { { &cb[0], 1 }, { &sb[0], 1 }, { data_write, 2 }, { NULL, 0 } },
                                     { { &cb[1], 1 }, { &sb[1], 1 }, { NULL, 0 }, { p_values, size } } };

        ret = _i3c_read(p_device, pd, ctxtb);
        return ret;
    }

    return VL53L9_ERROR_INVALID_STATE;
}

int vl53l9_read_async(void *const p_dev, uint16_t address, volatile uint8_t *p_values, uint32_t size) {

    vl53l9_device_t *p_device = (vl53l9_device_t *)p_dev;

    if (p_device == NULL || p_values == NULL || size == 0) {
        return VL53L9_ERROR_INVALID_PARAM;
    }

    if (p_device->bus_type & PLATFORM_BUS_I3C) {
        uint8_t data_write[2];
        data_write[0] = (address >> 8) & 0xFF;
        data_write[1] = address & 0xFF;

        uint32_t cb[2];
        uint32_t sb[2];
        I3C_PrivateTypeDef pd[2] = {
            { p_device->address, { data_write, 2 }, { NULL, 0 }, HAL_I3C_DIRECTION_WRITE },
            { p_device->address, { NULL, 0 }, { (uint8_t *)p_values, size }, HAL_I3C_DIRECTION_READ }
        };
        I3C_XferTypeDef ctxtb[2] = { { { &cb[0], 1 }, { &sb[0], 1 }, { data_write, 2 }, { NULL, 0 } },
                                     { { &cb[1], 1 }, { &sb[1], 1 }, { NULL, 0 }, { (uint8_t *)p_values, size } } };

        return _i3c_read_async(p_device, pd, ctxtb);
    }

    return VL53L9_ERROR_INVALID_STATE;
}

int vl53l9_read8(void *const p_dev, uint16_t address, uint8_t *p_value) {

    vl53l9_device_t *p_device = (vl53l9_device_t *)p_dev;

    if (p_device == NULL || p_value == NULL) {
        return VL53L9_ERROR_INVALID_PARAM;
    }

    if (p_device->bus_type & PLATFORM_BUS_I3C) {
        uint8_t data_write[2];
        data_write[0] = (address >> 8) & 0xFF;
        data_write[1] = address & 0xFF;

        uint32_t cb[2];
        uint32_t sb[2];

        I3C_PrivateTypeDef pd[2] = { { p_device->address, { data_write, 2 }, { NULL, 0 }, HAL_I3C_DIRECTION_WRITE },
                                     { p_device->address, { NULL, 0 }, { p_value, 1 }, HAL_I3C_DIRECTION_READ } };
        I3C_XferTypeDef ctxtb[2] = { { { &cb[0], 1 }, { &sb[0], 1 }, { data_write, 2 }, { NULL, 0 } },
                                     { { &cb[1], 1 }, { &sb[1], 1 }, { NULL, 0 }, { p_value, 1 } } };

        return _i3c_read(p_device, pd, ctxtb);
    }

    return VL53L9_ERROR_INVALID_STATE;
}

int vl53l9_read16(void *const p_dev, uint16_t address, uint16_t *p_value) {

    vl53l9_device_t *p_device = (vl53l9_device_t *)p_dev;

    if (p_device == NULL || p_value == NULL) {
        return VL53L9_ERROR_INVALID_PARAM;
    }

    if (p_device->bus_type & PLATFORM_BUS_I3C) {
        uint8_t data_write[2];
        data_write[0] = (address >> 8) & 0xFF;
        data_write[1] = address & 0xFF;

        uint32_t cb[2];
        uint32_t sb[2];

        I3C_PrivateTypeDef pd[2] = {
            { p_device->address, { data_write, 2 }, { NULL, 0 }, HAL_I3C_DIRECTION_WRITE },
            { p_device->address, { NULL, 0 }, { (uint8_t *)p_value, 2 }, HAL_I3C_DIRECTION_READ }
        };
        I3C_XferTypeDef ctxtb[2] = { { { &cb[0], 1 }, { &sb[0], 1 }, { data_write, 2 }, { NULL, 0 } },
                                     { { &cb[1], 1 }, { &sb[1], 1 }, { NULL, 0 }, { (uint8_t *)p_value, 2 } } };

        return _i3c_read(p_device, pd, ctxtb);
    }

    return VL53L9_ERROR_INVALID_STATE;
}

int vl53l9_read32(void *const p_dev, uint16_t address, uint32_t *p_value) {

    vl53l9_device_t *p_device = (vl53l9_device_t *)p_dev;

    if (p_device == NULL || p_value == NULL) {
        return VL53L9_ERROR_INVALID_PARAM;
    }

    if (p_device->bus_type & PLATFORM_BUS_I3C) {
        uint8_t data_write[2];
        data_write[0] = (address >> 8) & 0xFF;
        data_write[1] = address & 0xFF;

        uint32_t cb[2];
        uint32_t sb[2];

        I3C_PrivateTypeDef pd[2] = {
            { p_device->address, { data_write, 2 }, { NULL, 0 }, HAL_I3C_DIRECTION_WRITE },
            { p_device->address, { NULL, 0 }, { (uint8_t *)p_value, 4 }, HAL_I3C_DIRECTION_READ }
        };
        I3C_XferTypeDef ctxtb[2] = { { { &cb[0], 1 }, { &sb[0], 1 }, { data_write, 2 }, { NULL, 0 } },
                                     { { &cb[1], 1 }, { &sb[1], 1 }, { NULL, 0 }, { (uint8_t *)p_value, 4 } } };

        return _i3c_read(p_device, pd, ctxtb);
    }

    return VL53L9_ERROR_INVALID_STATE;
}

int vl53l9_write(void *const p_dev, uint16_t address, uint8_t *p_values, uint32_t size) {

    vl53l9_device_t *p_device = (vl53l9_device_t *)p_dev;
    uint8_t data_write[2 + size];

    if (p_device == NULL || p_values == NULL || size == 0) {
        return VL53L9_ERROR_INVALID_PARAM;
    }

    if (p_device->bus_type & PLATFORM_BUS_I3C) {
        data_write[0] = (address >> 8) & 0xFF;
        data_write[1] = address & 0xFF;
        memcpy(&data_write[2], p_values, size);

        uint32_t cb[1];
        uint32_t sb[1];
        I3C_PrivateTypeDef pd[1] = {
            { p_device->address, { data_write, sizeof(data_write) }, { NULL, 0 }, HAL_I3C_DIRECTION_WRITE },
        };
        I3C_XferTypeDef ctxtb[1] = {
            { { &cb[0], 1 }, { &sb[0], 1 }, { data_write, sizeof(data_write) }, { NULL, 0 } }
        };

        return _i3c_write(p_device, pd, ctxtb);
    }
    return VL53L9_ERROR_INVALID_STATE;
}

int vl53l9_write8(void *const p_dev, uint16_t address, uint8_t value) {

    vl53l9_device_t *p_device = (vl53l9_device_t *)p_dev;

    if (p_device == NULL) {
        return VL53L9_ERROR_INVALID_PARAM;
    }

    if (p_device->bus_type & PLATFORM_BUS_I3C) {
        uint8_t data_write[3];

        data_write[0] = (address >> 8) & 0xFF;
        data_write[1] = address & 0xFF;
        data_write[2] = value & 0xFF;

        uint32_t cb[1];
        uint32_t sb[1];
        I3C_PrivateTypeDef pd[1] = {
            { p_device->address, { data_write, 3 }, { NULL, 0 }, HAL_I3C_DIRECTION_WRITE },
        };
        I3C_XferTypeDef ctxtb[1] = { { { &cb[0], 1 }, { &sb[0], 1 }, { data_write, 3 }, { NULL, 0 } } };
        return _i3c_write(p_device, pd, ctxtb);
    }

    return VL53L9_ERROR_INVALID_STATE;
}

int vl53l9_write16(void *const p_dev, uint16_t address, uint16_t value) {

    int ret = VL53L9_ERROR_NONE;
    vl53l9_device_t *p_device = (vl53l9_device_t *)p_dev;

    if (p_device == NULL) {
        return VL53L9_ERROR_INVALID_PARAM;
    }

    if (p_device->bus_type & PLATFORM_BUS_I3C) {
        uint8_t data_write[4];

        data_write[0] = (address >> 8) & 0xFF;
        data_write[1] = address & 0xFF;
        data_write[2] = (value >> 0) & 0xFF;
        data_write[3] = (value >> 8) & 0xFF;

        uint32_t cb[1];
        uint32_t sb[1];
        I3C_PrivateTypeDef pd[1] = {
            { p_device->address, { data_write, 4 }, { NULL, 0 }, HAL_I3C_DIRECTION_WRITE },
        };
        I3C_XferTypeDef ctxtb[1] = { { { &cb[0], 1 }, { &sb[0], 1 }, { data_write, 4 }, { NULL, 0 } } };
        ret = _i3c_write(p_device, pd, ctxtb);
        return ret;
    }
    return VL53L9_ERROR_INVALID_STATE;
}

int vl53l9_write32(void *const p_dev, uint16_t address, uint32_t value) {

    int ret = VL53L9_ERROR_NONE;
    vl53l9_device_t *p_device = (vl53l9_device_t *)p_dev;

    if (p_device == NULL) {
        return VL53L9_ERROR_INVALID_PARAM;
    }

    if (p_device->bus_type & PLATFORM_BUS_I3C) {
        uint8_t data_write[6];

        data_write[0] = (address >> 8) & 0xFF;
        data_write[1] = address & 0xFF;
        data_write[2] = (value >> 0) & 0xFF;
        data_write[3] = (value >> 8) & 0xFF;
        data_write[4] = (value >> 16) & 0xFF;
        data_write[5] = (value >> 24) & 0xFF;

        uint32_t cb[1];
        uint32_t sb[1];
        I3C_PrivateTypeDef pd[1] = {
            { p_device->address, { data_write, 6 }, { NULL, 0 }, HAL_I3C_DIRECTION_WRITE },
        };
        I3C_XferTypeDef ctxtb[1] = { { { &cb[0], 1 }, { &sb[0], 1 }, { data_write, 6 }, { NULL, 0 } } };
        ret = _i3c_write(p_device, pd, ctxtb);
        return ret;
    }
    return VL53L9_ERROR_INVALID_STATE;
}

static int _i3c_read(void *const p_dev, I3C_PrivateTypeDef *aPrivateDescriptor, I3C_XferTypeDef *aContextBuffers) {

    int ret = VL53L9_ERROR_NONE;
    vl53l9_device_t *p_device = (vl53l9_device_t *)p_dev;

    I3C_HandleTypeDef *p_hi3c = (I3C_HandleTypeDef *)p_device->bus;

    if (ret == VL53L9_ERROR_NONE) {
        if (HAL_I3C_AddDescToFrame(p_hi3c, NULL, &aPrivateDescriptor[0], &aContextBuffers[0],
                                   aContextBuffers[0].CtrlBuf.Size, I3C_PRIVATE_WITHOUT_ARB_RESTART) != HAL_OK) {
            ret = VL53L9_ERROR_INTERNAL;
        }
    }

    if (ret == VL53L9_ERROR_NONE) {
        if (HAL_I3C_Ctrl_Transmit(p_hi3c, &aContextBuffers[0], 100) != HAL_OK) {
            ret = VL53L9_ERROR_INTERNAL;
        }
        if (ret == VL53L9_ERROR_NONE) {
            ret = _i3c_wait_ready(p_hi3c);
            if (ret != VL53L9_ERROR_NONE)
                return ret;
        }
    }
    if (ret == VL53L9_ERROR_NONE) {
        if (HAL_I3C_AddDescToFrame(p_hi3c, NULL, &aPrivateDescriptor[1], &aContextBuffers[1],
                                   aContextBuffers[1].CtrlBuf.Size, I3C_PRIVATE_WITHOUT_ARB_STOP) != HAL_OK) {
            ret = VL53L9_ERROR_INTERNAL;
        }
    }
    if (ret == VL53L9_ERROR_NONE) {
        if ((HAL_I3C_Ctrl_Receive(p_hi3c, &aContextBuffers[1], 100)) != HAL_OK) {
            ret = VL53L9_ERROR_INTERNAL;
        }
    }
    if (ret != VL53L9_ERROR_NONE) {
        ret = VL53L9_ERROR_INVALID_OPERATION;
    }

    return ret;
}

static int _i3c_read_async(void *const p_dev, I3C_PrivateTypeDef *aPrivateDescriptor,
                           I3C_XferTypeDef *aContextBuffers) {

    int ret = VL53L9_ERROR_NONE;
    vl53l9_device_t *p_device = (vl53l9_device_t *)p_dev;

    I3C_HandleTypeDef *p_hi3c = (I3C_HandleTypeDef *)p_device->bus;

    if (ret == VL53L9_ERROR_NONE) {
        if (HAL_I3C_AddDescToFrame(p_hi3c, NULL, &aPrivateDescriptor[0], &aContextBuffers[0],
                                   aContextBuffers[0].CtrlBuf.Size, I3C_PRIVATE_WITHOUT_ARB_RESTART) != HAL_OK) {
            ret = VL53L9_ERROR_INTERNAL;
        }
    }

    if (ret == VL53L9_ERROR_NONE) {
        if (HAL_I3C_Ctrl_Transmit(p_hi3c, &aContextBuffers[0], 100) != HAL_OK) {
            ret = VL53L9_ERROR_INTERNAL;
        }
        if (ret == VL53L9_ERROR_NONE) {
            ret = _i3c_wait_ready(p_hi3c);
            if (ret != VL53L9_ERROR_NONE)
                return ret;
        }
    }
    if (ret == VL53L9_ERROR_NONE) {
        if (HAL_I3C_AddDescToFrame(p_hi3c, NULL, &aPrivateDescriptor[1], &aContextBuffers[1],
                                   aContextBuffers[1].CtrlBuf.Size, I3C_PRIVATE_WITHOUT_ARB_STOP) != HAL_OK) {
            ret = VL53L9_ERROR_INTERNAL;
        }
    }
    if (ret == VL53L9_ERROR_NONE) {
        if ((HAL_I3C_Ctrl_Receive_DMA(p_hi3c, &aContextBuffers[1])) != HAL_OK) {
            ret = VL53L9_ERROR_INTERNAL;
        }
    }
    if (ret != VL53L9_ERROR_NONE) {
        ret = VL53L9_ERROR_INVALID_OPERATION;
    }

    return ret;
}

static int _i3c_write(void *const p_dev, I3C_PrivateTypeDef *aPrivateDescriptor, I3C_XferTypeDef *aContextBuffers) {
    vl53l9_device_t *p_device = (vl53l9_device_t *)p_dev;
    I3C_HandleTypeDef *p_hi3c = (I3C_HandleTypeDef *)p_device->bus;
    uint32_t start = HAL_GetTick();
    vl53_i3c_write_size = aContextBuffers[0].TxBuf.Size;
    vl53_i3c_write_phase = 1;
    HAL_StatusTypeDef status = HAL_I3C_AddDescToFrame(p_hi3c, NULL, &aPrivateDescriptor[0],
                                 &aContextBuffers[0], aContextBuffers[0].CtrlBuf.Size,
                                 I3C_PRIVATE_WITHOUT_ARB_STOP);
    if (status == HAL_OK) {
        vl53_i3c_write_phase = 2;
        uint32_t previous_threshold = LL_I3C_GetTxFIFOThreshold(p_hi3c->Instance);
        /* This helper sends one descriptor: word FIFO mode is allowed and
         * services four bytes per write instead of one. */
        LL_I3C_SetTxFIFOThreshold(p_hi3c->Instance, LL_I3C_TXFIFO_THRESHOLD_4_4);
        TX_THREAD *thread = tx_thread_identify();
        UINT previous_preemption = 0;
        bool protected = false;
        /* Long polled writes (notably the patch) cannot tolerate a task switch
         * while the bus keeps draining the FIFO. IRQs and the HAL tick remain
         * enabled; restore scheduling before returning, including on errors. */
        if (aContextBuffers[0].TxBuf.Size > 256U && thread != TX_NULL) {
            UINT result = tx_thread_preemption_change(thread, 0, &previous_preemption);
            if (result != TX_SUCCESS) {
                LL_I3C_SetTxFIFOThreshold(p_hi3c->Instance, previous_threshold);
                return VL53L9_ERROR_PLATFORM;
            }
            protected = true;
        }
        status = HAL_I3C_Ctrl_Transmit(p_hi3c, &aContextBuffers[0], 100);
        /* Save HAL diagnostics before restoring preemption permits a switch. */
        vl53_i3c_hal_status = status;
        vl53_i3c_hal_error = p_hi3c->ErrorCode;
        vl53_i3c_tx_remaining = p_hi3c->TxXferCount;
        LL_I3C_SetTxFIFOThreshold(p_hi3c->Instance, previous_threshold);
        if (protected) {
            UINT ignored;
            (void)tx_thread_preemption_change(thread, previous_preemption, &ignored);
        }
    }
    vl53_i3c_hal_status = status;
    vl53_i3c_hal_error = p_hi3c->ErrorCode;
    vl53_i3c_tx_remaining = p_hi3c->TxXferCount;
    vl53_i3c_write_elapsed_ms = (uint32_t)(HAL_GetTick() - start);
    if (status == HAL_TIMEOUT)
        return VL53L9_ERROR_TIMEOUT;
    if (status != HAL_OK)
        return VL53L9_ERROR_PLATFORM;
    vl53_i3c_write_phase = 3;
    return VL53L9_ERROR_NONE;
}

int vl53l9_wait_ms(void *const p_dev, uint32_t delay_ms) {
    (void)p_dev;
    HAL_Delay(delay_ms);
    return 0;
}

int vl53l9_get_config_vddio(void *const p_dev, vl53l9_vddio_t *voltage) {
    int ret = VL53L9_ERROR_NONE;
    vl53l9_device_t *p_device = (vl53l9_device_t *)p_dev;
    *voltage = p_device->vddio;
    return ret;
}

int vl53l9_get_config_vdda(void *const p_dev, vl53l9_vdda_t *voltage) {
    int ret = VL53L9_ERROR_NONE;
    vl53l9_device_t *p_device = (vl53l9_device_t *)p_dev;
    *voltage = p_device->vdda;
    return ret;
}

int vl53l9_get_config_ext_clock(void *const p_dev, uint32_t *ext_clock) {
    int ret = VL53L9_ERROR_NONE;
    vl53l9_device_t *p_device = (vl53l9_device_t *)p_dev;
    *ext_clock = p_device->ext_clock;
    return ret;
}
