#include "acpi_sim.h"
#include "../drivers/vga.h"
#include "../drivers/acpi.h"
#include "../libc/string.h"
#include "../include/io.h"

// Simulated ACPI Registers
static uint16_t sim_pm1a_evt = 0;
static uint16_t sim_pm1a_cnt = 0;
static uint16_t sim_pm1b_cnt = 0;
static uint8_t  sim_smi_cmd  = 0;
static uint32_t sim_gpe0_sts = 0;
static uint32_t sim_gpe0_en  = 0;

static char sim_state[4] = "S0";

void acpi_sim_init(void) {
    sim_pm1a_evt = 0;
    sim_pm1a_cnt = 0;
    sim_pm1b_cnt = 0;
    sim_smi_cmd  = 0;
    sim_gpe0_sts = 0;
    sim_gpe0_en  = 0xFFFFFFFF; // Enable all GPEs by default for easy simulation
    strcpy(sim_state, "S0");
}

static void print_sim_help(void) {
    vga_puts("Simulated ACPI (sacpi) Tool:\n");
    vga_puts("  sacpi help                   - Display this help message\n");
    vga_puts("  sacpi info                   - Show actual and simulated ACPI tables & status\n");
    vga_puts("  sacpi state                  - Show current simulated sleep state\n");
    vga_puts("  sacpi event <gpe_bit>        - Trigger a General Purpose Event (GPE)\n");
    vga_puts("  sacpi write <reg> <value>    - Write to simulated ACPI register\n");
    vga_puts("  sacpi read <reg>             - Read from simulated ACPI register\n");
    vga_puts("  sacpi aml <method> [arg]     - Execute simulated AML control method\n");
    vga_puts("  sacpi shutdown               - Simulate step-by-step S5 shutdown sequence\n");
    vga_puts("  sacpi reboot                 - Simulate step-by-step ACPI reboot sequence\n");
    vga_puts("  sacpi reset                  - Reset simulated ACPI registers and state\n");
}

static uint32_t parse_num(const char* s) {
    if (s[0] == '0' && s[1] == 'x') {
        uint32_t val = 0;
        s += 2;
        while (*s) {
            val <<= 4;
            if (*s >= '0' && *s <= '9') val += *s - '0';
            else if (*s >= 'a' && *s <= 'f') val += *s - 'a' + 10;
            else if (*s >= 'A' && *s <= 'F') val += *s - 'A' + 10;
            s++;
        }
        return val;
    }
    return atoi(s);
}

static void handle_pm1a_cnt_write(uint16_t val) {
    sim_pm1a_cnt = val;
    int slp_en = (val >> 13) & 1;
    int slp_typx = (val >> 10) & 7;
    int sci_en = val & 1;

    vga_printf("[SimACPI] Write PM1a_CNT = 0x%x (SCI_EN=%d, SLP_TYPx=%d, SLP_EN=%d)\n",
               val, sci_en, slp_typx, slp_en);

    if (slp_en) {
        vga_printf("[SimACPI] SLP_EN bit set! Initiating transition from %s...\n", sim_state);
        vga_puts("[SimACPI] Executing _PTS (Prepare To Sleep) AML method...\n");
        vga_printf("[AML] _PTS(%d) executed successfully.\n", slp_typx);

        if (slp_typx == 5 || slp_typx == 7) {
            vga_puts("[SimACPI] S5 Soft Off (Shutdown) requested!\n");
            vga_puts("[SimACPI] Disabling GPEs and preparing devices for power down...\n");
            vga_puts("[SimACPI] Powering off simulated hardware.\n");
            strcpy(sim_state, "S5");
            vga_set_color(VGA_LIGHT_RED, VGA_BLACK);
            vga_puts("[System State: S5 (Powered Off)]\n");
            vga_set_color(VGA_LIGHT_GREY, VGA_BLACK);
        } else if (slp_typx == 3) {
            strcpy(sim_state, "S3");
            vga_puts("[SimACPI] System transitioned to S3 State (Suspend to RAM).\n");
            vga_puts("[SimACPI] Simulated DRAM entered self-refresh mode.\n");
        } else if (slp_typx == 4) {
            strcpy(sim_state, "S4");
            vga_puts("[SimACPI] System transitioned to S4 State (Suspend to Disk).\n");
            vga_puts("[SimACPI] Simulated system state saved to disk.\n");
        } else {
            vga_printf("[SimACPI] System transitioned to S%d sleep state.\n", slp_typx);
            itoa(slp_typx, sim_state + 1, 10);
            sim_state[0] = 'S';
            sim_state[2] = '\0';
        }
    }
}

void acpi_sim_execute_command(const char* args) {
    while (*args == ' ') args++;
    if (*args == '\0') {
        print_sim_help();
        return;
    }

    char cmd[32];
    int i = 0;
    while (*args && *args != ' ' && i < 31) {
        cmd[i++] = *args++;
    }
    cmd[i] = '\0';

    while (*args == ' ') args++;

    if (strcmp(cmd, "help") == 0) {
        print_sim_help();
    }
    else if (strcmp(cmd, "info") == 0) {
        vga_puts("--- Actual ACPI Hardware Info ---\n");
        acpi_rsdp_t* rsdp = acpi_get_rsdp();
        acpi_fadt_t* fadt = acpi_get_fadt();
        if (rsdp) {
            vga_printf("  RSDP Address:  0x%x\n", (uint32_t)rsdp);
            vga_printf("  OEM ID:        %.6s\n", rsdp->oem_id);
            vga_printf("  RSDT Address:  0x%x\n", rsdp->rsdt_addr);
        } else {
            vga_puts("  RSDP Address:  Not Found (Real ACPI skipped/not supported)\n");
        }

        if (fadt) {
            vga_printf("  FADT Address:  0x%x\n", (uint32_t)fadt);
            vga_printf("  PM1a_CNT_BLK:  0x%x\n", fadt->pm1a_cnt_blk);
            vga_printf("  PM1b_CNT_BLK:  0x%x\n", fadt->pm1b_cnt_blk);
            vga_printf("  SMI_CMD Port:  0x%x\n", fadt->smi_cmd);
            vga_printf("  ACPI Enable:   0x%x\n", fadt->acpi_enable);
            vga_printf("  ACPI Disable:  0x%x\n", fadt->acpi_disable);
        } else {
            vga_puts("  FADT Address:  Not Found\n");
        }

        vga_puts("\n--- Simulated ACPI Registry Space ---\n");
        vga_printf("  Simulated State:   %s\n", sim_state);
        vga_printf("  Sim PM1a_EVT:      0x%x\n", sim_pm1a_evt);
        vga_printf("  Sim PM1a_CNT:      0x%x\n", sim_pm1a_cnt);
        vga_printf("  Sim PM1b_CNT:      0x%x\n", sim_pm1b_cnt);
        vga_printf("  Sim SMI_CMD:       0x%x\n", sim_smi_cmd);
        vga_printf("  Sim GPE0_STS:      0x%x\n", sim_gpe0_sts);
        vga_printf("  Sim GPE0_EN:       0x%x\n", sim_gpe0_en);
        vga_printf("  ACPI Enabled:      %s\n", (sim_pm1a_cnt & 1) ? "YES" : "NO");
    }
    else if (strcmp(cmd, "state") == 0) {
        vga_printf("Simulated ACPI Power State: %s\n", sim_state);
    }
    else if (strcmp(cmd, "reset") == 0) {
        acpi_sim_init();
        vga_puts("Simulated ACPI state and registers reset to S0 (Working).\n");
    }
    else if (strcmp(cmd, "event") == 0) {
        if (*args == '\0') {
            vga_puts("Usage: sacpi event <gpe_bit>\n");
            return;
        }
        int gpe = atoi(args);
        if (gpe < 0 || gpe >= 32) {
            vga_puts("Error: GPE bit must be between 0 and 31.\n");
            return;
        }

        vga_printf("[SimACPI] External event triggered GPE bit %d\n", gpe);
        sim_gpe0_sts |= (1 << gpe);

        if (sim_gpe0_en & (1 << gpe)) {
            vga_printf("[SimACPI] GPE %d is ENABLED in GPE0_EN.\n", gpe);
            if (sim_pm1a_cnt & 1) { // SCI_EN
                vga_puts("[SimACPI] SCI_EN is set. Generating System Control Interrupt (SCI)...\n");
                vga_printf("[AML] Executing control method _L%02d or _E%02d...\n", gpe, gpe);
                vga_printf("[AML] Event %d handled. Clearing GPE %d status bit.\n", gpe, gpe);
                sim_gpe0_sts &= ~(1 << gpe);
            } else {
                vga_puts("[SimACPI] SCI_EN is not set. Event queued but no interrupt generated.\n");
            }
        } else {
            vga_printf("[SimACPI] GPE %d is DISABLED in GPE0_EN. Event ignored.\n", gpe);
        }
    }
    else if (strcmp(cmd, "write") == 0) {
        char reg[32];
        int j = 0;
        while (*args && *args != ' ' && j < 31) {
            reg[j++] = *args++;
        }
        reg[j] = '\0';

        while (*args == ' ') args++;
        if (*args == '\0') {
            vga_puts("Usage: sacpi write <reg> <value>\n");
            return;
        }
        uint32_t val = parse_num(args);

        if (strcmp(reg, "pm1a_cnt") == 0) {
            handle_pm1a_cnt_write(val);
        } else if (strcmp(reg, "pm1b_cnt") == 0) {
            sim_pm1b_cnt = val;
            vga_printf("[SimACPI] Sim PM1b_CNT written with 0x%x\n", val);
        } else if (strcmp(reg, "smi_cmd") == 0) {
            sim_smi_cmd = val;
            vga_printf("[SimACPI] SMI_CMD written with 0x%x\n", val);
            if (val == 0xF0) {
                sim_pm1a_cnt |= 1; // Set SCI_EN
                vga_puts("[SimACPI] Enabled ACPI mode (SCI_EN set in PM1a_CNT).\n");
            } else if (val == 0xF1) {
                sim_pm1a_cnt &= ~1; // Clear SCI_EN
                vga_puts("[SimACPI] Disabled ACPI mode (SCI_EN cleared).\n");
            }
        } else if (strcmp(reg, "gpe0_en") == 0) {
            sim_gpe0_en = val;
            vga_printf("[SimACPI] GPE0_EN written with 0x%x\n", val);
        } else if (strcmp(reg, "gpe0_sts") == 0) {
            sim_gpe0_sts = val;
            vga_printf("[SimACPI] GPE0_STS written with 0x%x\n", val);
        } else {
            vga_printf("Unknown simulated register: %s\n", reg);
        }
    }
    else if (strcmp(cmd, "read") == 0) {
        if (*args == '\0') {
            vga_puts("Usage: sacpi read <reg>\n");
            return;
        }
        char reg[32];
        strcpy(reg, args);
        // Trim spaces
        int len = strlen(reg);
        while (len > 0 && reg[len-1] == ' ') {
            reg[len-1] = '\0';
            len--;
        }

        if (strcmp(reg, "pm1a_cnt") == 0) {
            vga_printf("PM1a_CNT = 0x%x\n", sim_pm1a_cnt);
        } else if (strcmp(reg, "pm1b_cnt") == 0) {
            vga_printf("PM1b_CNT = 0x%x\n", sim_pm1b_cnt);
        } else if (strcmp(reg, "smi_cmd") == 0) {
            vga_printf("SMI_CMD = 0x%x\n", sim_smi_cmd);
        } else if (strcmp(reg, "gpe0_en") == 0) {
            vga_printf("GPE0_EN = 0x%x\n", sim_gpe0_en);
        } else if (strcmp(reg, "gpe0_sts") == 0) {
            vga_printf("GPE0_STS = 0x%x\n", sim_gpe0_sts);
        } else {
            vga_printf("Unknown simulated register: %s\n", reg);
        }
    }
    else if (strcmp(cmd, "aml") == 0) {
        if (*args == '\0') {
            vga_puts("Usage: sacpi aml <method> [arg]\n");
            return;
        }
        char method[32];
        int j = 0;
        while (*args && *args != ' ' && j < 31) {
            method[j++] = *args++;
        }
        method[j] = '\0';

        while (*args == ' ') args++;

        if (strcmp(method, "_INI") == 0) {
            vga_puts("[AML] Executing \\_SB._INI (System Bus Initialization Method)...\n");
            vga_puts("[AML] Initializing platform components, configuring interrupts.\n");
            vga_puts("[AML] Namespace initialization complete. Return value: 0\n");
        } else if (strcmp(method, "_PTS") == 0) {
            int slp = 5;
            if (*args) slp = atoi(args);
            vga_printf("[AML] Executing \\_PTS(%d) (Prepare To Sleep)...\n", slp);
            vga_printf("[AML] Saving device contexts for S%d target state.\n", slp);
            vga_puts("[AML] PTS complete. Ready for PM1 control write.\n");
        } else if (strcmp(method, "_S5") == 0) {
            vga_puts("[AML] Reading \\_S5 object (S5 Package Definition)...\n");
            vga_puts("[AML] S5 Package structure:\n");
            vga_puts("  Package (0x4) {\n");
            vga_puts("    0x7,  // PM1a_CNT Value (SLP_TYPa)\n");
            vga_puts("    0x7,  // PM1b_CNT Value (SLP_TYPb)\n");
            vga_puts("    0x0,  // Reserved\n");
            vga_puts("    0x0   // Reserved\n");
            vga_puts("  }\n");
        } else if (strcmp(method, "_WAK") == 0) {
            vga_puts("[AML] Executing \\_WAK (System Wake Method)...\n");
            vga_puts("[AML] Restoring PCI configurations, waking display controllers.\n");
            vga_puts("[AML] Return status: Package(0x0, 0x0) (Success)\n");
        } else {
            vga_printf("Unsupported or unknown AML method: %s\n", method);
        }
    }
    else if (strcmp(cmd, "shutdown") == 0) {
        vga_puts("--- Simulated ACPI Shutdown (S5 Transition) ---\n");
        vga_puts("1. OSPM queries AML namespace: reads \\_S5 package.\n");
        vga_puts("   -> Returns SLP_TYPa = 7 (for S5 soft-off).\n");
        vga_puts("2. OSPM executes \\_PTS(5) (Prepare To Sleep AML method).\n");
        vga_puts("   -> Preparation complete.\n");
        vga_puts("3. OSPM disables interrupts and writes to PM1a_CNT:\n");
        acpi_fadt_t* fadt = acpi_get_fadt();
        vga_printf("   -> outw(PM1a_CNT_PORT, (7 << 10) | (1 << 13)) -> outw(0x%x, 0x3c00)\n",
                   fadt ? fadt->pm1a_cnt_blk : 0x4004);
        if (fadt && fadt->pm1b_cnt_blk) {
            vga_printf("4. OSPM writes to PM1b_CNT:\n");
            vga_printf("   -> outw(PM1b_CNT_PORT, (7 << 10) | (1 << 13)) -> outw(0x%x, 0x3c00)\n",
                       fadt->pm1b_cnt_blk);
        }
        vga_puts("5. Hardware detects SLP_EN=1 and SLP_TYPx=7: cuts main power.\n");
        vga_puts("\nSimulation complete. Note: Run the shell's 'shutdown' command to perform an actual ACPI power off.\n");
    }
    else if (strcmp(cmd, "reboot") == 0) {
        vga_puts("--- Simulated ACPI Reboot Sequence ---\n");
        vga_puts("1. OSPM attempts standard keyboard controller reset:\n");
        vga_puts("   -> Wait for input buffer to clear (reading 0x64 until bit 1 is 0).\n");
        vga_puts("   -> outb(0x64, 0xFE) (Pulse Reset pin of CPU).\n");
        vga_puts("2. If keyboard controller is unresponsive, check FADT RESET_REG.\n");
        acpi_fadt_t* fadt = acpi_get_fadt();
        if (fadt) {
            vga_printf("   -> FADT RESET_REG address: 0x%x\n", fadt->pm_tmr_blk - 0x10);
            vga_puts("   -> Write FADT RESET_VALUE to RESET_REG to force reboot.\n");
        } else {
            vga_puts("   -> No FADT loaded, skipping RESET_REG check.\n");
        }
        vga_puts("3. System reboot triggers CPU power-on initialization (RESET vector 0xFFFFFFF0).\n");
        vga_puts("\nSimulation complete. Note: Run the shell's 'reboot' command to perform an actual ACPI reboot.\n");
    }
    else {
        vga_printf("Unknown sacpi subcommand: %s\n", cmd);
    }
}
