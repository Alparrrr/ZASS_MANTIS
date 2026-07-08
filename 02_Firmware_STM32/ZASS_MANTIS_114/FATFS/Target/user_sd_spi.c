#include "user_sd_spi.h"
#include <stdlib.h>
#include <string.h>

/* ==============================================================================
   STM32G0 DONANIM ALT KATMANI (HARDWARE ABSTRACTION LAYER)
   Bu kısım G0'ın SPI FIFO kilitlenmelerini önleyen doğrudan Register erişimidir.
============================================================================== */
static void SD_IO_Init(void) {
    // CS pini boşta HIGH olmalı
    HAL_GPIO_WritePin(SD_CS_PORT, SD_CS_PIN, GPIO_PIN_SET);
}

static void SD_IO_CSState(uint8_t state) {
    if(state == 1) {
        HAL_GPIO_WritePin(SD_CS_PORT, SD_CS_PIN, GPIO_PIN_SET);   // Kartı Bırak
    } else {
        HAL_GPIO_WritePin(SD_CS_PORT, SD_CS_PIN, GPIO_PIN_RESET); // Kartı Seç
    }
}

static uint8_t SD_IO_WriteByte(uint8_t Data) {
    // TX kuyruğu boşalana kadar bekle
    while ((SD_SPI_HANDLE.Instance->SR & SPI_SR_TXE) == 0) {}

    // STM32G0 Hayati Kuralı: FIFO'ya ZORLA 8-bit yazılır
    *((__IO uint8_t *)&SD_SPI_HANDLE.Instance->DR) = Data;

    // RX verisi gelene kadar bekle
    while ((SD_SPI_HANDLE.Instance->SR & SPI_SR_RXNE) == 0) {}

    // Gelen veriyi ZORLA 8-bit oku
    return *((__IO uint8_t *)&SD_SPI_HANDLE.Instance->DR);
}

static void SD_IO_WriteReadData(const uint8_t *DataIn, uint8_t *DataOut, uint16_t DataLength) {
    for(uint16_t i = 0; i < DataLength; i++) {
        uint8_t tx = (DataIn != NULL) ? DataIn[i] : 0xFF;
        uint8_t rx = SD_IO_WriteByte(tx);
        if(DataOut != NULL) {
            DataOut[i] = rx;
        }
    }
}
/* ============================================================================== */


/* Özel Değişkenler ve Makrolar */
#define SD_DUMMY_BYTE            0xFF
#define SD_MAX_FRAME_LENGTH      17
#define SD_CMD_LENGTH            6
#define SD_MAX_TRY               100
#define SD_TOKEN_START_DATA_SINGLE_BLOCK_READ    0xFE
#define SD_TOKEN_START_DATA_SINGLE_BLOCK_WRITE   0xFE

#define SD_CMD_GO_IDLE_STATE     0
#define SD_CMD_SEND_IF_COND      8
#define SD_CMD_SEND_CSD          9
#define SD_CMD_SEND_CID          10
#define SD_CMD_SEND_STATUS       13
#define SD_CMD_SET_BLOCKLEN      16
#define SD_CMD_READ_SINGLE_BLOCK 17
#define SD_CMD_WRITE_SINGLE_BLOCK 24
#define SD_CMD_SD_ERASE_GRP_START 32
#define SD_CMD_SD_ERASE_GRP_END  33
#define SD_CMD_ERASE             38
#define SD_CMD_SD_APP_OP_COND    41
#define SD_CMD_APP_CMD           55
#define SD_CMD_READ_OCR          58

#define SD_R1_NO_ERROR           (0x00)
#define SD_R1_IN_IDLE_STATE      (0x01)
#define SD_R1_ILLEGAL_COMMAND    (0x04)
#define SD_R2_NO_ERROR           (0x00)
#define SD_DATA_OK               (0x05)
#define SD_DATA_OTHER_ERROR      (0xFF)
#define SD_DATA_CRC_ERROR        (0x0B)
#define SD_DATA_WRITE_ERROR      (0x0D)

typedef enum {
    SD_ANSWER_R1_EXPECTED,
    SD_ANSWER_R1B_EXPECTED,
    SD_ANSWER_R2_EXPECTED,
    SD_ANSWER_R3_EXPECTED,
    SD_ANSWER_R4R5_EXPECTED,
    SD_ANSWER_R7_EXPECTED,
} SD_Answer_type;

typedef struct {
    uint8_t r1; uint8_t r2; uint8_t r3; uint8_t r4; uint8_t r5;
} SD_CmdAnswer_typedef;

__IO uint8_t SdStatus = SD_NOT_PRESENT;
uint16_t flag_SDHC = 0;

static uint8_t SD_GetCIDRegister(SD_CID* Cid);
static uint8_t SD_GetCSDRegister(SD_CSD* Csd);
static uint8_t SD_GetDataResponse(void);
static uint8_t SD_GoIdleState(void);
static SD_CmdAnswer_typedef SD_SendCmd(uint8_t Cmd, uint32_t Arg, uint8_t Crc, uint8_t Answer);
static uint8_t SD_WaitData(uint8_t data);
static uint8_t SD_ReadData(void);


/* --- PUBLIC FONKSİYONLAR --- */

uint8_t BSP_SD_Init(void) {
    SD_IO_Init();

    // 1. G0 KRİTİK YAMASI: CubeMX SPI'ı başlatır ama aktif (ENABLE) etmez.
    // Register kullandığımız için SPI'ı manuel açmalıyız.
    SET_BIT(SD_SPI_HANDLE.Instance->CR1, SPI_CR1_SPE);

    // 2. BAŞLANGIÇ HIZINI DÜŞÜRME (Maks 400 kHz olmalı)
    // SPI'ı kapat, Prescaler'ı en yüksek (En Yavaş) değere al ve geri aç
    CLEAR_BIT(SD_SPI_HANDLE.Instance->CR1, SPI_CR1_SPE);
    MODIFY_REG(SD_SPI_HANDLE.Instance->CR1, SPI_CR1_BR, SPI_BAUDRATEPRESCALER_256);
    SET_BIT(SD_SPI_HANDLE.Instance->CR1, SPI_CR1_SPE);

    // 3. KART UYANDIRMA SEKANSI (74+ Dummy Clock)
    // CS pini HIGH (1) olmalı ve havaya 80 adet saat vuruşu basılmalı!
    SD_IO_CSState(1);
    HAL_Delay(10); // Voltajın oturmasını 10ms bekle
    for(uint8_t i = 0; i < 10; i++) {
        SD_IO_WriteByte(0xFF); // 10 byte * 8 bit = 80 clock sinyali
    }

    if(BSP_SD_IsDetected() == SD_NOT_PRESENT) {
        SdStatus = SD_NOT_PRESENT;
        return MSD_ERROR;
    } else {
        SdStatus = SD_PRESENT;
    }

    // 4. Kartı Uyandır (CMD0 vs..)
    uint8_t state = SD_GoIdleState();

    // 5. Kart uyandıysa SPI hızını tekrar yükselt! (Normal Çalışma Hızı)
    if(state == MSD_OK) {
        CLEAR_BIT(SD_SPI_HANDLE.Instance->CR1, SPI_CR1_SPE);
        // Hızı donanımına göre artırıyoruz (Örn: Prescaler 16 veya 8)
        MODIFY_REG(SD_SPI_HANDLE.Instance->CR1, SPI_CR1_BR, SPI_BAUDRATEPRESCALER_16);
        SET_BIT(SD_SPI_HANDLE.Instance->CR1, SPI_CR1_SPE);
    }

    return state;
}

uint8_t BSP_SD_IsDetected(void) {
#if USE_SD_DETECT_PIN
    if(HAL_GPIO_ReadPin(SD_DETECT_PORT, SD_DETECT_PIN) != GPIO_PIN_RESET) {
        return SD_NOT_PRESENT;
    }
#endif
    return SD_PRESENT; // Pin kullanılmıyorsa veya LOW okunduysa hep takılı say
}

uint8_t BSP_SD_GetCardInfo(SD_CardInfo *pCardInfo) {
    uint8_t status;
    status = SD_GetCSDRegister(&(pCardInfo->Csd));
    status|= SD_GetCIDRegister(&(pCardInfo->Cid));
    if(flag_SDHC == 1) {
        pCardInfo->LogBlockSize = 512;
        pCardInfo->CardBlockSize = 512;
        pCardInfo->CardCapacity = (pCardInfo->Csd.version.v2.DeviceSize + 1) * 1024 * pCardInfo->LogBlockSize;
        pCardInfo->LogBlockNbr = (pCardInfo->CardCapacity) / (pCardInfo->LogBlockSize);
    } else {
        pCardInfo->CardCapacity = (pCardInfo->Csd.version.v1.DeviceSize + 1);
        pCardInfo->CardCapacity *= (1 << (pCardInfo->Csd.version.v1.DeviceSizeMul + 2));
        pCardInfo->LogBlockSize = 512;
        pCardInfo->CardBlockSize = 1 << (pCardInfo->Csd.RdBlockLen);
        pCardInfo->CardCapacity *= pCardInfo->CardBlockSize;
        pCardInfo->LogBlockNbr = (pCardInfo->CardCapacity) / (pCardInfo->LogBlockSize);
    }
    return status;
}

uint8_t BSP_SD_ReadBlocks(uint32_t *pData, uint32_t ReadAddr, uint32_t NumOfBlocks, uint32_t Timeout) {
    uint32_t offset = 0;
    uint32_t addr;
    uint8_t retr = BSP_SD_ERROR;
    uint8_t *ptr = NULL;
    SD_CmdAnswer_typedef response;
    uint16_t BlockSize = 512;

    response = SD_SendCmd(SD_CMD_SET_BLOCKLEN, BlockSize, 0xFF, SD_ANSWER_R1_EXPECTED);
    SD_IO_CSState(1);
    SD_IO_WriteByte(SD_DUMMY_BYTE);
    if (response.r1 != SD_R1_NO_ERROR) goto error;

    ptr = malloc(sizeof(uint8_t)*BlockSize);
    if(ptr == NULL) goto error;
    memset(ptr, SD_DUMMY_BYTE, sizeof(uint8_t)*BlockSize);

    addr = (ReadAddr * ((flag_SDHC == 1) ? 1 : BlockSize));

    while (NumOfBlocks--) {
        response = SD_SendCmd(SD_CMD_READ_SINGLE_BLOCK, addr, 0xFF, SD_ANSWER_R1_EXPECTED);
        if (response.r1 != SD_R1_NO_ERROR) goto error;

        if (SD_WaitData(SD_TOKEN_START_DATA_SINGLE_BLOCK_READ) == MSD_OK) {
            SD_IO_WriteReadData(ptr, (uint8_t*)pData + offset, BlockSize);
            offset += BlockSize;
            addr = ((flag_SDHC == 1) ? (addr + 1) : (addr + BlockSize));
            SD_IO_WriteByte(SD_DUMMY_BYTE);
            SD_IO_WriteByte(SD_DUMMY_BYTE);
        } else goto error;

        SD_IO_CSState(1);
        SD_IO_WriteByte(SD_DUMMY_BYTE);
    }
    retr = MSD_OK;

error:
    SD_IO_CSState(1);
    SD_IO_WriteByte(SD_DUMMY_BYTE);
    if(ptr != NULL) free(ptr);
    return retr;
}

uint8_t BSP_SD_WriteBlocks(uint32_t *pData, uint32_t WriteAddr, uint32_t NumOfBlocks, uint32_t Timeout) {
    uint32_t offset = 0;
    uint32_t addr;
    uint8_t retr = BSP_SD_ERROR;
    uint8_t *ptr = NULL;
    SD_CmdAnswer_typedef response;
    uint16_t BlockSize = 512;

    response = SD_SendCmd(SD_CMD_SET_BLOCKLEN, BlockSize, 0xFF, SD_ANSWER_R1_EXPECTED);
    SD_IO_CSState(1);
    SD_IO_WriteByte(SD_DUMMY_BYTE);
    if (response.r1 != SD_R1_NO_ERROR) goto error;

    ptr = malloc(sizeof(uint8_t)*BlockSize);
    if (ptr == NULL) goto error;

    addr = (WriteAddr * ((flag_SDHC == 1) ? 1 : BlockSize));

    while (NumOfBlocks--) {
        response = SD_SendCmd(SD_CMD_WRITE_SINGLE_BLOCK, addr, 0xFF, SD_ANSWER_R1_EXPECTED);
        if (response.r1 != SD_R1_NO_ERROR) goto error;

        SD_IO_WriteByte(SD_DUMMY_BYTE);
        SD_IO_WriteByte(SD_DUMMY_BYTE);
        SD_IO_WriteByte(SD_TOKEN_START_DATA_SINGLE_BLOCK_WRITE);

        SD_IO_WriteReadData((uint8_t*)pData + offset, ptr, BlockSize);

        offset += BlockSize;
        addr = ((flag_SDHC == 1) ? (addr + 1) : (addr + BlockSize));

        SD_IO_WriteByte(SD_DUMMY_BYTE);
        SD_IO_WriteByte(SD_DUMMY_BYTE);

        if (SD_GetDataResponse() != SD_DATA_OK) goto error;

        SD_IO_CSState(1);
        SD_IO_WriteByte(SD_DUMMY_BYTE);
    }
    retr = MSD_OK;

error:
    if(ptr != NULL) free(ptr);
    SD_IO_CSState(1);
    SD_IO_WriteByte(SD_DUMMY_BYTE);
    return retr;
}

uint8_t BSP_SD_Erase(uint32_t StartAddr, uint32_t EndAddr) {
    uint8_t retr = BSP_SD_ERROR;
    SD_CmdAnswer_typedef response;
    uint16_t BlockSize = 512;

    response = SD_SendCmd(SD_CMD_SD_ERASE_GRP_START, (StartAddr) * (flag_SDHC == 1 ? 1 : BlockSize), 0xFF, SD_ANSWER_R1_EXPECTED);
    SD_IO_CSState(1);
    SD_IO_WriteByte(SD_DUMMY_BYTE);
    if (response.r1 == SD_R1_NO_ERROR) {
        response = SD_SendCmd(SD_CMD_SD_ERASE_GRP_END, (EndAddr*512) * (flag_SDHC == 1 ? 1 : BlockSize), 0xFF, SD_ANSWER_R1_EXPECTED);
        SD_IO_CSState(1);
        SD_IO_WriteByte(SD_DUMMY_BYTE);
        if (response.r1 == SD_R1_NO_ERROR) {
            response = SD_SendCmd(SD_CMD_ERASE, 0, 0xFF, SD_ANSWER_R1B_EXPECTED);
            if (response.r1 == SD_R1_NO_ERROR) retr = MSD_OK;
            SD_IO_CSState(1);
            SD_IO_WriteByte(SD_DUMMY_BYTE);
        }
    }
    return retr;
}

uint8_t BSP_SD_GetCardState(void) {
    SD_CmdAnswer_typedef retr;
    retr = SD_SendCmd(SD_CMD_SEND_STATUS, 0, 0xFF, SD_ANSWER_R2_EXPECTED);
    SD_IO_CSState(1);
    SD_IO_WriteByte(SD_DUMMY_BYTE);
    if(( retr.r1 == SD_R1_NO_ERROR) && ( retr.r2 == SD_R2_NO_ERROR)) return MSD_OK;
    return BSP_SD_ERROR;
}

/* --- PRIVATE (ALT) FONKSİYONLAR --- */

static uint8_t SD_GetCSDRegister(SD_CSD* Csd) {
    uint16_t counter = 0;
    uint8_t CSD_Tab[16];
    uint8_t retr = BSP_SD_ERROR;
    SD_CmdAnswer_typedef response;

    response = SD_SendCmd(SD_CMD_SEND_CSD, 0, 0xFF, SD_ANSWER_R1_EXPECTED);
    if(response.r1 == SD_R1_NO_ERROR) {
        if (SD_WaitData(SD_TOKEN_START_DATA_SINGLE_BLOCK_READ) == MSD_OK) {
            for (counter = 0; counter < 16; counter++) CSD_Tab[counter] = SD_IO_WriteByte(SD_DUMMY_BYTE);
            SD_IO_WriteByte(SD_DUMMY_BYTE); SD_IO_WriteByte(SD_DUMMY_BYTE);

            Csd->CSDStruct = (CSD_Tab[0] & 0xC0) >> 6;
            Csd->Reserved1 =  CSD_Tab[0] & 0x3F;
            Csd->TAAC = CSD_Tab[1];
            Csd->NSAC = CSD_Tab[2];
            Csd->MaxBusClkFrec = CSD_Tab[3];
            Csd->CardComdClasses = (CSD_Tab[4] << 4) | ((CSD_Tab[5] & 0xF0) >> 4);
            Csd->RdBlockLen = CSD_Tab[5] & 0x0F;
            Csd->PartBlockRead   = (CSD_Tab[6] & 0x80) >> 7;
            Csd->WrBlockMisalign = (CSD_Tab[6] & 0x40) >> 6;
            Csd->RdBlockMisalign = (CSD_Tab[6] & 0x20) >> 5;
            Csd->DSRImpl         = (CSD_Tab[6] & 0x10) >> 4;

            if(flag_SDHC == 0) {
                Csd->version.v1.Reserved1 = ((CSD_Tab[6] & 0x0C) >> 2);
                Csd->version.v1.DeviceSize =  ((CSD_Tab[6] & 0x03) << 10) | (CSD_Tab[7] << 2) | ((CSD_Tab[8] & 0xC0) >> 6);
                Csd->version.v1.MaxRdCurrentVDDMin = (CSD_Tab[8] & 0x38) >> 3;
                Csd->version.v1.MaxRdCurrentVDDMax = (CSD_Tab[8] & 0x07);
                Csd->version.v1.MaxWrCurrentVDDMin = (CSD_Tab[9] & 0xE0) >> 5;
                Csd->version.v1.MaxWrCurrentVDDMax = (CSD_Tab[9] & 0x1C) >> 2;
                Csd->version.v1.DeviceSizeMul = ((CSD_Tab[9] & 0x03) << 1) |((CSD_Tab[10] & 0x80) >> 7);
            } else {
                Csd->version.v2.Reserved1 = ((CSD_Tab[6] & 0x0F) << 2) | ((CSD_Tab[7] & 0xC0) >> 6);
                Csd->version.v2.DeviceSize= ((CSD_Tab[7] & 0x3F) << 16) | (CSD_Tab[8] << 8) | CSD_Tab[9];
                Csd->version.v2.Reserved2 = ((CSD_Tab[10] & 0x80) >> 8);
            }
            retr = MSD_OK;
        }
    }
    SD_IO_CSState(1); SD_IO_WriteByte(SD_DUMMY_BYTE);
    return retr;
}

static uint8_t SD_GetCIDRegister(SD_CID* Cid) {
    uint32_t counter = 0;
    uint8_t retr = BSP_SD_ERROR;
    uint8_t CID_Tab[16];
    SD_CmdAnswer_typedef response;

    response = SD_SendCmd(SD_CMD_SEND_CID, 0, 0xFF, SD_ANSWER_R1_EXPECTED);
    if(response.r1 == SD_R1_NO_ERROR) {
        if(SD_WaitData(SD_TOKEN_START_DATA_SINGLE_BLOCK_READ) == MSD_OK) {
            for (counter = 0; counter < 16; counter++) CID_Tab[counter] = SD_IO_WriteByte(SD_DUMMY_BYTE);
            SD_IO_WriteByte(SD_DUMMY_BYTE); SD_IO_WriteByte(SD_DUMMY_BYTE);
            retr = MSD_OK;
        }
    }
    SD_IO_CSState(1); SD_IO_WriteByte(SD_DUMMY_BYTE);
    return retr;
}

static SD_CmdAnswer_typedef SD_SendCmd(uint8_t Cmd, uint32_t Arg, uint8_t Crc, uint8_t Answer) {
    uint8_t frame[SD_CMD_LENGTH], frameout[SD_CMD_LENGTH];
    SD_CmdAnswer_typedef retr = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

    frame[0] = (Cmd | 0x40);
    frame[1] = (uint8_t)(Arg >> 24);
    frame[2] = (uint8_t)(Arg >> 16);
    frame[3] = (uint8_t)(Arg >> 8);
    frame[4] = (uint8_t)(Arg);
    frame[5] = (Crc | 0x01);

    SD_IO_CSState(0);
    SD_IO_WriteReadData(frame, frameout, SD_CMD_LENGTH);

    switch(Answer) {
    case SD_ANSWER_R1_EXPECTED :
        retr.r1 = SD_ReadData();
        break;
    case SD_ANSWER_R1B_EXPECTED :
        retr.r1 = SD_ReadData();
        retr.r2 = SD_IO_WriteByte(SD_DUMMY_BYTE);
        SD_IO_CSState(1); HAL_Delay(1); SD_IO_CSState(0);
        while (SD_IO_WriteByte(SD_DUMMY_BYTE) != 0xFF);
        break;
    case SD_ANSWER_R2_EXPECTED :
        retr.r1 = SD_ReadData();
        retr.r2 = SD_IO_WriteByte(SD_DUMMY_BYTE);
        break;
    case SD_ANSWER_R3_EXPECTED :
    case SD_ANSWER_R7_EXPECTED :
        retr.r1 = SD_ReadData();
        retr.r2 = SD_IO_WriteByte(SD_DUMMY_BYTE);
        retr.r3 = SD_IO_WriteByte(SD_DUMMY_BYTE);
        retr.r4 = SD_IO_WriteByte(SD_DUMMY_BYTE);
        retr.r5 = SD_IO_WriteByte(SD_DUMMY_BYTE);
        break;
    default : break;
    }
    return retr;
}

static uint8_t SD_GetDataResponse(void) {
    uint8_t dataresponse;
    uint8_t rvalue = SD_DATA_OTHER_ERROR;

    dataresponse = SD_IO_WriteByte(SD_DUMMY_BYTE);
    SD_IO_WriteByte(SD_DUMMY_BYTE);

    switch (dataresponse & 0x1F) {
    case SD_DATA_OK:
        rvalue = SD_DATA_OK;
        SD_IO_CSState(1); SD_IO_CSState(0);
        while (SD_IO_WriteByte(SD_DUMMY_BYTE) != 0xFF);
        break;
    case 0x0B: rvalue = 0x0B; break; // CRC Error
    case 0x0D: rvalue = 0x0D; break; // Write Error
    default: break;
    }
    return rvalue;
}

static uint8_t SD_GoIdleState(void) {
    SD_CmdAnswer_typedef response;
    __IO uint8_t counter = 0;

    do {
        counter++;
        response = SD_SendCmd(SD_CMD_GO_IDLE_STATE, 0, 0x95, SD_ANSWER_R1_EXPECTED);
        SD_IO_CSState(1);
        SD_IO_WriteByte(SD_DUMMY_BYTE);
        if(counter >= SD_MAX_TRY) return BSP_SD_ERROR;
    } while(response.r1 != SD_R1_IN_IDLE_STATE);

    response = SD_SendCmd(SD_CMD_SEND_IF_COND, 0x1AA, 0x87, SD_ANSWER_R7_EXPECTED);
    SD_IO_CSState(1); SD_IO_WriteByte(SD_DUMMY_BYTE);

    if((response.r1  & SD_R1_ILLEGAL_COMMAND) == SD_R1_ILLEGAL_COMMAND) {
        do {
            response = SD_SendCmd(SD_CMD_APP_CMD, 0x00000000, 0xFF, SD_ANSWER_R1_EXPECTED);
            SD_IO_CSState(1); SD_IO_WriteByte(SD_DUMMY_BYTE);
            response = SD_SendCmd(SD_CMD_SD_APP_OP_COND, 0x00000000, 0xFF, SD_ANSWER_R1_EXPECTED);
            SD_IO_CSState(1); SD_IO_WriteByte(SD_DUMMY_BYTE);
        } while(response.r1 == SD_R1_IN_IDLE_STATE);
        flag_SDHC = 0;
    } else if(response.r1 == SD_R1_IN_IDLE_STATE) {
        do {
            response = SD_SendCmd(SD_CMD_APP_CMD, 0, 0xFF, SD_ANSWER_R1_EXPECTED);
            SD_IO_CSState(1); SD_IO_WriteByte(SD_DUMMY_BYTE);
            response = SD_SendCmd(SD_CMD_SD_APP_OP_COND, 0x40000000, 0xFF, SD_ANSWER_R1_EXPECTED);
            SD_IO_CSState(1); SD_IO_WriteByte(SD_DUMMY_BYTE);
        } while(response.r1 == SD_R1_IN_IDLE_STATE);

        response = SD_SendCmd(SD_CMD_READ_OCR, 0x00000000, 0xFF, SD_ANSWER_R3_EXPECTED);
        SD_IO_CSState(1); SD_IO_WriteByte(SD_DUMMY_BYTE);
        if(response.r1 != SD_R1_NO_ERROR) return BSP_SD_ERROR;
        flag_SDHC = (response.r2 & 0x40) >> 6;
    } else {
        return BSP_SD_ERROR;
    }
    return MSD_OK;
}

static uint8_t SD_ReadData(void) {
    uint8_t timeout = 0x08;
    uint8_t readvalue;
    do {
        readvalue = SD_IO_WriteByte(SD_DUMMY_BYTE);
        timeout--;
    } while ((readvalue == SD_DUMMY_BYTE) && timeout);
    return readvalue;
}

static uint8_t SD_WaitData(uint8_t data) {
    uint16_t timeout = 0xFFFF;
    uint8_t readvalue;
    do {
        readvalue = SD_IO_WriteByte(SD_DUMMY_BYTE);
        timeout--;
    } while ((readvalue != data) && timeout);
    if (timeout == 0) return BSP_SD_TIMEOUT;
    return MSD_OK;
}
