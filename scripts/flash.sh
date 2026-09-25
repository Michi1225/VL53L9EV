#!/usr/bin/env bash

set -e

PROGRAMMER="${STM32_PRG_PATH}/STM32_Programmer_CLI"
LOADER="${STM32_PRG_PATH}/ExternalLoader/VL53L9_eval_ExtMemLoader.stldr"

FSBL_ADDR=0x90040000
APP_A_ADDR=0x90100000
APP_B_ADDR=0x90900000

case "$1" in

    (fsbl)
        "$PROGRAMMER" \
            -c port=SWD \
            -el "$LOADER" \
            -w "FSBL/build/VL53L9_eval_FSBL-signed.bin" "$FSBL_ADDR" \
            -v
        ;;

    (app-a)
        "$PROGRAMMER" \
            -c port=SWD \
            -el "$LOADER" \
            -w "Appli/build/VL53L9_eval_Appli-signed.bin" "$APP_A_ADDR" \
            -v
        ;;

    (app-b)
        "$PROGRAMMER" \
            -c port=SWD \
            -el "$LOADER" \
            -w "Appli/build/VL53L9_eval_Appli-signed.bin" "$APP_B_ADDR" \
            -v
        ;;

    (*)
        echo "Usage: $0 {fsbl|app-a|app-b}"
        exit 1
        ;;
esac