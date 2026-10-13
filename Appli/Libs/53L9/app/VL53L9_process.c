#include "VL53L9_process.h"
#include "stm32n6xx_hal_def.h"
#include "vl53l9.h"
#include "vl53l9_interface.h"
#include "dcmipp.h"
#include <stdbool.h>
#include <string.h>

#define INIT_CALL(call) do { if ((call) != 0) return HAL_ERROR; } while (0)

void VL53L9_prepare_capture(VL53L9_TypeDef_t *tof)
{
    if (SCB->CCR & SCB_CCR_DC_Msk)
        SCB_CleanInvalidateDCache_by_Addr((uint32_t *)tof->g_csi_output_buffer,
                                         sizeof(tof->g_csi_output_buffer));
}

HAL_StatusTypeDef VL53L9_init(VL53L9_TypeDef_t *tof)
{
    tof->p_dev = &device[CONF_DEVICE_ID];
    tof->p_profile = &g_ranging_profiles[CONF_USECASE];
    memset(&(tof->frame), 0, sizeof(tof->frame));

    INIT_CALL(vl53l9_utils_get_csi_resolution(tof->p_profile->binning, &tof->csi_width, &tof->csi_height));
    INIT_CALL(platform_power_reset(CONF_DEVICE_ID));
    INIT_CALL(platform_assign_dynamic_address());
    INIT_CALL(vl53l9_init(tof->p_dev));
    INIT_CALL(vl53l9_utils_set_profile(tof->p_dev, tof->p_profile));

    /* retrieve and override output interface parameters */
    INIT_CALL(vl53l9_get_hw_config(tof->p_dev, &tof->hw_config));

    tof->hw_config.output_interface = VL53L9_OUTPUT_CSI2;
    tof->hw_config.signaling_mode = true;
    tof->hw_config.csi_data_rate = 1e9;
    tof->hw_config.csi_virtual_channel = 0;
    tof->hw_config.csi_status_line_force_width = false;
    tof->hw_config.csi_status_line_datatype = 0x2A;
    tof->hw_config.csi_frame_datatype = 0x2A;
    tof->hw_config.csi_frame_height = tof->csi_height - 1; /* no need to consider last row containing status line */
    tof->hw_config.csi_frame_width = tof->csi_width;

    INIT_CALL(vl53l9_set_hw_config(tof->p_dev, tof->hw_config));
    INIT_CALL(vl53l9_get_hw_config(tof->p_dev, &tof->hw_config));
    INIT_CALL(HAL_DCMIPP_CSI_SetVCConfig(&hdcmipp, DCMIPP_VIRTUAL_CHANNEL0, DCMIPP_CSI_DT_BPP8));

    return HAL_OK;
}

#undef INIT_CALL

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
    if (result != 0) return HAL_ERROR;

    return HAL_OK;
}

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
