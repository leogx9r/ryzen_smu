/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (C) 2020 Leonardo Gates <leogatesx9r@protonmail.com> */
/* Ryzen SMU Root Complex Communication */

#ifndef __SMU_H__
#define __SMU_H__

#include <linux/pci.h>
#include <linux/printk.h>

/* Redefine output format for nicer formatting. */
#ifdef pr_fmt
    #undef pr_fmt
#endif
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

/**
 * Controls the processor via the SMU (System Management Unit).
 * Allows users to set or retrieve various configurations and
 *  limitations of the processor.
 */

/* Maximum size in bytes, of the PM table for Matisse, Renoir, Picasso & RavenRidge 2 */
#define PM_TABLE_MAX_SIZE                             0x88C

/* Specifies the range on how often, in milliseconds, to command the SMU to update the PM table. */
#define PM_TABLE_MAX_UPDATE_TIME_MS                   60000
#define PM_TABLE_MIN_UPDATE_TIME_MS                   50

/* Specifies the amount of attempts an of polling the SMU for a command response till it fails. */
#define SMU_RETRIES_MAX                               32768
#define SMU_RETRIES_MIN                               500

/* PCI Query Registers. [0x60,0x64] & [0xB4, 0xB8] also work. */
#define SMU_PCI_ADDR_REG                              0xC4
#define SMU_PCI_DATA_REG                              0xC8

/**
 * Return values that can be sent from the SMU in response to a command.
 */
enum smu_return_val {
    SMU_Return_OK                = 0x01,
    SMU_Return_Failed            = 0xFF,
    SMU_Return_UnknownCmd        = 0xFE,
    SMU_Return_CmdRejectedPrereq = 0xFD,
    SMU_Return_CmdRejectedBusy   = 0xFC,

    // Custom Error Code -- Does not exist in SMU.

    // SMU Management failed to respond within the SMU_TIMEOUT_MS range.
    SMU_Return_CommandTimeout    = 0xFB,
    // An invalid argument was sent to the function.
    SMU_Return_InvalidArgument   = 0xFA,
    // Function is unsupported on the current processor.
    SMU_Return_Unsupported       = 0xF9,
    // Insufficient buffer size specified.
    SMU_Return_InsufficientSize  = 0xF8,
    // Failed to map physical address.
    SMU_Return_MappedError       = 0xF7,
};

/**
 * Supported processor codenames with SMU capabilities.
 */
enum smu_processor_codename {
    CODENAME_UNDEFINED,
    CODENAME_COLFAX,
    CODENAME_RENOIR,
    CODENAME_PICASSO,
    CODENAME_MATISSE,
    CODENAME_THREADRIPPER,
    CODENAME_CASTLEPEAK,
    CODENAME_RAVENRIDGE,
    CODENAME_RAVENRIDGE2,
    CODENAME_SUMMITRIDGE,
    CODENAME_PINNACLERIDGE,

    // Not yet supported but still added for now.
    CODENAME_REMBRANT,
    CODENAME_VERMEER,
    CODENAME_VANGOGH,
    CODENAME_CEZANNE,
    CODENAME_MILAN,

    CODENAME_COUNT
};

/**
 * SMU MP1 Interface Version [v9-v13]
 */
enum smu_if_version {
    IF_VERSION_9,
    IF_VERSION_10,
    IF_VERSION_11,
    IF_VERSION_12,
    IF_VERSION_13,

    IF_VERSION_COUNT
};

/**
 * SMU Mailbox Target
 */
enum smu_mailbox {
    MAILBOX_TYPE_RSMU,
    MAILBOX_TYPE_MP1,

    MAILBOX_TYPE_COUNT
};

/* Parameters for SMU execution. */
extern uint smu_pm_use_timer;
extern uint smu_pm_update_ms;
extern uint smu_timeout_attempts;

/**
 * Initializes for SMU use. MUST be called before using any function.
 *
 * Returns 0 on success, anything else on failure.
 */
int smu_init(struct pci_dev* dev);

/**
 * Cleans up the allocated objects after use.
 */
void smu_cleanup(void);

/**
 * Returns the running processor's detected code name.
 */
enum smu_processor_codename smu_get_codename(void);

/**
 * Reads or writes 32 bit words to the SMU.
 */
u32 smu_read_address(struct pci_dev* dev, u32 address);
void smu_write_address(struct pci_dev* dev, u32 address, u32 value);

/**
 * Performs an SMU request with up to 6 arguments specified in the args array.
 * Results are returned in the args array if the request succeeds with, up to
 *  n_args being read back.
 *
 * Returns an smu_return_val indicating the status of the operation.
 */
enum smu_return_val smu_send_command(struct pci_dev* dev, u32 op, u32* args, u32 n_args,
    enum smu_mailbox mailbox);

/**
 * Returns the current SMU firmware version from the specified mailbox.
 */
u32 smu_get_version(struct pci_dev* dev, enum smu_mailbox mb);

/**
 * Returns the interface version of the MP1 mailbox.
 */
enum smu_if_version smu_get_mp1_if_version(void);

/**
 * Commands the SMU to update the PM table mapped at the DRAM base address.
 *
 * Returns an smu_return_val indicating the status of the operation.
 */
enum smu_return_val smu_transfer_table_to_dram(struct pci_dev* dev);

/**
 * For Matisse and Renoir processors, returns a numeric value indicating the format
 *  of the PM table.
 *
 * Returns an smu_return_val indicating the status of the operation.
 */
enum smu_return_val smu_get_pm_table_version(struct pci_dev* dev, u32* version);

/**
 * Reads the PM table for the current CPU, if supported, into the destination buffer.
 *
 * Returns an smu_return_val indicating the status of the operation.
 */
enum smu_return_val smu_read_pm_table(struct pci_dev* dev, unsigned char* dst, size_t* len);

#endif /* __SMU_H__ */
