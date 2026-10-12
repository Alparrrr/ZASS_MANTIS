/* PC testi: ST'nin ff_gen_drv.h'inden sadece 'disk' tablosu (is_initialized bayragi). */
#ifndef FAKE_FF_GEN_DRV_H
#define FAKE_FF_GEN_DRV_H
#include <stdint.h>
typedef struct { uint8_t is_initialized[1]; } Disk_drvTypeDef;
#endif
