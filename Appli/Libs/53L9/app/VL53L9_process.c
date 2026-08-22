#include "VL53L9_process.h"
#include "stm32n6xx_hal_def.h"
#include "vl53l9_interface.h"
#include <stdbool.h>
#include <string.h>



HAL_StatusTypeDef VL53L9_init(VL53L9_TypeDef_t *tof)
{
    tof->p_dev = &device[CONF_DEVICE_ID];
    tof->p_profile = &g_ranging_profiles[CONF_USECASE];
    memset(&(tof->frame), 0, sizeof(tof->frame));

    int32_t errorcode = 0;
    
    errorcode |= vl53l9_utils_get_csi_resolution(tof->p_profile->binning, &(tof->csi_width), &(tof->csi_height));

    errorcode |= platform_power_reset(CONF_DEVICE_ID);

    errorcode |= platform_assign_dynamic_address();

    errorcode |= vl53l9_init(tof->p_dev);

    errorcode |= vl53l9_utils_set_profile(tof->p_dev, tof->p_profile);

    /* retrieve and override output interface parameters */
    errorcode |= vl53l9_get_hw_config(tof->p_dev, &(tof->hw_config));

    tof->hw_config.output_interface = VL53L9_OUTPUT_CSI2;
    tof->hw_config.signaling_mode = true;
    tof->hw_config.csi_data_rate = 1e9;
    tof->hw_config.csi_virtual_channel = 0;
    tof->hw_config.csi_status_line_force_width = false;
    tof->hw_config.csi_status_line_datatype = 0x2A;
    tof->hw_config.csi_frame_datatype = 0x2A;
    tof->hw_config.csi_frame_height = tof->csi_height - 1; /* no need to consider last row containing status line */
    tof->hw_config.csi_frame_width = tof->csi_width;

    errorcode |= vl53l9_set_hw_config(tof->p_dev, tof->hw_config);

    errorcode |= platform_start_csi_pipe((uint8_t *)tof->g_csi_output_buffer);

    return (HAL_StatusTypeDef) errorcode;
}


HAL_StatusTypeDef VL53L9_start_ranging(VL53L9_TypeDef_t *tof)
{
    platform_enable_event(PLATFORM_CAM_PIPE_FRAME_EVT);
    return (HAL_StatusTypeDef) vl53l9_start(tof->p_dev);
}

HAL_StatusTypeDef VL53L9_process(VL53L9_TypeDef_t *tof)
{
    bool event = false;
    platform_get_event_status(PLATFORM_CAM_PIPE_FRAME_EVT, &event);
    if(!event) return HAL_OK;

    platform_acknowledge_event(PLATFORM_CAM_PIPE_FRAME_EVT);
    SCB_InvalidateDCache_by_Addr((uint32_t *)tof->g_csi_output_buffer, sizeof(tof->g_csi_output_buffer));

    return vl53l9_utils_parse_frame((uint8_t *)tof->g_csi_output_buffer, sizeof(tof->g_csi_output_buffer), &(tof->frame));

}