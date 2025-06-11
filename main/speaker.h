#ifndef SPEAKER_H
#define SPEAKER_H

#include "esp_spp_api.h"

typedef void (*speaker_event_cb_t)(void);
void speaker_start(void);
void speaker_stop(void);
void speaker_spp_callback(esp_spp_cb_event_t event, esp_spp_cb_param_t *param);
void speaker_register_mode_switch_callback(speaker_event_cb_t cb);


#endif // SPEAKER_H
