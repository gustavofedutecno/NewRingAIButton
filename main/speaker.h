#ifndef SPEAKER_H
#define SPEAKER_H

#include "esp_spp_api.h"

void speaker_start(void);
void speaker_spp_callback(esp_spp_cb_event_t event, esp_spp_cb_param_t *param);

#endif // SPEAKER_H
