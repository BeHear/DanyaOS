#ifndef DANYA_ACPI_SIM_H
#define DANYA_ACPI_SIM_H

#include "../include/types.h"

void acpi_sim_init(void);
void acpi_sim_execute_command(const char* args);

#endif
