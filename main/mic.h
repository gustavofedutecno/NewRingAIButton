#ifndef MIC_H
#define MIC_H

#include "esp_spp_api.h"

typedef void (*mic_event_cb_t)(void);
void mic_start(void);
void mic_stop(void);
void mic_spp_callback(esp_spp_cb_event_t event, esp_spp_cb_param_t *param);
void mic_register_mode_switch_callback(mic_event_cb_t cb);
void mic_set_client_handle(uint32_t handle);
bool mic_is_i2s_installed(void);
void mic_uninstall_i2s(void);


#endif // MIC_H
