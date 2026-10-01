#include "sd_card_init.h"

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "FS.h"
#include "cybsp.h"
#include "cy_gpio.h"
#include "cy_sd_host.h"
#include "cy_sysint.h"
#include "cycfg_peripherals.h"
#include "mtb_hal.h"
#include "message_log.h"

#define SDHC_IRQ_PRIORITY (3U)

static mtb_hal_sdhc_t *sd_card_hal_object;
static cy_stc_sd_host_context_t sd_card_context;
static bool sd_card_hw_ready;
static bool sd_card_filesystem_is_ready;

static const char *sd_card_type_name(cy_en_sd_host_card_type_t card_type);
static const char *sd_card_capacity_name(cy_en_sd_host_card_capacity_t capacity);
static const char *sd_card_file_system_name(uint16_t file_system_type);

bool sd_card_filesystem_ready(void)
{
    return sd_card_filesystem_is_ready;
}

bool sd_card_get_status(message_sd_card_status_t *status)
{
    FS_DISK_INFO disk_info = {0};
    uint32_t block_count = 0U;

    if (status == NULL)
    {
        return false;
    }
    memset(status, 0, sizeof(*status));
    status->inserted = Cy_SD_Host_IsCardConnected(CYBSP_SDHC_1_HW) ? 1U : 0U;
    status->initialized = sd_card_hw_ready ? 1U : 0U;
    if (!status->inserted || !status->initialized)
    {
        return true;
    }

    (void)snprintf(status->card_type,
                   sizeof(status->card_type),
                   "%s",
                   sd_card_type_name(sd_card_context.cardType));
    (void)snprintf(status->card_capacity,
                   sizeof(status->card_capacity),
                   "%s",
                   sd_card_capacity_name(sd_card_context.cardCapacity));
    if ((CY_SD_HOST_SD == sd_card_context.cardType) ||
        (CY_SD_HOST_EMMC == sd_card_context.cardType))
    {
        if (CY_SD_HOST_SUCCESS != Cy_SD_Host_GetBlockCount(CYBSP_SDHC_1_HW,
                                                            &block_count,
                                                            &sd_card_context))
        {
            return false;
        }
    }
    else
    {
        block_count = sd_card_context.maxSectorNum;
    }
    status->total_mib = block_count / 2048U;

    if (!sd_card_filesystem_is_ready ||
        (0 != FS_GetVolumeInfo("", &disk_info)))
    {
        return true;
    }
    (void)snprintf(status->filesystem,
                   sizeof(status->filesystem),
                   "%s",
                   sd_card_file_system_name(disk_info.FSType));
    status->free_kib = FS_GetVolumeFreeSpaceKB("");
    return true;
}

bool Cy_SD_Host_IsCardConnected(SDHC_Type const *base)
{
    CY_UNUSED_PARAMETER(base);

    return (0U == Cy_GPIO_Read(CYBSP_SDHC_DETECT_PORT,
                               CYBSP_SDHC_DETECT_PIN));
}

static const char *sd_card_type_name(cy_en_sd_host_card_type_t card_type)
{
    switch (card_type)
    {
        case CY_SD_HOST_SD:
            return "SD memory";
        case CY_SD_HOST_SDIO:
            return "SDIO";
        case CY_SD_HOST_COMBO:
            return "SD memory + SDIO";
        case CY_SD_HOST_EMMC:
            return "eMMC";
        case CY_SD_HOST_UNUSABLE:
            return "unsupported";
        default:
            return "unknown";
    }
}

static const char *sd_card_capacity_name(cy_en_sd_host_card_capacity_t capacity)
{
    switch (capacity)
    {
        case CY_SD_HOST_SDSC:
            return "SDSC";
        case CY_SD_HOST_SDHC:
            return "SDHC";
        case CY_SD_HOST_SDXC:
            return "SDXC";
        default:
            return "unknown capacity";
    }
}

static const char *sd_card_file_system_name(uint16_t file_system_type)
{
    switch (file_system_type)
    {
        case FS_TYPE_FAT12:
            return "FAT12";
        case FS_TYPE_FAT16:
            return "FAT16";
        case FS_TYPE_FAT32:
            return "FAT32";
        case FS_TYPE_EFS:
            return "EFS";
        default:
            return "unknown";
    }
}

static void sd_card_isr(void)
{
    (void)mtb_hal_sdhc_process_interrupt(sd_card_hal_object);
}

void FS_MMC_HW_CM_ConfigureHw(mtb_hal_sdhc_t *sdhc_object)
{
    cy_en_sd_host_status_t host_status;
    cy_en_sysint_status_t interrupt_status;
    cy_stc_sysint_t const interrupt_config =
    {
        .intrSrc = CYBSP_SDHC_1_IRQ,
        .intrPriority = SDHC_IRQ_PRIORITY,
    };

    sd_card_hw_ready = false;
    sd_card_hal_object = sdhc_object;

    Cy_SD_Host_Enable(CYBSP_SDHC_1_HW);

    host_status = Cy_SD_Host_Init(CYBSP_SDHC_1_HW,
                                  &CYBSP_SDHC_1_config,
                                  &sd_card_context);
    if (CY_SD_HOST_SUCCESS != host_status)
    {
        (void)message_log_printf(MESSAGE_LOG_SUBSYSTEM_SD_CARD,
                     "SDHC1 host initialization failed: %d\r\n",
                     (int)host_status);
        return;
    }

    host_status = Cy_SD_Host_InitCard(CYBSP_SDHC_1_HW,
                                      &CYBSP_SDHC_1_card_cfg,
                                      &sd_card_context);
    if (CY_SD_HOST_SUCCESS != host_status)
    {
        (void)message_log_printf(MESSAGE_LOG_SUBSYSTEM_SD_CARD,
                     "SD card initialization failed: %d\r\n",
                     (int)host_status);
        return;
    }

    if (CY_RSLT_SUCCESS != mtb_hal_sdhc_setup(sdhc_object,
                                               &CYBSP_SDHC_1_sdhc_hal_config,
                                               NULL,
                                               &sd_card_context))
    {
        (void)message_log_printf(MESSAGE_LOG_SUBSYSTEM_SD_CARD,
                     "SDHC HAL setup failed\r\n");
        return;
    }

    interrupt_status = Cy_SysInt_Init(&interrupt_config, sd_card_isr);
    if (CY_SYSINT_SUCCESS != interrupt_status)
    {
        (void)message_log_printf(MESSAGE_LOG_SUBSYSTEM_SD_CARD,
                     "SDHC interrupt initialization failed: %d\r\n",
                     (int)interrupt_status);
        return;
    }

    NVIC_EnableIRQ((IRQn_Type)interrupt_config.intrSrc);
    sd_card_hw_ready = true;
}

void sd_card_init_and_report(void)
{
    if (!Cy_SD_Host_IsCardConnected(CYBSP_SDHC_1_HW))
    {
        (void)message_log_printf(MESSAGE_LOG_SUBSYSTEM_SD_CARD,
                     "No SD card detected in the SDHC1 slot\r\n");
        return;
    }

    mtb_hal_syspm_lock_deepsleep();
    FS_Init();
    sd_card_filesystem_is_ready = true;

    if (!sd_card_hw_ready)
    {
        mtb_hal_syspm_unlock_deepsleep();
        return;
    }

    (void)message_log_printf(MESSAGE_LOG_SUBSYSTEM_SD_CARD,
                             "SD card detected: %s, %s, RCA 0x%04lX\r\n",
                             sd_card_type_name(sd_card_context.cardType),
                             sd_card_capacity_name(sd_card_context.cardCapacity),
                             (unsigned long)sd_card_context.RCA);

    if ((CY_SD_HOST_SDIO == sd_card_context.cardType) ||
        (CY_SD_HOST_UNUSABLE == sd_card_context.cardType))
    {
        (void)message_log_printf(MESSAGE_LOG_SUBSYSTEM_SD_CARD,
                     "Card does not expose usable memory capacity\r\n");
        mtb_hal_syspm_unlock_deepsleep();
        return;
    }

    uint32_t block_count = 0U;
    cy_en_sd_host_status_t status = CY_SD_HOST_SUCCESS;
    if ((CY_SD_HOST_SD == sd_card_context.cardType) ||
        (CY_SD_HOST_EMMC == sd_card_context.cardType))
    {
        status = Cy_SD_Host_GetBlockCount(CYBSP_SDHC_1_HW,
                                          &block_count,
                                          &sd_card_context);
    }
    else
    {
        block_count = sd_card_context.maxSectorNum;
    }

    if ((CY_SD_HOST_SUCCESS != status) || (block_count == 0U))
    {
        (void)message_log_printf(MESSAGE_LOG_SUBSYSTEM_SD_CARD,
                     "Unable to determine SD card size: %d\r\n",
                     (int)status);
        mtb_hal_syspm_unlock_deepsleep();
        return;
    }

    (void)message_log_printf(MESSAGE_LOG_SUBSYSTEM_SD_CARD,
                             "SD card size: %lu sectors, %lu MiB\r\n",
                             (unsigned long)block_count,
                             (unsigned long)(block_count / 2048U));

    FS_DISK_INFO disk_info = { 0 };
    int file_system_status = FS_GetVolumeInfo("", &disk_info);
    if (0 == file_system_status)
    {
        (void)message_log_printf(MESSAGE_LOG_SUBSYSTEM_SD_CARD,
                     "SD card filesystem: %s\r\n",
                     sd_card_file_system_name(disk_info.FSType));
    }
    else
    {
        (void)message_log_printf(MESSAGE_LOG_SUBSYSTEM_SD_CARD,
                     "SD card filesystem: not recognized by emFile (%d)\r\n",
                     file_system_status);
    }

    mtb_hal_syspm_unlock_deepsleep();
}