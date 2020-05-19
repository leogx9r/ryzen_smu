/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (C) 2020 Leonardo Gates <leogatesx9r@protonmail.com> */
/* Ryzen SMU Root Complex Communication */

#include <linux/module.h>
#include <linux/delay.h>
#include <linux/time.h>
#include <linux/pci.h>
#include <asm/io.h>

#include "smu.h"

static struct {
    enum smu_processor_codename    codename;

    u32                            addr_mb_cmd;
    u32                            addr_mb_rsp;
    u32                            addr_mb_args;

    u64                            pm_dram_base;
    u32                            pm_dram_base_alt;
    u32                            pm_dram_map_size;
    u32                            pm_dram_map_size_alt;
    u64                            pm_last_probe_ns;

    u8*                            pm_table_virt_addr;
    u8*                            pm_table_virt_addr_alt;
} g_smu = {
    .codename                    = CODENAME_UNDEFINED,

    .addr_mb_cmd                 = 0,
    .addr_mb_rsp                 = 0,
    .addr_mb_args                = 0,

    .pm_dram_base                = 0,
    .pm_dram_base_alt            = 0,
    .pm_dram_map_size            = 0,
    .pm_dram_map_size_alt        = 0,
    .pm_last_probe_ns            = 0,

    .pm_table_virt_addr          = NULL,
    .pm_table_virt_addr_alt      = NULL,
};

static DEFINE_MUTEX(amd_pci_mutex);
static DEFINE_MUTEX(amd_smu_mutex);

u32 smu_read_address(struct pci_dev* dev, u32 address) {
    u32 ret;

    mutex_lock(&amd_pci_mutex);
    pci_write_config_dword(dev, SMU_PCI_ADDR_REG, address);
    pci_read_config_dword(dev, SMU_PCI_DATA_REG, &ret);
    mutex_unlock(&amd_pci_mutex);

    return ret;
}

void smu_write_address(struct pci_dev* dev, u32 address, u32 value) {
    mutex_lock(&amd_pci_mutex);
    pci_write_config_dword(dev, SMU_PCI_ADDR_REG, address);
    pci_write_config_dword(dev, SMU_PCI_DATA_REG, value);
    mutex_unlock(&amd_pci_mutex);
}

enum smu_return_val smu_send_command(struct pci_dev* dev, u32 op, u32* args, u32 n_args) {
    u32 retries, tmp, i;

    mutex_lock(&amd_smu_mutex);

    // Step 1: Wait until the RSP register is non-zero.
    tmp = 0; retries = smu_timeout_attempts;
    while (tmp == 0 && retries--)
        tmp = smu_read_address(dev, g_smu.addr_mb_rsp);

    // Step 1.b: A command is still being processed meaning
    //  a new command cannot be issued.
    if (!retries && !tmp) {
        mutex_unlock(&amd_smu_mutex);
        return SMU_Return_CommandTimeout;
    }

    // Step 2: Write zero (0) to the RSP register
    smu_write_address(dev, g_smu.addr_mb_rsp, 0);

    // Step 3: Write the argument(s) into the argument register(s)
    for (i = 0; i < 6; i++) {
        tmp = i >= n_args ? 0 : args[i];
        smu_write_address(dev, g_smu.addr_mb_args + (i * 4), tmp);
    }

    // Step 4: Write the message Id into the Message ID register
    smu_write_address(dev, g_smu.addr_mb_cmd, op);

    // Step 5: Wait until the Response register is non-zero.
    tmp = 0;
    while(tmp == 0 && retries--)
        tmp = smu_read_address(dev, g_smu.addr_mb_rsp);

    // Step 6: If the Response register contains OK, then SMU has finished processing
    //  the message.
    if (tmp != SMU_Return_OK && !retries) {
        mutex_unlock(&amd_smu_mutex);
        return tmp == 0 ? SMU_Return_CommandTimeout : tmp;
    }

    // Step 7: If a return argument is expected, the Argument register may be read
    //  at this time.
    for (i = 0; i < n_args; i++)
        args[i] = smu_read_address(dev, g_smu.addr_mb_args + (i * 4));

    mutex_unlock(&amd_smu_mutex);

    return SMU_Return_OK;
}

int smu_resolve_cpu_class(struct pci_dev* dev) {
    u32 e_model, pkg_type;

    // Combines BaseModel + ExtModel + ExtFamily + Reserved
    // See: CPUID_Fn00000001_EAX
    e_model = cpuid_eax(0x00000001);
    e_model = ((e_model & 0xff) >> 4) + ((e_model >> 12) & 0xf0);

    // Combines "PkgType" and "Reserved"
    // See: CPUID_Fn80000001_EBX
    pkg_type = cpuid_ebx(0x80000001) >> 28;

    // The following is a direct recreating of what Ryzen Master
    //  uses to determine each processor model.
    // In the case where multiple CPUs of the same family releases,
    //  this should technically allow us to detect all future models.
    if (e_model <= 0xF && pkg_type == 2) {
        if (e_model == 1)
            g_smu.codename = CODENAME_SUMMITRIDGE;
        else {
            if (e_model != 8) {
                pr_err("cpuid: failed to detect processor codename (1)");
                return -1;
            }

            g_smu.codename = CODENAME_PINNACLERIDGE;
        }
    }
    else if (e_model - 0x70 <= 0xF && pkg_type == 2)
        g_smu.codename = CODENAME_MATISSE;
    else if (e_model - 48 <= 0xF && pkg_type == 7)
        g_smu.codename = CODENAME_CASTLEPEAK;
    else if (e_model <= 0xF && pkg_type == 7) {
        if (e_model == 8)
            g_smu.codename = CODENAME_COLFAX;
        else
            g_smu.codename = CODENAME_THREADRIPPER;
    }
    else if (e_model - 16 > 0x1f) {
        if (e_model - 96 > 0xf) {
            pr_err("cpuid: failed to detect processor codename (2)");
            return -2;
        }

        g_smu.codename = CODENAME_RENOIR;
    }
    else if (e_model & 0xFFFFFFE0 || e_model == 24) {
        if (((smu_read_address(dev, 0x5D5C0) >> 30) - 1) & 0xfffffffd || pkg_type != 2) {
            if (e_model != 24) {
                pr_err("cpuid: failed to detect processor codename (3)");
                return -3;
            }

            g_smu.codename = CODENAME_PICASSO;
        }
        else
            g_smu.codename = CODENAME_RAVENRIDGE2;
    }
    else if (e_model <= 0x1F && pkg_type == 2)
        g_smu.codename = CODENAME_RAVENRIDGE;
    else {
        pr_err("cpuid: failed to detect processor codename (4)");
        return -4;
    }

    return 0;
}

int smu_init(struct pci_dev* dev) {
    if (g_smu.codename != CODENAME_UNDEFINED)
        return 0;

    if (smu_resolve_cpu_class(dev))
        return -ENODEV;

    switch (g_smu.codename) {
        case CODENAME_CASTLEPEAK:
        case CODENAME_MATISSE:
            g_smu.addr_mb_cmd  = 0x3B10524;
            g_smu.addr_mb_rsp  = 0x3B10570;
            g_smu.addr_mb_args = 0x3B10A40;
            pr_debug("SMU mailbox 1 selected for use");
            break;
        case CODENAME_COLFAX:
        case CODENAME_SUMMITRIDGE:
        case CODENAME_THREADRIPPER:
        case CODENAME_PINNACLERIDGE:
            g_smu.addr_mb_cmd  = 0x3B1051C;
            g_smu.addr_mb_rsp  = 0x3B10568;
            g_smu.addr_mb_args = 0x3B10590;
            pr_debug("SMU mailbox 2 selected for use");
            break;
        case CODENAME_RENOIR:
        case CODENAME_PICASSO:
        case CODENAME_RAVENRIDGE:
        case CODENAME_RAVENRIDGE2:
            g_smu.addr_mb_cmd  = 0x3B10A20;
            g_smu.addr_mb_rsp  = 0x3B10A80;
            g_smu.addr_mb_args = 0x3B10A88;
            pr_debug("SMU mailbox 3 selected for use");
            break;
        default:
            return -ENODEV;
    }

    return 0;
}

void smu_cleanup(void) {
    // Unmap DRAM Base if required after SMU use
    if (g_smu.pm_table_virt_addr)
        iounmap(g_smu.pm_table_virt_addr);

    if (g_smu.pm_table_virt_addr_alt)
        iounmap(g_smu.pm_table_virt_addr_alt);
}

enum smu_processor_codename smu_get_codename(void) {
    return g_smu.codename;
}

u32 smu_get_version(struct pci_dev* dev) {
    u32 args[6] = { 1, 0, 0, 0, 0, 0 }, ret;

    // OP 0x02 is consistent with all platforms meaning
    //  it can be used directly.
    ret = smu_send_command(dev, 0x02, args, 1);
    if (ret != SMU_Return_OK)
        return ret;

    return args[0];
}

u64 smu_get_dram_base_address(struct pci_dev* dev) {
    u32 args[6] = { 0, 0, 0, 0, 0, 0 }, fn[3], ret, parts[2];

    switch (g_smu.codename) {
        case CODENAME_MATISSE:
        case CODENAME_CASTLEPEAK:
            fn[0] = 0x06;
            goto BASE_ADDR_CLASS_1;
        case CODENAME_RENOIR:
            fn[0] = 0x65;
            fn[1] = 0x66;
            goto BASE_ADDR_CLASS_1;
        case CODENAME_COLFAX:
        case CODENAME_PINNACLERIDGE:
            fn[0] = 0x0b;
            fn[1] = 0x0c;
            goto BASE_ADDR_CLASS_2;
        case CODENAME_PICASSO:
        case CODENAME_RAVENRIDGE:
        case CODENAME_RAVENRIDGE2:
            fn[0] = 0x0a;
            fn[1] = 0x3d;
            fn[2] = 0x0b;
            goto BASE_ADDR_CLASS_3;
        default:
            return SMU_Return_Unsupported;
    }

BASE_ADDR_CLASS_1:
    args[0] = args[1] = 1;
    ret = smu_send_command(dev, fn[0], args, 2);

    return ret != SMU_Return_OK ? ret : args[0] | ((u64)args[1] << 32);

BASE_ADDR_CLASS_2:
    ret = smu_send_command(dev, fn[0], args, 1);
    if (ret != SMU_Return_OK)
        return ret;

    ret = smu_send_command(dev, fn[1], args, 1);

    return ret != SMU_Return_OK ? ret : args[0];

BASE_ADDR_CLASS_3:
    args[0] = 3;
    ret = smu_send_command(dev, fn[0], args, 1);
    if (ret != SMU_Return_OK)
        return ret;

    args[0] = 3;
    ret = smu_send_command(dev, fn[2], args, 1);
    if (ret != SMU_Return_OK)
        return ret;

    // 1st Base.
    parts[0] = args[0];

    args[0] = 3;
    ret = smu_send_command(dev, fn[1], args, 1);
    if (ret != SMU_Return_OK)
        return ret;

    args[0] = 5;
    ret = smu_send_command(dev, fn[0], args, 1);
    if (ret != SMU_Return_OK)
        return ret;

    args[0] = 5;
    ret = smu_send_command(dev, fn[2], args, 1);
    if (ret != SMU_Return_OK)
        return ret;

    // 2nd base.
    parts[1] = args[0];

    return (u64)parts[1] << 32 | parts[0];
}

enum smu_return_val smu_probe_pm_table(struct pci_dev* dev) {
    u32 args[6] = { 0, 0, 0, 0, 0, 0 }, fn;

    /**
     * Probes (updates) the PM Table.
     * Physically mapped at the DRAM Base address(es).
     */

    switch (g_smu.codename) {
        case CODENAME_MATISSE:
            fn = 0x05;
            break;
        case CODENAME_RENOIR:
            args[0] = 3;
            fn = 0x65;
            break;
        case CODENAME_PICASSO:
        case CODENAME_RAVENRIDGE2:
            args[0] = 3;
            fn = 0x3d;
            break;
        default:
            return SMU_Return_Unsupported;
    }

    return smu_send_command(dev, fn, args, 1);
}

enum smu_return_val smu_get_pm_table_type(struct pci_dev* dev, u32* type) {
    u32 fn;

    /**
     * For Matisse & Renoir, there are different PM tables for each chip.
     * Presumably this is based on core count.
     */
    switch (g_smu.codename) {
        case CODENAME_MATISSE:
            fn = 0x08;
            break;
        case CODENAME_RENOIR:
            fn = 0x06;
            break;
        default:
            return SMU_Return_Unsupported;
    }

    *type = 0;
    return smu_send_command(dev, fn, type, 1);
}

enum smu_return_val smu_read_pm_table(struct pci_dev* dev, unsigned char* dst, size_t* len) {
    u32 ret, type, size;
    u64 tm;

    // The DRAM base does not change across boots meaning it only needs to be
    //  fetched once.
    if (g_smu.pm_dram_base == 0 || g_smu.pm_dram_map_size == 0) {
        g_smu.pm_dram_base = smu_get_dram_base_address(dev);

        if (g_smu.pm_dram_base < 0xFF && g_smu.pm_dram_base >= 0) {
            pr_err("Unable to receive the DRAM base address (%d)", (u8)g_smu.pm_dram_base);
            return g_smu.pm_dram_base;
        }

        // Each model has different types and sizes.
        if (g_smu.codename == CODENAME_MATISSE || g_smu.codename == CODENAME_RENOIR) {
            ret = smu_get_pm_table_type(dev, &type);

            if (ret != SMU_Return_OK) {
                pr_err("Failed to get PM Table type, returned %d\n", ret);
                return ret;
            }
        }
        switch (g_smu.codename) {
            case CODENAME_MATISSE:
                switch (type) {
                    case 0x240902:
                        g_smu.pm_dram_map_size = 0x514;
                        break;
                    case 0x240903:
                        g_smu.pm_dram_map_size = 0x518;
                        break;
                    case 0x240802:
                        g_smu.pm_dram_map_size = 0x7E0;
                        break;
                    case 0x240803:
                        g_smu.pm_dram_map_size = 0x7E4;
                        break;
                    default:
                    UNKNOWN_PM_TABLE_TYPE:
                        pr_err("Unknown PM table type: 0x%08X", type);
                        return SMU_Return_Unsupported;
                }
                break;
            case CODENAME_RENOIR:
                switch (type) {
                    case 0x370000:
                        g_smu.pm_dram_map_size = 0x794;
                        break;
                    case 0x370001:
                        g_smu.pm_dram_map_size = 0x884;
                        break;
                    case 0x370002:
                        g_smu.pm_dram_map_size = 0x88C;
                        break;
                    default:
                        goto UNKNOWN_PM_TABLE_TYPE;
                }
                break;
            case CODENAME_PICASSO:
            case CODENAME_RAVENRIDGE2:
                g_smu.pm_dram_map_size_alt = 0xA4;
                g_smu.pm_dram_map_size = 0x608 + g_smu.pm_dram_map_size_alt;

                // Split DRAM base into high/low values.
                g_smu.pm_dram_base_alt = g_smu.pm_dram_base >> 32;
                g_smu.pm_dram_base &= 0xFFFFFFFF;
                break;
            default:
                return SMU_Return_Unsupported;
        }
    }

    // Validate output buffer size
    // N.B. In the case of Picasso/RavenRidge 2, we include the secondary PM Table size as well
    if (*len < g_smu.pm_dram_map_size) {
        pr_warn("Insufficient buffer size for PM table read: %lu < %d", *len, g_smu.pm_dram_map_size);

        *len = g_smu.pm_dram_map_size;
        return SMU_Return_InsufficientSize;
    }

    // Clamp output size
    *len = g_smu.pm_dram_map_size;

    // Check if we should tell the SMU to refresh the table with nanosecond precision
    tm = ktime_get_ns();
    if ((tm - g_smu.pm_last_probe_ns) > (1000000 * smu_pm_update_ms)) {
        ret = smu_probe_pm_table(dev);
        if (ret != SMU_Return_OK)
            return ret;

        g_smu.pm_last_probe_ns = tm;
    }

    // Primary PM Table size
    size = g_smu.pm_dram_map_size - g_smu.pm_dram_map_size_alt;

    // We only map the DRAM base(s) once for use.
    if (g_smu.pm_table_virt_addr == NULL) {
        g_smu.pm_table_virt_addr = ioremap(g_smu.pm_dram_base, size);

        if (g_smu.pm_table_virt_addr == NULL) {
            pr_err("Failed to map DRAM base: %llX (0x%X B)", g_smu.pm_dram_base, size);
            return SMU_Return_MappedError;
        }

        // In Picasso/RavenRidge 2, we map the secondary (high) address as well.
        if (g_smu.pm_dram_map_size_alt) {
            g_smu.pm_table_virt_addr_alt = ioremap(g_smu.pm_dram_base_alt, g_smu.pm_dram_map_size_alt);

            if (g_smu.pm_table_virt_addr_alt == NULL) {
                pr_err("Failed to map DRAM alt base: %X (0x%X B)", g_smu.pm_dram_base_alt, g_smu.pm_dram_map_size_alt);
                return SMU_Return_MappedError;
            }
        }
    }

    memcpy(dst, g_smu.pm_table_virt_addr, size);

    // Append secondary table if required.
    if (g_smu.pm_dram_map_size_alt)
        memcpy(dst + size, g_smu.pm_table_virt_addr_alt, g_smu.pm_dram_map_size_alt);

    return SMU_Return_OK;
}
