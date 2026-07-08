/*
 * Task_SDCard.c
 *
 *  Created on: Feb 10, 2026
 *      Author: ALPER
 */
#include "main.h"
#include "cmsis_os.h"
#include "app_fatfs.h"
#include "stm32g0xx_hal.h"

	extern JK_BMS_Data my_bms;
		uint8_t response = 0xFF;
 	static FATFS FatFs;
    static FIL fil;
    static FRESULT fres;
    static uint8_t isMounted = 0; // Kartın mount durumunu takip eder
    extern RTC_TimeTypeDef sTime;
    extern RTC_DateTypeDef sDate;
    FDCAN_Message_t receivedMessage[16];

void SD_Hardware_Wakeup(void)
	{
		// 1. CS High
		HAL_GPIO_WritePin(SPI1_CS_GPIO_Port, SPI1_CS_Pin, GPIO_PIN_SET);
		HAL_Delay(10);

		// 2. 80 Dummy Clock (Kartı uyandır)
		uint8_t dummy = 0xFF;
		for(int i=0; i<10; i++) {
			HAL_SPI_Transmit(&hspi1, &dummy, 1, 100);
		}

		// 3. CS Low
		HAL_GPIO_WritePin(SPI1_CS_GPIO_Port, SPI1_CS_Pin, GPIO_PIN_RESET);

		// 4. CMD0 (GO_IDLE_STATE)
		uint8_t cmd0[] = {0x40, 0x00, 0x00, 0x00, 0x00, 0x95};
		HAL_SPI_Transmit(&hspi1, cmd0, 6, 100);

		// 5. Cevap bekle

		for(int i=0; i<10; i++) {
			HAL_SPI_Receive(&hspi1, &response, 1, 100);
			if(response != 0xFF) break;
		}

		// 6. CS High (Bırak)
		HAL_GPIO_WritePin(SPI1_CS_GPIO_Port, SPI1_CS_Pin, GPIO_PIN_SET);
	}

void SD_Log_Process(void){


    char pathBuffer[100]; // Yolu tutmak için güvenli boyut
    char logBuffer[512];  // Log satırı için
    uint16_t total_volt;
    static uint16_t cell_voltages[24]; // 24 hücre için static (Stack dostu)
    uint8_t temperatures[3];
    int16_t Curr;
    uint8_t remain_capacity;
    UINT bw; // Bytes written

    // ---------------------------------------------------------
    // 1. MOUNT KONTROLÜ (Sürekli mount yapmamak için)
    // ---------------------------------------------------------
    if (isMounted == 0) {

    	SD_Hardware_Wakeup();
    	osDelay(25);

        fres = f_mount(&FatFs, "", 1);
        if (fres == FR_OK) {
            isMounted = 1; // Başarılı, bayrağı kaldır
        } else {

            return;
        }
    }


    // ---------------------------------------------------------
    // 2. VERİLERİ HAZIRLA (Senin Kodun)
    // ---------------------------------------------------------

    // RTC Okuma
    HAL_RTC_GetTime(&hrtc, &sTime, RTC_FORMAT_BIN);
    HAL_RTC_GetDate(&hrtc, &sDate, RTC_FORMAT_BIN);


    // ADIM 1: YIL Klasörü
        sprintf(pathBuffer, "20%02d", sDate.Year);
        f_mkdir(pathBuffer);

        // ADIM 2: AY Klasörü
        sprintf(pathBuffer, "20%02d/%02d_20%02d", sDate.Year, sDate.Month, sDate.Year);
        f_mkdir(pathBuffer);

        // ADIM 3: DOSYA YOLUNU HAZIRLA
        sprintf(pathBuffer, "20%02d/%02d_20%02d/%02d_%02d_20%02d_LOG.CSV",
            sDate.Year,
            sDate.Month, sDate.Year,
            sDate.Date, sDate.Month, sDate.Year
        );

        // ---------------------------------------------------------
        // 3. CSV FORMATINDA LOG OLUŞTURMA (YENİ BMS VERİLERİ İLE)
        // ---------------------------------------------------------
        // Eski CAN verileri yerine my_bms struct'ı içindeki veriler kullanıldı.
        // DİKKAT: BMS_Mesajini_Coz fonksiyonunda şu an sadece ilk 8 hücre okunuyor.
        // Logda 24 hücrelik yer açtım. Eğer BMS'in 24 hücre okuyorsa BMS_Mesajini_Coz
        // içindeki for döngüsünü (i < 24) olarak değiştirmelisin.

        int len = snprintf(logBuffer, sizeof(logBuffer),
                    // Tarih Saat (6 parametre) + Voltaj (1) + 8 Hücre (8) + Sıcaklıklar (2) + Akım (1) + SoC (1) = 19 Parametre
                    "20%02d.%02d.%02d %02d:%02d:%02d,%.2f,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%.1f,%d\n",

                    sDate.Year, sDate.Month, sDate.Date,
                    sTime.Hours, sTime.Minutes, sTime.Seconds,

                    my_bms.battery_volts, // %.2f

                    // 8 Hücre Voltajı (%d, %d...)
                    my_bms.cell_volts[0],  my_bms.cell_volts[1],  my_bms.cell_volts[2],  my_bms.cell_volts[3],
                    my_bms.cell_volts[4],  my_bms.cell_volts[5],  my_bms.cell_volts[6],  my_bms.cell_volts[7],

                    // Sıcaklıklar (%d, %d)
                    my_bms.mos_temp,
                    my_bms.batt_temp,

                    // Akım (%.1f)
                    (float)my_bms.battery_current / 10.0f,

                    // Şarj Yüzdesi SoC (%d)
                    my_bms.soc
                );

        if(fres == FR_OK )
            {
            	fres = f_open(&fil, pathBuffer, FA_OPEN_APPEND | FA_WRITE);
                f_write(&fil, logBuffer, len, &bw);
                f_close(&fil); // Veriyi fiziksel olarak kaydeder
            }
            else
            {
                // Dosya açılamadıysa (Kart çıkarıldı, bozuldu vs.)
                // Mount bayrağını indir ki bir sonraki turda tekrar Init yapsın.
            	f_mount(NULL, pathBuffer, 0); // veya f_mount(0, "", 0);
                isMounted = 0;
            }
}

/*void SD_Log_Process(void)
{
    // --- STATİK DEĞİŞKENLER (Stack Tasarrufu için) ---
    // Fonksiyon her çağrıldığında hafızada yer kaplamaz, kalıcıdır.

    // Senin değişkenlerin (Aynen korundu)

    char pathBuffer[100]; // Yolu tutmak için güvenli boyut
    char logBuffer[512];  // Log satırı için
    uint16_t total_volt;
    static uint16_t cell_voltages[24]; // 24 hücre için static (Stack dostu)
    uint8_t temperatures[3];
    int16_t Curr;
    uint8_t remain_capacity;
    UINT bw; // Bytes written

    // ---------------------------------------------------------
    // 1. MOUNT KONTROLÜ (Sürekli mount yapmamak için)
    // ---------------------------------------------------------
    if (isMounted == 0) {

    	SD_Hardware_Wakeup();
    	osDelay(25);

        fres = f_mount(&FatFs, "", 1);
        if (fres == FR_OK) {
            isMounted = 1; // Başarılı, bayrağı kaldır
        } else {

            return;
        }
    }

    // ---------------------------------------------------------
    // 2. VERİLERİ HAZIRLA (Senin Kodun)
    // ---------------------------------------------------------

    // RTC Okuma
    HAL_RTC_GetTime(&hrtc, &sTime, RTC_FORMAT_BIN);
    HAL_RTC_GetDate(&hrtc, &sDate, RTC_FORMAT_BIN);

    // --- KLASÖRLERİ OLUŞTUR (ADIM ADIM) ---

    // ADIM 1: YIL Klasörü
    sprintf(pathBuffer, "20%02d", sDate.Year);
    f_mkdir(pathBuffer); // Klasör varsa hata verir (FR_EXIST), sorun değil devam eder.

    // ADIM 2: AY Klasörü
    sprintf(pathBuffer, "20%02d/%02d_20%02d", sDate.Year, sDate.Month, sDate.Year);
    f_mkdir(pathBuffer);

    // C. DOSYA YOLUNU HAZIRLA
    sprintf(pathBuffer, "20%02d/%02d_20%02d/%02d_%02d_20%02d_LOG.CSV",
        sDate.Year,
        sDate.Month, sDate.Year,
        sDate.Date, sDate.Month, sDate.Year
    );

    // -------------------------
    // 3. CAN VERİLERİNİ AYIKLA
    // -------------------------

    total_volt = (receivedMessage[8].Data[1] << 8) | receivedMessage[8].Data[0];

    // Hücre 0-3
    cell_voltages[0]   = (receivedMessage[0].Data[1] << 8) | receivedMessage[0].Data[0];
    cell_voltages[1]   = (receivedMessage[0].Data[3] << 8) | receivedMessage[0].Data[2];
    cell_voltages[2]   = (receivedMessage[0].Data[5] << 8) | receivedMessage[0].Data[4];
    cell_voltages[3]   = (receivedMessage[0].Data[7] << 8) | receivedMessage[0].Data[6];

    // Hücre 4-7
    cell_voltages[4]   = (receivedMessage[1].Data[1] << 8) | receivedMessage[1].Data[0];
    cell_voltages[5]   = (receivedMessage[1].Data[3] << 8) | receivedMessage[1].Data[2];
    cell_voltages[6]   = (receivedMessage[1].Data[5] << 8) | receivedMessage[1].Data[4];
    cell_voltages[7]   = (receivedMessage[1].Data[7] << 8) | receivedMessage[1].Data[6];

    // Hücre 8-11
    cell_voltages[8]   = (receivedMessage[2].Data[1] << 8) | receivedMessage[2].Data[0];
    cell_voltages[9]   = (receivedMessage[2].Data[3] << 8) | receivedMessage[2].Data[2];
    cell_voltages[10]  = (receivedMessage[2].Data[5] << 8) | receivedMessage[2].Data[4];
    cell_voltages[11]  = (receivedMessage[2].Data[7] << 8) | receivedMessage[2].Data[6];

    // Hücre 12-15
    cell_voltages[12]  = (receivedMessage[3].Data[1] << 8) | receivedMessage[3].Data[0];
    cell_voltages[13]  = (receivedMessage[3].Data[3] << 8) | receivedMessage[3].Data[2];
    cell_voltages[14]  = (receivedMessage[3].Data[5] << 8) | receivedMessage[3].Data[4];
    cell_voltages[15]  = (receivedMessage[3].Data[7] << 8) | receivedMessage[3].Data[6];

    // Hücre 16-19
    cell_voltages[16]  = (receivedMessage[4].Data[1] << 8) | receivedMessage[4].Data[0];
    cell_voltages[17]  = (receivedMessage[4].Data[3] << 8) | receivedMessage[4].Data[2];
    cell_voltages[18]  = (receivedMessage[4].Data[5] << 8) | receivedMessage[4].Data[4];
    cell_voltages[19]  = (receivedMessage[4].Data[7] << 8) | receivedMessage[4].Data[6];

    // Hücre 20-23
    cell_voltages[20]  = (receivedMessage[5].Data[1] << 8) | receivedMessage[5].Data[0];
    cell_voltages[21]  = (receivedMessage[5].Data[3] << 8) | receivedMessage[5].Data[2];
    cell_voltages[22]  = (receivedMessage[5].Data[5] << 8) | receivedMessage[5].Data[4];
    cell_voltages[23]  = (receivedMessage[5].Data[7] << 8) | receivedMessage[5].Data[6];

    // Sıcaklıklar
    temperatures[0] = receivedMessage[11].Data[1] - 50;
    temperatures[1] = receivedMessage[11].Data[2] - 50;
    temperatures[2] = receivedMessage[11].Data[3] - 50;

    // Kapasite ve Akım
    remain_capacity = receivedMessage[8].Data[4];
    // Not: Akım negatif olabileceği için int16 cast işlemi önemlidir
    Curr = ((((receivedMessage[8].Data[3] << 8) | receivedMessage[8].Data[2])/10) - 400);

    // ---------------------------------------------------------
    // 4. CSV FORMATINDA LOG OLUŞTURMA
    // ---------------------------------------------------------

    // snprintf kullanıyoruz, sprintf'ten daha güvenlidir (Taşmayı önler)
    int len = snprintf(logBuffer, sizeof(logBuffer),
        "20%02d.%02d.%02d %02d:%02d:%02d,%.2f,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%.1f,%.1f,%.1f,%.3f,%d\n",

        sDate.Year, sDate.Month, sDate.Date,
        sTime.Hours, sTime.Minutes, sTime.Seconds,

        (float)total_volt / 10.0f, // Eğer birim 0.1V ise float çevrimi gerekebilir, değilse düz yaz

        cell_voltages[0] , cell_voltages[1] , cell_voltages[2] , cell_voltages[3] ,
        cell_voltages[4] , cell_voltages[5] , cell_voltages[6] , cell_voltages[7] ,
        cell_voltages[8] , cell_voltages[9] , cell_voltages[10], cell_voltages[11],
        cell_voltages[12], cell_voltages[13], cell_voltages[14], cell_voltages[15],
        cell_voltages[16], cell_voltages[17], cell_voltages[18], cell_voltages[19],
        cell_voltages[20], cell_voltages[21], cell_voltages[22], cell_voltages[23],
        // Not: Log formatın sadece 8 hücre içeriyor, 24 hücre için buraya ekleme yapmalısın.

        (float)temperatures[0], (float)temperatures[1], (float)temperatures[2],

        (float)Curr / 10.0f, // Birim dönüşümü gerekiyorsa

        remain_capacity
    );

    // ---------------------------------------------------------
    // 5. YAZ VE KAYDET
    // ---------------------------------------------------------


    if(fres == FR_OK )
    {
    	fres = f_open(&fil, pathBuffer, FA_OPEN_APPEND | FA_WRITE);
        f_write(&fil, logBuffer, len, &bw);
        f_close(&fil); // Veriyi fiziksel olarak kaydeder
    }
    else
    {
        // Dosya açılamadıysa (Kart çıkarıldı, bozuldu vs.)
        // Mount bayrağını indir ki bir sonraki turda tekrar Init yapsın.
    	f_mount(NULL, pathBuffer, 0); // veya f_mount(0, "", 0);
        isMounted = 0;
    }
}*/
