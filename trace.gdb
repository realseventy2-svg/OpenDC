set architecture sh4
target remote localhost:3263
c
info registers
x/20i -16
x/32wx 0x8c010000
x/20i 0x8c010000
