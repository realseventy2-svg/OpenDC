! ==============================================================================
! Sega Dreamcast Complete Cold-Boot Reset Stub & ASIC Hardware Bring-Up
! Target: Hitachi SH7091 (SH-4) @ 200 MHz
! Base:   0xA0000000 (Flash ROM)
! ==============================================================================

.section .text.entry, "ax"
.global _start
.global start
.extern _main
.type _start, @function
.align 4

_start:
start:
    ! ---- 1. CPU Configuration ----
    ! Enter Privileged Mode, mask interrupts, clear exception block (BL=0)
    mov.l   val_sr_init, r0
    ldc     r0, sr

    ! Set Vector Base Register to SDRAM Base (0x8C000000)
    mov.l   val_vbr_init, r0
    ldc     r0, vbr

    ! Initialize SH-4 Floating Point Unit (FPSCR: Single precision, DN=1)
    mov.l   val_fpscr_init, r0
    lds     r0, fpscr

    ! Initialize SH-4 On-Chip Caches (CCR: 16KB Operand + 8KB Instruction)
    mov.l   p_ccr, r1
    mov.l   val_ccr_init, r0
    mov.l   r0, @r1

    ! ---- 2. Bus State Controller (BSC) / 16MB SDRAM Bring-Up ----
    mov.l   p_bsc_base, r1          ! 0xFF800000

    ! BCR1 = 0x00000000 (Area 0 ROM 32-bit, Area 3 SDRAM 32-bit)
    mov     #0, r0
    mov.l   r0, @r1

    ! BCR2 = 0x0008 (Area 3 32-bit Bus Width)
    mov     #8, r0
    mov.w   r0, @(4, r1)

    ! WCR1, WCR2, WCR3 = 0 (Zero waitstates)
    mov     #0, r0
    mov.l   r0, @(8, r1)
    mov.l   r0, @(12, r1)
    mov.l   r0, @(16, r1)

    ! MCR = 0x10095814 (SDRAM Timings, Burst Mode, Area 3 Enable)
    mov.l   val_mcr, r0
    mov.l   r0, @(20, r1)

    ! RTCSR = 0xA508 (Refresh Timer Control: phi/4 clock)
    mov.l   p_rtcsr, r1
    mov.l   val_rtcsr, r0
    mov.w   r0, @r1

    ! RTCOR = 0xA520 (Time Constant for 15.6 us Auto-Refresh)
    mov.l   p_rtcor, r1
    mov.l   val_rtcor, r0
    mov.w   r0, @r1

    ! RFCR = 0xA400 (Clear Refresh Counter)
    mov.l   p_rfcr, r1
    mov.l   val_rfcr, r0
    mov.w   r0, @r1

    ! ---- 3. PowerVR2 / HOLLY System ASIC Reset Pulse ----
    mov.l   p_pvr_reset, r1         ! 0xA05F8008
    mov.l   val_pvr_rst_on, r0      ! 0x00000003 (Assert TA & Core Reset)
    mov.l   r0, @r1

    ! Short delay during reset assert
    mov.l   val_rst_delay, r2
rst_delay_loop:
    dt      r2
    bf      rst_delay_loop

    ! Release PowerVR2 Reset
    mov     #0, r0
    mov.l   r0, @r1                 ! 0x00000000 (Release Reset)

    ! ---- 4. Hardware ASIC & PLL Stabilization Window (1.5 Seconds) ----
    ! Calibrated for SH-4 200 MHz core: ~20,000,000 iterations (~1.5 seconds)
    mov.l   val_asic_settle, r2
asic_settle_loop:
    dt      r2
    bf      asic_settle_loop

    ! ---- 5. Stack Setup (Top of 16MB SDRAM) ----
    mov.l   val_stack_top, r15

    ! ---- 6. Relocate .data Section (Flash ROM LMA -> SDRAM VMA) ----
    mov.l   p_data_lma, r0
    mov.l   p_data_vma, r1
    mov.l   val_data_size, r2
    tst     r2, r2
    bt      zero_bss

copy_data_loop:
    mov.l   @r0+, r3
    mov.l   r3, @r1
    add     #4, r1
    add     #-4, r2
    cmp/pl  r2
    bt      copy_data_loop

zero_bss:
    ! ---- 7. Zero-Fill .bss Section in SDRAM ----
    mov.l   p_bss_start, r0
    mov.l   p_bss_end,   r1
    mov     #0, r2

zero_bss_loop:
    cmp/hs  r1, r0
    bt      jump_to_c
    mov.l   r2, @r0
    add     #4, r0
    bra     zero_bss_loop
    nop

jump_to_c:
    ! ---- 8. Handoff to C _main() in SDRAM ----
    mov.l   p_main_addr, r0
    jmp     @r0
    nop

halt_loop:
    sleep
    bra     halt_loop
    nop

.align 4
val_sr_init:        .long   0x400000F0     ! Privileged mode, BL=0, IMASK=15
val_vbr_init:       .long   0x8C000000     ! SDRAM Exception Vector Base
val_fpscr_init:     .long   0x00040001     ! Single-precision FPU, Denorm Flush
p_ccr:              .long   0xFF00001C     ! Cache Control Register
val_ccr_init:       .long   0x0000080B     ! Instruction & Operand Cache Enable

p_bsc_base:         .long   0xFF800000     ! Bus State Controller Base
val_mcr:            .long   0x10095814     ! Area 3 SDRAM Timings
p_rtcsr:            .long   0xFF80001C
val_rtcsr:          .long   0x0000A508
p_rtcor:            .long   0xFF800024
val_rtcor:          .long   0x0000A520
p_rfcr:             .long   0xFF800028
val_rfcr:           .long   0x0000A400

p_pvr_reset:        .long   0xA05F8008     ! PowerVR Core / TA Reset Register
val_pvr_rst_on:     .long   0x00000003     ! Assert Reset
val_rst_delay:      .long   0x00000400     ! Reset Pulse Duration
val_asic_settle:    .long   0x01312D00     ! 20,000,000 cycles (~1.5s ASIC PLL lock)

val_stack_top:      .long   __stack_top    ! 0x8D000000
p_data_lma:         .long   __data_lma
p_data_vma:         .long   __data_vma
val_data_size:      .long   __data_size
p_bss_start:        .long   __bss_start
p_bss_end:          .long   __bss_end
p_main_addr:        .long   _main
