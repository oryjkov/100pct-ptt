
When programming from platformIO
```
-d1 \
-s /home/olegr/.platformio/packages/tool-openocd/scripts \
-f interface/stlink.cfg \
-c "transport select hla_swd; set WORKAREASIZE 0x4000" \
-f target/nrf51.cfg \
-c "program {.pio/build/seeedTinyBLE/firmware.hex} verify reset; shutdown;" \
```

programming from arduino

```
/home/olegr/.arduino15/packages/sandeepmistry/tools/openocd/0.10.0-dev.nrf5/bin/openocd
-d0 -f interface/stlink-v2.cfg -c "transport select hla_swd; set WORKAREASIZE 0x4000;" -f target/nrf51.cfg -c "init; halt; nrf51 mass_erase; program {{/home/olegr/.arduino15/packages/sandeepmistry/hardware/nRF5/0.7.0/cores/nRF5/SDK/components/softdevice/s130/hex/s130_nrf51_2.0.1_softdevice.hex}} verify reset; shutdown;"
```
