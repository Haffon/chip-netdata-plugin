# Netdata plugin for C.H.I.P.

This is a lightweight plugin for [netdata](https://github.com/firehol/netdata) that generates charts for [C.H.I.P.'s](https://getchip.com/pages/chip) hardware sensors, such as the internal temperature and battery information.  It is written in C to be highly efficient and has no external dependencies.  Think of it as a much prettier version of [battery.sh](https://github.com/NextThingCo/CHIP-hwtest/blob/chip/stable/chip-hwtest/bin/battery.sh).

## Installation

1. [Install Netdata](https://github.com/firehol/netdata/wiki/Installation)
2. Build & install chip.plugin:
```
curl -O https://raw.githubusercontent.com/jengel/chip-netdata-plugin/master/chip.plugin.c
gcc -o chip.plugin chip.plugin.c
sudo cp chip.plugin /usr/libexec/netdata/plugins.d/
```
3. (Optional) Restart netdata to immediately pick up the new plugin. Otherwise, it will be automatically recognized within 60 seconds.
```
killall netdata
/usr/sbin/netdata
```

## Charts

Here is the list of the graphs it produces:
- Internal temperature (&deg;F)
- ACIN voltage (mV) & current (mA)
- Battery voltage (mV)
- Battery level (%)
- Battery charge & discharge current (mA)
- VBUS voltage (mV), current (mA), & current limit (mA)

## License

MIT License.