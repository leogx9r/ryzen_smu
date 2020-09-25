/**
 * Ryzen SMU Userspace Sensor Monitor
 * Copyright (C) 2020 Leonardo Gates <leogatesx9r@protonmail.com>
 *
 * This program is free software: you can redistribute it &&/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 **/

#define _GNU_SOURCE

#include <math.h>
#include <sched.h>
#include <fcntl.h>
#include <cpuid.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <libsmu.h>

#define PROGRAM_VERSION                 "1.0"
#define PM_TABLE_SUPPORTED_VERSION      0x240903

#define AMD_MSR_PWR_UNIT                0xC0010299
#define AMD_MSR_CORE_ENERGY             0xC001029A
#define AMD_MSR_PACKAGE_ENERGY          0xC001029B
#define AMD_TIME_UNIT_MASK              0xF0000
#define AMD_ENERGY_UNIT_MASK            0x1F00
#define AMD_POWER_UNIT_MASK             0xF

#define MAX_CORES                       32

#define READ_SMN_V1(offs)                       { if (smu_read_smn_addr(&obj, offs + offset, &value1) != SMU_Return_OK) goto _READ_ERROR; }
#define READ_SMN_V2(offs)                       { if (smu_read_smn_addr(&obj, offs + offset, &value2) != SMU_Return_OK) goto _READ_ERROR; }

typedef struct {
    int               init;

    int               msr[MAX_CORES];
    float             core_power[MAX_CORES];
    float             package_power;
    float             energy_unit_delta;
} rapl_state_t;

// Ryzen 3700X/3800X
typedef struct {
    float            ppt_limit;          // 0x0000 -- Power [W]
    float            ppt_used;           // 0x0004 -- Power [W]
    float            tdc_limit;          // 0x0008 -- Current [A]
    float            tdc_used;           // 0x000C -- Current [A]
    float            thermal_junction;   // 0x0010 -- Degrees [C]
    float            current_temp;       // 0x0014 -- Degrees [C]
    float            pad_0018[2];        // 0x0018
    float            edc_limit;          // 0x0020 -- Current [A]
    float            edc_used;           // 0x0024 -- Current [A]
    float            pad_0028;           // 0x0028
    float            svi_core;           // 0x002C -- Voltage [V]
    float            pad_0030;           // 0x0030
    float            ppt_used_alt;       // 0x0034 -- Power [W]
    float            pad_0038[10];       // 0x0038
    float            core_power;         // 0x0060 -- Power [W]
    float            soc_power;          // 0x0064 -- Power [W]
    float            pad_0068[14];       // 0x0068
    float            vddcr_vdd;          // 0x00A0 -- Voltage [V]
    float            pad_00A4[3];        // 0x00A4
    float            vddcr_soc;          // 0x00B0 -- Voltage [V]
    float            svi_soc;            // 0x00B4 -- Voltage [V]
    float            svi_soc_current;    // 0x00B8 -- Current [A]
    float            pad_00BC;           // 0x00BC
    float            if_limit;           // 0x00C0 -- Frequency [MHz]
    float            if_frequency;       // 0x00C4 -- Frequency [MHz]
    float            pad_00C8[24];       // 0x00C8
    float            uncore_frequency;   // 0x0128 -- Frequency [MHz]
    float            pad_012C[3];        // 0x012C
    float            mclk_frequency;     // 0x0138 -- Frequency [MHz]
    float            pad_013C[46];       // 0x013C
    float            cldo_vddp;          // 0x01F4 -- Voltage [V]
    float            cldo_vddg;          // 0x01F8 -- Voltage [V]
    float            pad_01FC[8];        // 0x01FC
    float            gated_time;         // 0x021C
    float            pad_0220[11];       // 0x0220
    float            ncore_power[8];     // 0x024C -- Power [W]
    float            pad_026C[32];       // 0x026C
    float            ncore_frequency[8]; // 0x02EC -- Frequency [MHz]
    float            ncore_real_freq[8]; // 0x030C -- Frequency [MHz]
    float            ncore_usage[8];     // 0x032C
    float            pad_034C[8];        // 0x034C
    float            ncore_sleep[8];     // 0x036C
    float            pad_038C[99];       // 0x038C
} __attribute__((__packed__)) pm_table_0x240903, *ppm_table_0x240903;

static smu_obj_t obj;
static rapl_state_t rapl = { 0, };
static int use_rapl = 0;
static int update_time_s = 1;

int open_msr(int core, int* fp) {
    char msr_path[512];

	sprintf(msr_path, "/dev/cpu/%d/msr", core);
	*fp = open(msr_path, O_RDONLY);

    if (!*fp) {
        perror(__func__);
        return 0xc0de;
    }

    return 0;
}

static long long read_msr(int fd, unsigned int which) {
	__uint64_t data;

	if (pread(fd, &data, sizeof data, which) != sizeof data) {
		perror(__func__);
		exit(0xdead);
	}

	return (long long)data;
}

void print_memory_timings() {
    const char* bool_str[2] = { "Disabled", "Enabled" };
    unsigned int value1, value2, offset;

    READ_SMN_V1(0x50200);
    offset = value1 == 0x300 ? 0x100000 : 0;

    READ_SMN_V1(0x50050); READ_SMN_V2(0x50058);
    fprintf(stdout, "BankGroupSwap: %s\n",
        bool_str[!(value1 == value2 && value1 == 0x87654321)]);

    READ_SMN_V1(0x500D0); READ_SMN_V2(0x500D4);
    fprintf(stdout, "BankGroupSwapAlt: %s\n",
        bool_str[(value1 >> 4 & 0x7F) != 0 || (value2 >> 4 & 0x7F) != 0]);

    READ_SMN_V1(0x50200); READ_SMN_V2(0x50204);
    fprintf(stdout, "Memory Clock: %.0f MHz\nGDM: %s\nCR: %s\nTcl: %d\nTras: %d\nTrcdrd: %d\nTrcdwr: %d\n",
        (value1 & 0x7f) / 3.f * 100.f,
        bool_str[((value1 >> 11) & 1) == 1],
        ((value1 & 0x400) >> 10) != 0 ? "2T" : "1T",
        value2 & 0x3f,
        value2 >> 8 & 0x7f,
        value2 >> 16 & 0x3f,
        value2 >> 24 & 0x3f);

    READ_SMN_V1(0x50208); READ_SMN_V2(0x5020C);
    fprintf(stdout, "Trc: %d\nTrp: %d\nTrrds: %d\nTrrdl: %d\nTrtp: %d\n",
        value1 & 0xff,
        value1 >> 16 & 0x3f,
        value2 & 0x1f,
        value2 >> 8 & 0x1f,
        value2 >> 24 & 0x1f);

    READ_SMN_V1(0x50210); READ_SMN_V2(0x50214);
    fprintf(stdout, "Tfaw: %d\nTcwl: %d\nTwtrs: %d\nTwtrl: %d\n",
        value1 & 0xff,
        value2 & 0x3f,
        value2 >> 8 & 0x1f,
        value2 >> 16 & 0x3f);

    READ_SMN_V1(0x50218); READ_SMN_V2(0x50220);
    fprintf(stdout, "Twr: %d\nTrdrddd: %d\nTrdrdsd: %d\nTrdrdsc: %d\nTrdrdscl: %d\n",
        value1 & 0xff,
        value2 & 0xf,
        value2 >> 8 & 0xf,
        value2 >> 16 & 0xf,
        value2 >> 24 & 0x3f);

    READ_SMN_V1(0x50224); READ_SMN_V2(0x50228);
    fprintf(stdout, "Twrwrdd: %d\nTwrwrsd: %d\nTwrwrsc: %d\nTwrwrscl: %d\nTwrrd: %d\nTrdwr: %d\n",
        value1 & 0xf,
        value1 >> 8 & 0xf,
        value1 >> 16 & 0xf,
        value1 >> 24 & 0x3f,
        value2 & 0xf,
        value2 >> 8 & 0x1f);

    READ_SMN_V1(0x50254);
    fprintf(stdout, "Tcke: %d\n", value1 >> 24 & 0x1f);

    READ_SMN_V1(0x50260); READ_SMN_V2(0x50264);
    if (value1 != value2 && value1 == 0x21060138)
        value1 = value2;

    fprintf(stdout, "Trfc: %d\nTrfc2: %d\nTrfc4: %d\n",
        value1 & 0x3ff,
        value1 >> 11 & 0x3ff,
        value1 >> 22 & 0x3ff);

    exit(0);

_READ_ERROR:
    fprintf(stderr, "Unable to read SMN address space.");
    exit(1);
}

void append_u32_to_str(char* buffer, unsigned int val) {
    char tmp[12] = { 0 };

    sprintf(tmp, "%c%c%c%c", val & 0xff, val >> 8 & 0xff, val >> 16 & 0xff, val >> 24 & 0xff);
    strcat(buffer, tmp);
}

const char* get_processor_name() {
    unsigned int eax, ebx, ecx, edx;
    static char buffer[50] = { 0 };

    __get_cpuid(0x80000002, &eax, &ebx, &ecx, &edx);
    append_u32_to_str(buffer, eax);
    append_u32_to_str(buffer, ebx);
    append_u32_to_str(buffer, ecx);
    append_u32_to_str(buffer, edx);

    __get_cpuid(0x80000003, &eax, &ebx, &ecx, &edx);
    append_u32_to_str(buffer, eax);
    append_u32_to_str(buffer, ebx);
    append_u32_to_str(buffer, ecx);
    append_u32_to_str(buffer, edx);

    __get_cpuid(0x80000004, &eax, &ebx, &ecx, &edx);
    append_u32_to_str(buffer, eax);
    append_u32_to_str(buffer, ebx);
    append_u32_to_str(buffer, ecx);
    append_u32_to_str(buffer, edx);

    return buffer;
}

unsigned int get_processor_cores() {
    unsigned int eax, ebx, ecx, edx,
        logicalCores, threadsPerCore;

    __get_cpuid(0x00000001, &eax, &ebx, &ecx, &edx);
    logicalCores = (ebx >> 16) & 0xFF;

    __get_cpuid(0x8000001E, &eax, &ebx, &ecx, &edx);
    threadsPerCore = ((ebx >> 8) & 0xF) + 1;

    if (!threadsPerCore)
        return logicalCores;

    return logicalCores / threadsPerCore;
}

unsigned int get_processor_ccds() {
    unsigned int ccdCount, value1, value2, value3, value4;

    ccdCount = 0;

    if (smu_read_smn_addr(&obj, 0x5D21A, &value1) != SMU_Return_OK ||
        smu_read_smn_addr(&obj, 0x5D21B, &value2) != SMU_Return_OK ||
        smu_read_smn_addr(&obj, 0x5D21C, &value3) != SMU_Return_OK) {
            perror("Failed to determine the CCD count");
            exit(-1);
    }

    value1 = (value1 >> 22) & 0xff;
    value2 = (value2 >> 30) & 0xff;
    value3 = value3 & 0x3f;

    value4 = value2 | (4 * value3);

    if ((value1 & 1) == 0 || value4 & 1)
      ccdCount = ccdCount;
    else
      ccdCount = ccdCount + 1;

    if (0x80000000 & value1 && (0x80000000 & value4) == 0)
        ccdCount = ccdCount + 1;

    if (0x40000000 & value1 && (0x40000000 & value4) == 0)
        ccdCount = ccdCount + 1;

    if (0x20000000 & value1 && (0x20000000 & value4) == 0)
        ccdCount = ccdCount + 1;

    if (0x10000000 & value1 && (0x10000000 & value4) == 0)
        ccdCount = ccdCount + 1;

    if (0x8000000 & value1 && (0x8000000 & value4) == 0)
        ccdCount = ccdCount + 1;

    if (0x4000000 & value1 && (0x4000000 & value4) == 0)
        ccdCount = ccdCount + 1;

    if (0x2000000 & value1 && (0x2000000 & value4) == 0)
        ccdCount = ccdCount + 1;

    return ccdCount;
}

void rapl_monitor(FILE* fp, int n_cores) {
    double core_energy_a[MAX_CORES], core_energy_b[MAX_CORES];
    double package_energy_a, package_energy_b;
    int i;

    if (!rapl.init) {
        for (i = 0; i < n_cores; i++) {
            if (!open_msr(i, &rapl.msr[i]))
                continue;

            fprintf(stdout, "Failed to open MSR for reading.\n");
            return;
        }

        rapl.energy_unit_delta = pow(0.5, (read_msr(rapl.msr[0], AMD_MSR_PWR_UNIT) & AMD_ENERGY_UNIT_MASK) >> 8);

        rapl.init = 1;
    }

    package_energy_a = read_msr(rapl.msr[0], AMD_MSR_PACKAGE_ENERGY) * rapl.energy_unit_delta;
    for (i = 0; i < n_cores; i++)
        core_energy_a[i] = read_msr(rapl.msr[i], AMD_MSR_CORE_ENERGY) * rapl.energy_unit_delta;

    usleep(100000);

    package_energy_b = read_msr(rapl.msr[0], AMD_MSR_PACKAGE_ENERGY) * rapl.energy_unit_delta;
    for (i = 0; i < n_cores; i++)
		core_energy_b[i] = read_msr(rapl.msr[i], AMD_MSR_CORE_ENERGY) * rapl.energy_unit_delta;

    fprintf(stdout, "Package:         %8.3f W\n", (package_energy_b - package_energy_a) * 10);
    for (i = 0; i < n_cores; i++)
        fprintf(fp, "Core %d:         %8.3f W\n", i, (core_energy_b[i] - core_energy_a[i]) * 10);
}

void start_pm_monitor(int force) {
    float total_usage, peak_core_frequency, core_voltage, core_frequency,
        total_voltage, average_voltage, core_sleep_time, edc_used;

    const char* name, *codename;
    unsigned int cores, ccds, if_ver, i;
    ppm_table_0x240903 pmt;
    unsigned char *pm_buf;

    if (!smu_pm_tables_supported(&obj)) {
        fprintf(stderr, "PM Tables are not supported on this platform.\n");
        exit(0);
    }

    if (!force && obj.pm_table_version != PM_TABLE_SUPPORTED_VERSION) {
        fprintf(stderr, "PM Table version is not currently suppported. Run with \"-pf\" flag to ignore this.\n");
        exit(0);
    }

    name     = get_processor_name();
    codename = smu_codename_to_str(&obj);
    cores    = get_processor_cores();
    ccds     = get_processor_ccds();

    pm_buf = calloc(obj.pm_table_size, sizeof(unsigned char));
    pmt = (ppm_table_0x240903)pm_buf;

    switch (obj.smu_if_version) {
        case IF_VERSION_9:
            if_ver = 9;
            break;
        case IF_VERSION_10:
            if_ver = 10;
            break;
        case IF_VERSION_11:
            if_ver = 11;
            break;
        case IF_VERSION_12:
            if_ver = 12;
            break;
        case IF_VERSION_13:
            if_ver = 13;
            break;
        default:
            if_ver = 0;
            break;
    }

    while(1) {
        if (smu_read_pm_table(&obj, pm_buf, obj.pm_table_size) != SMU_Return_OK)
            continue;

        fprintf(stdout, "\e[1;1H\e[2J");

        fprintf(stdout, "=====================  CPU INFO  =====================\n");
        fprintf(stdout, "Model: %s\nCode Name: %s\nCCD(s): %d | Core(s): %d | IF: v%d\n", name, codename, ccds, cores, if_ver);

        total_usage = total_voltage = peak_core_frequency = 0;

        average_voltage = (pmt->vddcr_vdd * (1.0 - (pmt->gated_time * 0.01))) + (0.002 * pmt->gated_time);

        for (i = 0; i < cores; i++) {
            core_frequency = pmt->ncore_real_freq[i] * 1000.f;

            if (peak_core_frequency < core_frequency)
                peak_core_frequency = core_frequency;

            total_usage += pmt->ncore_usage[i];

            // "Real core frequency" -- excluding gating
            if (pmt->ncore_frequency[i] != 0.f) {
                core_sleep_time = pmt->ncore_sleep[i] / 100.f;
                core_voltage = ((1.0 - core_sleep_time) * average_voltage) + (0.2 * core_sleep_time);
                total_voltage += core_voltage;
            }

            if (pmt->ncore_usage[i] >= 6.f)
                fprintf(stdout, "Core #%d: %4.0f MHz  @ %4.4f W @ %1.4f V ( %6.2f % )\n", i, core_frequency, pmt->ncore_power[i], core_voltage, pmt->ncore_usage[i]);
            else
                fprintf(stdout, "Core #%d: Sleeping  @ %4.4f W @ %1.4f V ( %6.2f % )\n", i, pmt->ncore_power[i], core_voltage, pmt->ncore_usage[i]);
        }


        average_voltage = total_voltage / cores;
        edc_used = pmt->edc_used * (total_usage / cores / 100);

        if (edc_used < pmt->tdc_used)
            edc_used = pmt->tdc_used;

        fprintf(stdout, "Peak Core Frequency:  %8.0f MHz\n", peak_core_frequency);
        fprintf(stdout, "Vdd Voltage:          %2.6f V\n", pmt->svi_core);
        fprintf(stdout, "Peak Core Voltage:    %2.6f V\n", pmt->vddcr_vdd);
        fprintf(stdout, "Average Voltage:      %2.6f V\n", average_voltage);

        fprintf(stdout, "======================================================\n\n");
        fprintf(stdout, "===================== PBO LIMITS =====================\n");

        fprintf(stdout, "TjMax: %8.2f °C\n", pmt->thermal_junction);
        fprintf(stdout, "Temp:  %8.2f °C\n", pmt->current_temp);
        fprintf(stdout, "Core:  %8.4f W\n", pmt->core_power);
        fprintf(stdout, "SoC:   %8.4f W / %7.4f A / %7.6f V\n", pmt->soc_power, pmt->svi_soc_current, pmt->svi_soc);
        fprintf(stdout, "PPT:   %8.2f W / %7.f W ( %6.2f % )\n", pmt->ppt_used, pmt->ppt_limit, (pmt->ppt_used / pmt->ppt_limit * 100));
        fprintf(stdout, "TDC:   %8.2f A / %7.f A ( %6.2f % )\n", pmt->tdc_used, pmt->tdc_limit, (pmt->tdc_used / pmt->tdc_limit * 100));
        fprintf(stdout, "EDC:   %8.2f A / %7.f A ( %6.2f % )\n", edc_used, pmt->edc_limit, (edc_used / pmt->edc_limit * 100));
        fprintf(stdout, "======================================================\n\n");

        fprintf(stdout, "=====================   MEMORY   =====================\n");
        fprintf(stdout, "Coupled Mode:   %s\n", pmt->uncore_frequency == pmt->mclk_frequency ? "ON" : "OFF");
        fprintf(stdout, "FCLK (Avg):   %6.f MHz\n", pmt->if_frequency);
        fprintf(stdout, "FCLK:         %6.f MHz\n", pmt->if_limit);
        fprintf(stdout, "UCLK:         %6.f MHz\n", pmt->uncore_frequency);
        fprintf(stdout, "MCLK:         %6.f MHz\n", pmt->mclk_frequency);
        fprintf(stdout, "VDDCR_SoC:    %.4f V\n", pmt->vddcr_soc);
        fprintf(stdout, "cLDO_VDDP:    %.4f V\n", pmt->cldo_vddp);
        fprintf(stdout, "cLDO_VDDG:    %.4f V\n", pmt->cldo_vddg);
        fprintf(stdout, "======================================================\n");

        if (use_rapl) {
            fprintf(stdout, "=====================    RAPL    =====================\n");
            rapl_monitor(stdout, cores);
            fprintf(stdout, "======================================================\n");
        }

        // Hide Cursor
        fprintf(stdout, "\e[?25l");

        fflush(stdout);
        sleep(update_time_s);
    }
}

void print_version() {
    fprintf(stdout, "SMU Monitor " PROGRAM_VERSION "\n");
    exit(0);
}

void show_help(char* program) {
    fprintf(stdout,
        "SMU Monitor " PROGRAM_VERSION "\n\n"

        "Usage: %s <option(s)>\n\n"

        "Options:\n"
            "\t-h - Show this help screen.\n"
            "\t-v - Show program version.\n"
            "\t-m - Print DRAM Timings and exit.\n"
            "\t-r - Enable RAPL support when monitoring.\n"
            "\t-f - Force PM table monitoring even if the PM table version is not supported.\n"
            "\t-u<seconds> - Update the monitoring only after this number of second(s) have passed. Defaults to 1.\n",
        program
    );
}

void parse_args(int argc, char** argv) {
    int c = 0, force, core;

    core = 0;
    force = 0;

    while ((c = getopt(argc, argv, "vmrftpu:")) != -1) {
        switch (c) {
            case 'v':
                print_version();
                exit(0);
            case 'm':
                print_memory_timings();
                exit(0);
            case 'r':
                use_rapl = 1;
                break;
            case 'f':
                force = 1;
                break;
            case 'u':
                // TODO
                break;
            case 'h':
                show_help(argv[0]);
                exit(0);
            case '?':
                exit(0);
            default:
                break;
        }
    }

    start_pm_monitor(force);
}

int main(int argc, char** argv) {
    smu_return_val ret;

    if (getuid() != 0 && geteuid() != 0) {
        fprintf(stderr, "Program must be run as root.\n");
        exit(-1);
    }

    ret = smu_init(&obj);
    if (ret != SMU_Return_OK) {
        fprintf(stderr, "%s\n", smu_return_to_str(ret));
        exit(-2);
    }

    parse_args(argc, argv);

    return 0;
}
