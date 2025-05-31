#ifndef MIC_H
#define MIC_H

#include "esp_spp_api.h"

void mic_start_tasks(void);
void mic_stop_tasks(void);
void mic_spp_callback(esp_spp_cb_event_t event, esp_spp_cb_param_t *param);

#endif // MIC_H
