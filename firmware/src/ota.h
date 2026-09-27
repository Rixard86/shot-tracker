#ifndef OTA_H
#define OTA_H

#include <stdbool.h>

int ota_init(void);
void ota_feed(void);
bool ota_image_confirmed(void);
void ota_confirm_when_healthy(void);

#endif
