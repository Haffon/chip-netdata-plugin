//
// Copyright (c) Jeff Engel
//
// This module is a plugin for netdata to add charts for a CHIP computer's
// power and battery status.
// The code is based heavily on ideas from https://gist.github.com/yoursunny/b89f86c9f5911cea322f3047ff99c576
//
// Installation:
//   gcc -o chip.plugin chip.plugin.c
//   cp chip.plugin /usr/libexec/netdata/plugins.d/
//

#include <fcntl.h>
#include <inttypes.h>
#include <memory.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <linux/i2c-dev.h>

//
// Enumeration of all possible dimensions. Each dimension is mapped as an x-
// value for a chart.
//

enum Dimensions
{
    internaltemp,
    batlevel,
    chargelimit,
    chargeterm,
    batcharge,
    batdischarge,
    batvoltage,
    acinvoltage,
    acincurrent,
    vbusvoltage,
    vbusvoltagelimit,
    vbuscurrent,
    vbuscurrentlimit,

    MaxDimensions
};

enum DataType
{
    Float,
    Uint8,
    Uint16
};

struct
{
    char * Name;
    enum DataType DataType;
    char * DataFormat;
    char * Properties;
} const gDimensionDefinitions[MaxDimensions] = {
    { "internaltemp",     Float,  "%.1f",     "\"Internal Temp\" absolute" },
    { "batlevel",         Uint8,  "%" PRIu8,  "\"Charge\" absolute" },
    { "chargelimit",      Uint16, "%" PRIu16, "\"Charge Limit\" absolute" },
    { "chargeterm",       Uint16, "%" PRIu16, "\"Charge Termination Limit\" absolute" },
    { "batcharge",        Float,  "%.1f",     "\"Batt Charge\" absolute" },
    { "batdischarge",     Uint16, "%" PRIu16, "\"Batt Discharge\" absolute" },
    { "batvoltage",       Float,  "%.1f",     "\"Voltage\" absolute" },
    { "acinvoltage",      Float,  "%.1f",     "\"Voltage\" absolute" },
    { "acincurrent",      Float,  "%.3f",     "\"Current\" absolute" },
    { "vbusvoltage",      Float,  "%.1f",     "\"Voltage\" absolute" },
    { "vbusvoltagelimit", Uint16, "%" PRIu16, "\"Limit\" absolute" },
    { "vbuscurrent",      Float,  "%.3f",     "\"Current\" absolute" },
    { "vbuscurrentlimit", Uint16, "%" PRIu16, "\"Limit\" absolute" },
};

#define MAX_CHART_DIMENSIONS 4
#define EMPTY_DIM MaxDimensions

struct
{
    char * Name;
    char * Properties;
    enum Dimensions Dimensions[MAX_CHART_DIMENSIONS];
} const gChartDefinitions[] = {
    { "Chip.temps", "\"\" \"Temperature\" \"Degrees (F)\"", { internaltemp, EMPTY_DIM, EMPTY_DIM, EMPTY_DIM } },
    { "Chip.batterylevel", "\"\" \"Battery Level\" \"%\"", { batlevel, EMPTY_DIM, EMPTY_DIM, EMPTY_DIM } },
    { "Chip.batterycurrent", "\"\" \"Battery Current\" \"mA\"", { chargelimit, chargeterm, batcharge, batdischarge } },
    { "Chip.batteryvoltage", "\"\" \"Battery Voltage\" \"mV\"", { batvoltage, EMPTY_DIM, EMPTY_DIM, EMPTY_DIM } },
    { "Chip.acinvoltage", "\"\" \"ACIN Voltage\" \"mV\"", { acinvoltage, EMPTY_DIM, EMPTY_DIM, EMPTY_DIM } },
    { "Chip.acincurrent", "\"\" \"ACIN Current\" \"mA\"", { acincurrent, EMPTY_DIM, EMPTY_DIM, EMPTY_DIM } },
    { "Chip.vbusvoltage", "\"\" \"VBUS Voltage\" \"mV\"", { vbusvoltage, vbusvoltagelimit, EMPTY_DIM, EMPTY_DIM } },
    { "Chip.vbuscurrent", "\"\" \"VBUS Current\" \"mA\"", { vbuscurrent, vbuscurrentlimit, EMPTY_DIM, EMPTY_DIM } },
};

#define NUM_CHARTS (sizeof(gChartDefinitions) / sizeof(gChartDefinitions[0]))

//
// C.H.I.P. hardware-specific constants.
//

#define I2C_DEVICE "i2c-0"
#define AXP209_ADDRESS 0x34

int gI2c;
uint16_t gUpdateEvery;

//
// After it is read from the I2C bus and processed, the data for each dimension
// is stored in the gData array and a bit is set in the gIsValid bitmask to\
// indicate that the data can be used.
//

typedef union {
    float asFloat;
    uint8_t asUint8;
    uint16_t asUint16;
} value_t;

value_t gData[MaxDimensions];
uint32_t gIsValid;

void save_data_float(enum Dimensions index, float value)
{
    gData[index].asFloat = value;
    gIsValid |= 1 << index;
}

void save_data_uint8(enum Dimensions index, uint8_t value)
{
    gData[index].asUint8 = value;
    gIsValid |= 1 << index;
}

void save_data_uint16(enum Dimensions index, uint16_t value)
{
    gData[index].asUint16 = value;
    gIsValid |= 1 << index;
}

void format_data_value(enum Dimensions index, char buffer[32])
{
    if ((gIsValid & (1 << index)) == 0) {
        buffer[0] = '\0';
        return;
    }

    switch (gDimensionDefinitions[index].DataType)
    {
    case Float:
        sprintf(buffer, gDimensionDefinitions[index].DataFormat, gData[index].asFloat);
        break;
    case Uint8:
        sprintf(buffer, gDimensionDefinitions[index].DataFormat, gData[index].asUint8);
        break;
    case Uint16:
        sprintf(buffer, gDimensionDefinitions[index].DataFormat, gData[index].asUint16);
        break;
    }
}


int read_register_value(uint8_t address)
{
    int err;
    uint8_t buffer[1] = { address };

    err = write(gI2c, buffer, sizeof(buffer));
    if (err < 0) {
        fprintf(stderr, "Unable to query for register %#02x\n", address);
        exit(1);
    }

    err = read(gI2c, buffer, sizeof(buffer));
    if (err < 0) {
        fprintf(stderr, "Unable to read register %#02x\n", address);
        exit(1);
    }

    return buffer[0];
}

void write_register_value(uint8_t address, uint8_t value)
{
    int err;
    uint8_t buffer[2] = { address, value };

    err = write(gI2c, buffer, sizeof(buffer));
    if (err < 0) {
        fprintf(stderr, "Unable to write register %#02x\n", address);
        exit(1);
    }
}

int enable_adc(void)
{
    int err;
    uint8_t value;
    bool wait;

    err = ioctl(gI2c, I2C_SLAVE_FORCE, AXP209_ADDRESS);
    if (err < 0) {
        return err;
    }

    // Ensure both ADC enable registers have sufficient bitmasks to enable the
    // features necessary for querying power.

    wait = false;

    value = read_register_value(0x82);
    if ((value & 0xCC) != 0xCC) {
        write_register_value(0x82, value | 0xCC);
        wait = true;
    }

    value = read_register_value(0x83);
    if ((value & 0x80) != 0x80) {
        write_register_value(0x83, value | 0x80);
        wait = true;
    }

    // Wait for 1/25 seconds to ensure the ADC takes a reading.

    if (wait) {
        usleep(40000);
    }

    return 0;
}

uint16_t read_multi_value(uint8_t highaddress, uint8_t lowaddress)
{
    uint16_t highvalue = read_register_value(highaddress);
    uint16_t lowvalue = read_register_value(lowaddress);

    return (highvalue << 4) | (lowvalue & 0xF);
}

void gather_chart_data(void)
{
    uint8_t charge_ctl;
    uint8_t power_status;

    memset(gData, 0, sizeof(gData));
    gIsValid = 0;

    power_status = read_register_value(0x01);
    charge_ctl = read_register_value(0x33);

    float temp = read_multi_value(0x5E, 0x5F) * 0.18 - 228.46;
    save_data_float(internaltemp, temp);

    if (charge_ctl & 0x80) {
        uint16_t charge_current_limit = (uint16_t)(charge_ctl & 0xf) * 100 + 300;
        save_data_uint16(chargelimit, charge_current_limit);

        uint16_t charge_termination_limit = charge_current_limit / 10;
        if (charge_ctl & 0x10) {
            charge_termination_limit += charge_termination_limit >> 1;
        }

        save_data_uint16(chargeterm, charge_termination_limit);
    }

    if (power_status & 0x20) {
        float bat_charge = read_multi_value(0x7A, 0x7B) / 2.0f;
        save_data_float(batcharge, bat_charge);

        uint16_t bat_discharge = (read_register_value(0x7C) << 5) | (read_register_value(0x7D) & 0x1F);
        save_data_uint16(batdischarge, bat_discharge);

        uint8_t bat_gauge = read_register_value(0xB9) & 0x7F;
        save_data_uint8(batlevel, bat_gauge);

        float bat_voltage = read_multi_value(0x78, 0x79) * 1.1f;
        save_data_float(batvoltage, bat_voltage);
    }

    if (power_status & 0x80) {
        float acin_voltage = read_multi_value(0x56, 0x57) * 1.7f;
        save_data_float(acinvoltage, acin_voltage);

        float acin_current = read_multi_value(0x58, 0x59) * .625f;
        save_data_float(acincurrent, acin_current);
    }

    if (power_status & 0x20) {
        float vbus_voltage = read_multi_value(0x5A, 0x5B) * 1.7f;
        save_data_float(vbusvoltage, vbus_voltage);

        float vbus_current = read_multi_value(0x5C, 0x5D) * .375f;
        save_data_float(vbuscurrent, vbus_current);
    }

    uint8_t vbus_ipsout = read_register_value(0x30);
    if (vbus_ipsout & 0x40) {
        uint16_t vbus_voltage_limit = (vbus_ipsout >> 3) * 100 + 4000;
        save_data_float(vbusvoltagelimit, vbus_voltage_limit);
    }

    if ((vbus_ipsout & 0x3) < 3) {
        static const uint16_t targets[] = { 900, 500, 100 };
        uint16_t vbus_current_limit = targets[vbus_ipsout & 0x3];
        save_data_uint16(vbuscurrentlimit, vbus_current_limit);
    }
}

void print_charts_preamble()
{
    // Documentation: https://github.com/firehol/netdata/wiki/External-Plugins#chart
    // The format for each chart is:
    //   CHART type.id name title units [family [context [charttype (line, area, stacked)]]]]
    //   DIMENSION id [name [algorithm [multiplier [divisor [hidden]]]]]
    //   (repeat dimensions as necessary)

    uint8_t chartindex;
    uint8_t dimindex;
    enum Dimensions targetindex;

    for (chartindex = 0; chartindex < NUM_CHARTS; chartindex++) {
        printf("CHART %s %s\n",
               gChartDefinitions[chartindex].Name,
               gChartDefinitions[chartindex].Properties);

        for (dimindex = 0; dimindex < MAX_CHART_DIMENSIONS; dimindex++) {
            targetindex = gChartDefinitions[chartindex].Dimensions[dimindex];
            if (targetindex != EMPTY_DIM) {
                printf("DIMENSION %s %s\n",
                       gDimensionDefinitions[targetindex].Name,
                       gDimensionDefinitions[targetindex].Properties);
            }
        }
    }

    fflush(stdout);
}

void print_chart_data(uint64_t delayus)
{
    // Documentation: https://github.com/firehol/netdata/wiki/External-Plugins#data-collection
    // The format for each chart is:
    //   BEGIN type.id [microseconds]
    //   SET id = value
    //   (repeat as necessary)
    //   END
    
    char buffer[32];
    uint8_t chartindex;
    uint8_t dimindex;
    enum Dimensions targetindex;

    for (chartindex = 0; chartindex < NUM_CHARTS; chartindex++) {

        // Chart prologue

        printf("BEGIN %s", gChartDefinitions[chartindex].Name);
        if (delayus) {
            printf(" %" PRIu64 "\n", delayus);
        } else {
            printf("\n");
        }

        // Dimension data

        for (dimindex = 0; dimindex < MAX_CHART_DIMENSIONS; dimindex++) {
            targetindex = gChartDefinitions[chartindex].Dimensions[dimindex];
            if (targetindex != EMPTY_DIM) {
                format_data_value(targetindex, buffer);
                printf("SET %s = %s\n", gDimensionDefinitions[targetindex].Name, buffer);
            }
        }

        // Chart epilogue

        printf("END\n");
    }

    fflush(stdout);
}

int main(int argc, char** argv)
{
    // Parse the optional argument if supplied. It indicates the frequency (in
    // seconds) at which to emit new chart data. Valid values are 1-360.

    if (argc >= 2) {
        gUpdateEvery = atoi(argv[1]);
        if (gUpdateEvery < 1 || gUpdateEvery > 360) {
            fprintf(stderr, "Usage: %s [update_frequency]\n", argv[0]);
            return 1;
        }
    } else {
        gUpdateEvery = 1;
    }

    // Connect to the I2C bus and ensure the ADC is enabled.

    gI2c = open("/dev/" I2C_DEVICE, O_RDWR);
    if (gI2c < 0) {
        fprintf(stderr, "Unable to open a handle to the I2C bus\n");
        return 1;
    }

    if (enable_adc() < 0) {
        fprintf(stderr, "Unable to communicate with AXP209\n");
        return 1;
    }

    // Emit the chart and dimension definitions.

    print_charts_preamble();

    // Main loop: query and emit the values once per iteration.

    struct timespec starttime, endtime;
    uint64_t delta, starttimeus, endtimeus;

    endtimeus = 0;

    while (true) {
        clock_gettime(CLOCK_MONOTONIC, &starttime);
        starttimeus = (uint64_t)(starttime.tv_sec * 1000000L + starttime.tv_nsec / 1000L);

        // Calculate the time since the last frame in microseconds. This is
        // passed to netdata on all but the first frame to provide an accurate
        // collection time.

        if (endtimeus != 0) {
            delta = starttimeus - endtimeus;
        } else {
            delta = 0;
        }

        gather_chart_data();

        print_chart_data(delta);

        // Sleep for a full frame, subtracting out the latency encountered
        // while generating the current frame.

        clock_gettime(CLOCK_MONOTONIC, &endtime);
        endtimeus = (uint64_t)(endtime.tv_sec * 1000000L + endtime.tv_nsec / 1000L);

        delta = endtimeus - starttimeus;
        usleep(gUpdateEvery * 1000000L - delta);
    }

    return 0;
}
