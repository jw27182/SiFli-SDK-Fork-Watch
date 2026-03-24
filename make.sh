#!/bin/bash

# Activate environment
eval "$(/home/jw27182/.sifli/python_env/sifli-sdk2.4_py3.12_env/bin/python /home/jw27182/workspace/sf32Watch/SiFli-SDK-NEWLY/SiFli-SDK/tools/activate.py --export --shell bash)"

# Change directory and build
cd "/home/jw27182/workspace/sf32Watch/SiFli-SDK-NEWLY/SiFli-SDK/app/watch/project"
scons --board=sf32lb52-watch -j16

sftool -p /dev/ttyACM0 -c SF32LB52 -b 1000000 -m nor write_flash --verify "/home/jw27182/workspace/sf32Watch/SiFli-SDK-NEWLY/SiFli-SDK/app/watch/project/build_sf32lb52-watch_hcpu/bootloader/bootloader.bin@0x12010000" "/home/jw27182/workspace/sf32Watch/SiFli-SDK-NEWLY/SiFli-SDK/app/watch/project/build_sf32lb52-watch_hcpu/main.bin@0x12020000" "/home/jw27182/workspace/sf32Watch/SiFli-SDK-NEWLY/SiFli-SDK/app/watch/project/build_sf32lb52-watch_hcpu/ftab/ftab.bin@0x12000000"; sifli_exit=$?; printf "%s" "$sifli_exit" > "/tmp/sifli-sifli__download-292dd62e-5334-4b5d-9685-167735de1f06.code"