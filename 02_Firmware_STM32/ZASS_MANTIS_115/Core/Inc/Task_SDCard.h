/*
 * Task_SDCard.h
 *
 *  Created on: Feb 10, 2026
 *      Author: ALPER
 */

#ifndef INC_TASK_SDCARD_H_
#define INC_TASK_SDCARD_H_

#include <stdint.h>

/* myTask02'den ~1 sn'de bir. Kart yoksa hemen doner, 10 sn'de bir yeniden dener. */
void SD_Log_Process(void);

extern volatile uint8_t  sd_durum;          /* 0 kart yok, 1 bagli, 2 yaziyor, 3 hata */
extern volatile uint32_t sd_satir_sayisi;
extern volatile uint32_t sd_hata_sayisi;

#endif /* INC_TASK_SDCARD_H_ */
