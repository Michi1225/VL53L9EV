#include "VL53L9_process.h"
#include "stm32n6xx_hal_def.h"
#include "vl53l9.h"
#include "vl53l9_interface.h"
#include "dcmipp.h"
#include <stdbool.h>
#include <string.h>

#define INIT_CALL(call) do { if ((call) != 0) return HAL_ERROR; } while (0)

/** @brief Clean and invalidate the CSI DMA buffer before the next capture. */
void VL53L9_prepare_capture(VL53L9_TypeDef_t *tof)
{
    if (SCB->CCR & SCB_CCR_DC_Msk)
        SCB_CleanInvalidateDCache_by_Addr((uint32_t *)tof->g_csi_output_buffer,
                                         sizeof(tof->g_csi_output_buffer));
}

/** @brief Reset and address the ToF sensor, load its firmware, and apply the default profile. */
HAL_StatusTypeDef VL53L9_init(VL53L9_TypeDef_t *tof)
{
    tof->p_dev = &device[CONF_DEVICE_ID];
    tof->profile = g_ranging_profiles[CONF_USECASE];
    tof->p_profile = &tof->profile;
    memset(&(tof->frame), 0, sizeof(tof->frame));
    INIT_CALL(platform_power_reset(CONF_DEVICE_ID));
    INIT_CALL(platform_assign_dynamic_address());
    INIT_CALL(vl53l9_init(tof->p_dev));
    return VL53L9_configure(tof, &tof->profile);
}

/** @brief Validate the requested geometry and configure the ToF sensor and CSI receiver. */
HAL_StatusTypeDef VL53L9_configure(VL53L9_TypeDef_t *tof, const vl53l9_profile_t *profile)
{
    vl53l9_profile_t next = *profile;
    uint8_t width, height;
    INIT_CALL(vl53l9_utils_get_csi_resolution(next.binning, &width, &height));
    if (width > CSI_OUTPUT_PITCH || (uint32_t)width * height > CSI_OUTPUT_PAYLOAD_SIZE)
        return HAL_ERROR;
    INIT_CALL(vl53l9_utils_set_profile(tof->p_dev, &next));
    INIT_CALL(vl53l9_get_hw_config(tof->p_dev, &tof->hw_config));
    tof->hw_config.output_interface = VL53L9_OUTPUT_CSI2;
    tof->hw_config.signaling_mode = true;
    tof->hw_config.csi_data_rate = 1000000000U;
    tof->hw_config.csi_virtual_channel = 0;
    tof->hw_config.csi_status_line_force_width = false;
    tof->hw_config.csi_status_line_datatype = 0x2A;
    tof->hw_config.csi_frame_datatype = 0x2A;
    tof->hw_config.csi_frame_height = height - 1U;
    tof->hw_config.csi_frame_width = width;
    INIT_CALL(vl53l9_set_hw_config(tof->p_dev, tof->hw_config));
    INIT_CALL(vl53l9_get_hw_config(tof->p_dev, &tof->hw_config));
    INIT_CALL(HAL_DCMIPP_CSI_SetVCConfig(&hdcmipp, DCMIPP_VIRTUAL_CHANNEL0, DCMIPP_CSI_DT_BPP8));
    tof->profile = next;
    tof->p_profile = &tof->profile;
    tof->csi_width = width;
    tof->csi_height = height;
    return HAL_OK;
}

#undef INIT_CALL

/** @brief Arm frame capture and start ranging, stopping capture if sensor startup fails. */
HAL_StatusTypeDef VL53L9_start_ranging(VL53L9_TypeDef_t *tof)
{
    int result = platform_enable_event(PLATFORM_CAM_PIPE_FRAME_EVT);
    if (result != 0) return HAL_ERROR;
#ifndef VL53L9_SENSOR_ONLY
    /* Clear events before arming the snapshot, so an early callback survives. */

    VL53L9_prepare_capture(tof);
    result = platform_start_csi_pipe((uint8_t *)tof->g_csi_output_buffer);
    if (result != 0) return HAL_ERROR;
#endif

    result = vl53l9_start(tof->p_dev);
    if (result != 0)
    {
        (void)platform_stop_csi_pipe();
        return HAL_ERROR;
    }

    return HAL_OK;
}

/** @brief Copy and parse a completed CSI frame after invalidating its DMA buffer cache. */
HAL_StatusTypeDef VL53L9_process(VL53L9_TypeDef_t *tof)
{
    bool event = false;
    platform_get_event_status(PLATFORM_CAM_PIPE_FRAME_EVT, &event);
    if(!event) return HAL_OK;

    platform_acknowledge_event(PLATFORM_CAM_PIPE_FRAME_EVT);
    SCB_InvalidateDCache_by_Addr((uint32_t *)tof->g_csi_output_buffer, sizeof(tof->g_csi_output_buffer));

    memcpy(tof->frame_payload, (const void *)tof->g_csi_output_buffer,
           (size_t)tof->csi_width * tof->csi_height);
    int status = vl53l9_utils_parse_frame(tof->frame_payload,
                        (size_t)tof->csi_width * tof->csi_height, &tof->frame);
    return status == 0 ? HAL_OK : HAL_ERROR;

}

/* XSHUT shutdown leaves supply rails connected. Reset reassigns I3C and reloads
 * the patch before ranging can be restarted. Called only by the sensor owner. */
/** @brief Hold the sensor in XSHUT while leaving its supply rails connected. */
HAL_StatusTypeDef VL53L9_power_down(VL53L9_TypeDef_t *tof)
{
    if (!tof) return HAL_ERROR;
    return platform_power_disable(CONF_DEVICE_ID) == 0 ? HAL_OK : HAL_ERROR;
}
