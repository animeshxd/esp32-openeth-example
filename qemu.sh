qemu-system-xtensa \
    -nographic -machine esp32 \
    -drive file=build/build/flash_image.bin,if=mtd,format=raw \
    -nic tap,model=open_eth,ifname=tap0,script=no,downscript=no