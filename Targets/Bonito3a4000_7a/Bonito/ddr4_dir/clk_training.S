/*
## @file
#
#  Copyright (c) 2018 Loongson Technology Corporation Limited (www.loongson.cn).
#  All intellectual property rights(Copyright, Patent and Trademark) reserved.

#  Any violations of copyright or other intellectual property rights of the Loongson Technology
#  Corporation Limited will be held accountable in accordance with the law,
#  if you (or any of your subsidiaries, corporate affiliates or agents) initiate
#  directly or indirectly any Intellectual Property Assertion or Intellectual Property Litigation:
#  (i) against Loongson Technology Corporation Limited or any of its subsidiaries or corporate affiliates,
#  (ii) against any party if such Intellectual Property Assertion or Intellectual Property Litigation arises
#  in whole or in part from any software, technology, product or service of Loongson Technology Corporation
#  Limited or any of its subsidiaries or corporate affiliates, or (iii) against any party relating to the Software.
#
#  THIS SOFTWARE IS PROVIDED BY THE AUTHOR "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
#  THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR
#  BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY,OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
#  PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION).
#
#
##
*/
    .text
    .set    noreorder
    .set    mips3

    .global clk_train
    .ent    clk_train
clk_train:
    move    s4, ra
    sync
    nop
    move    k0, t3
    MC_ENABLE(MC0_ONLY_EN)
    beqz    t3, 1f
    nop
    MC_ENABLE(MC1_ONLY_EN)
1:
    sync
    nop
    move    a1, k0
    bal     enable_mc_regs_default
    nop

    move    a1, k0
    bal     enable_mc_conf_space
    nop

    GET_NODE_ID_a0;
    dli     t8, DDR_MC_CONFIG_BASE
    or      t8, t8, a0

    GET_DIMM_TYPE_V1
    and     a1, a1, 1
    bnez    a1, 1f
    nop
    //UDIMM or sodimm
    dla     a2, ddr4_reg_data
    beqz    t3, 2f
    nop
    dla     a2, ddr4_reg_data_mc1
2:
    b       3f
    nop
1:  //RDIMM or LRDIMM
    dla     a2, ddr4_RDIMM_reg_data
    beqz    t3, 2f
    nop
    dla     a2, ddr4_RDIMM_reg_data_mc1
2:
    b       3f
    nop
3:
    bal     mc_clk_train
    nop

    move    a1, k0
    bal     disable_mc_regs_default
    nop

    move    a1, k0
    bal     disable_mc_conf_space
    nop

    move    ra, s4
    jr      ra
    nop
    .end    clk_train

LEAF(mc_clk_train)
    move    t9, ra
    dli     k1, 0

#if (SEL_DBL ==1)
    /* set init s5 value for dbl_clk_training */
    dli     s5, 0

    /* check whether to train */
    GET_NODE_ID_a1
    mul     a1, DIMM_INFO_SIZE
    dli     t1, (0xffffffff00000000 | DIMM_INFO_IN_FLASH_OFFS)
    daddu   t1, a1
    mul     a0, k0, MC_INFO_SIZE	//if k0==1, add mc1 offset
    daddu   t1, a0

    /*check DDR freq */
    li      a1, DDR_FREQ
    lw      a0, DIMM_OFFS_CLK(t1)
    beq     a1, a0, 2f
    nop

    /* training dbl clk */
	or		s5, 1<<32
    /* set init cache value for dbl_clk_training */
    dli     t0, 0
1:
    /* get t0 dcc couple base address in a0 */
    dsll    a0, t0, 3
    daddu   a0, (DIMM_INFO_IN_CACHE_OFFS + DBL_DCC_TRAIN_OFFS)
    /* config dcc couples */
    dli     v0, 63
    dsubu   a1, v0, t0
    and     v1, a1, 7
    dsrl    a1, 3
    and     v0, a1, 7
    dsll    v0, 4
    or      v1, v0
    sw      v1, DBL_DCC_TRAIN_STORED_DCC_OFFSET(a0)
    dli     v0, 1
    sh      v0, DBL_DCC_TRAIN_STORED_DS_SUM_OFFSET(a0)
    dli     v0, 0xffff
    sh      v0, DBL_DCC_TRAIN_STORED_PERIOD_SUB_OFFSET(a0)
    daddu   t0, 1
    bltu    t0, DBL_DCC_TRAIN_STORED_NUM, 1b
    nop
2:
#endif
mcreset:
    beqz    k1, 2f
    nop
//write param registers from cache
    dli     a2, (DIMM_INFO_IN_CACHE_OFFS + MC_PARAM_IN_CACHE_OFFS)
    dli     t1, 744
    daddiu  v0, t8, 0x0
1:
    ld      a1, 0x0(a2)
    sd      a1, 0x0(v0)
    daddiu  t1, t1, -1
    daddiu  a2, a2, 0x8
    daddiu  v0, v0, 0x8
    bnez    t1, 1b
    nop

    b       ddrinit
    nop
2:
    daddu   a2, a2, s0
    nop
    dli     t1, 744
    daddiu  v0, t8, 0x0
//write param registers
1:
    ld      a1, 0x0(a2)
    sd      a1, 0x0(v0)
    daddiu  t1, t1, -1
    daddiu  a2, a2, 0x8
    daddiu  v0, v0, 0x8
    bnez    t1, 1b
    nop

    dli     t0, 0x99
    sb      t0, DBL_CFG(t8)

//cs_map
    GET_MC_CS_MAP_V1
    dli     a0, 0x01
    beq     a0, a1, 1f
    nop
    dli     a0, 0x10
    beq     a0, a1, 2f
    nop
    dli     a0, 0x11
    beq     a0, a1, 3f
    nop
    dli     a0, 0x03
    beq     a0, a1, 4f
    nop
    dli     a0, 0x30
    beq     a0, a1, 5f
    nop
    dli     a0, 0x33
    beq     a0, a1, 6f
    nop
    beq     a1, 0x2, 7f
    nop
    PRINTSTR("\r\nError: The CS MAP is not supported!!!!!!!")
10:
    b       10b
    nop

1:
    dli     a0, 0x0
    sw      a0, DDR4_CS_MAP_OFFSET(t8)
    dli     a0, 0x1
    sw      a0, DDR4_CKE_MAP_OFFSET(t8)
    dli     a0, 0x1
    sw      a0, DDR4_WRODT_MAP_OFFSET(t8)
    b       9f
    nop

2:
    dli     a0, 0x4
    sw      a0, DDR4_CS_MAP_OFFSET(t8)
    dli     a0, 0x4
    sw      a0, DDR4_CKE_MAP_OFFSET(t8)
    dli     a0, 0x4
    sw      a0, DDR4_WRODT_MAP_OFFSET(t8)
    b       9f
    nop

3:
    dli     a0, 0x40
    sw      a0, DDR4_CS_MAP_OFFSET(t8)
    dli     a0, 0x41
    sw      a0, DDR4_CKE_MAP_OFFSET(t8)
    dli     a0, 0x41
    sw      a0, DDR4_WRODT_MAP_OFFSET(t8)
    b       9f
    nop

4:
    dli     a0, 0x10
    sw      a0, DDR4_CS_MAP_OFFSET(t8)
    dli     a0, 0x21
    sw      a0, DDR4_CKE_MAP_OFFSET(t8)
    dli     a0, 0x21
    sw      a0, DDR4_WRODT_MAP_OFFSET(t8)
//set rtt_normal 120ohm
    li      a0, 0x7
    dsll    a0, a0, 8
    not     a0, a0
    lh      a1, DDR4_MR1_CS0_REG(t8)
    and     a1, a1, a0
    li      a0, RTT_DISABLE
    dsll    a0, a0, 8
    or      a1, a1, a0
    sh      a1, DDR4_MR1_CS0_REG(t8)

    li      a0, 0x7
    dsll    a0, a0, 8
    not     a0, a0
    lh      a1, DDR4_MR1_CS1_REG(t8)
    and     a1, a1, a0
    li      a0, RTT_DISABLE
    dsll    a0, a0, 8
    or      a1, a1, a0
    sh      a1, DDR4_MR1_CS1_REG(t8)
//set rtt_park 80ohm
    li      a0, 0x7
    dsll    a0, a0, 6
    not     a0, a0
    lh      a1, DDR4_MR5_CS0_REG(t8)
    and     a1, a1, a0
    li      a0, RTT_60
    dsll    a0, a0, 6
    or      a1, a1, a0
    sh      a1, DDR4_MR5_CS0_REG(t8)

    li      a0, 0x7
    dsll    a0, a0, 6
    not     a0, a0
    lh      a1, DDR4_MR5_CS1_REG(t8)
    and     a1, a1, a0
    li      a0, RTT_60
    dsll    a0, a0, 6
    or      a1, a1, a0
    sh      a1, DDR4_MR5_CS1_REG(t8)

    b       9f
    nop

5:
    dli     a0, 0x54
    sw      a0, DDR4_CS_MAP_OFFSET(t8)
    dli     a0, 0x84
    sw      a0, DDR4_CKE_MAP_OFFSET(t8)
    dli     a0, 0x84
    sw      a0, DDR4_WRODT_MAP_OFFSET(t8)
//set rtt_normal 120ohm
    li      a0, 0x7
    dsll    a0, a0, 8
    not     a0, a0
    lh      a1, DDR4_MR1_CS0_REG(t8)
    and     a1, a1, a0
    li      a0, RTT_DISABLE
    dsll    a0, a0, 8
    or      a1, a1, a0
    sh      a1, DDR4_MR1_CS0_REG(t8)

    li      a0, 0x7
    dsll    a0, a0, 8
    not     a0, a0
    lh      a1, DDR4_MR1_CS1_REG(t8)
    and     a1, a1, a0
    li      a0, RTT_DISABLE
    dsll    a0, a0, 8
    or      a1, a1, a0
    sh      a1, DDR4_MR1_CS1_REG(t8)
//set rtt_park 80ohm
    li      a0, 0x7
    dsll    a0, a0, 6
    not     a0, a0
    lh      a1, DDR4_MR5_CS0_REG(t8)
    and     a1, a1, a0
    li      a0, RTT_60
    dsll    a0, a0, 6
    or      a1, a1, a0
    sh      a1, DDR4_MR5_CS0_REG(t8)

    li      a0, 0x7
    dsll    a0, a0, 6
    not     a0, a0
    lh      a1, DDR4_MR5_CS1_REG(t8)
    and     a1, a1, a0
    li      a0, RTT_60
    dsll    a0, a0, 6
    or      a1, a1, a0
    sh      a1, DDR4_MR5_CS1_REG(t8)

    b       9f
    nop

6:
    dli     a0, 0x5410
    sw      a0, DDR4_CS_MAP_OFFSET(t8)
    dli     a0, 0x8421
    sw      a0, DDR4_CKE_MAP_OFFSET(t8)
    dli     a0, 0x8421
    sw      a0, DDR4_WRODT_MAP_OFFSET(t8)
    b       9f
    nop
7:
    dli     a0, 0x1
    sw      a0, DDR4_CS_MAP_OFFSET(t8)
    dli     a0, 0x2
    sw      a0, DDR4_CKE_MAP_OFFSET(t8)
    dli     a0, 0x2
    sw      a0, DDR4_WRODT_MAP_OFFSET(t8)
9:

##START
ddrinit:
    bnez    k1, 3f
    nop
//store param registers to cache
    dli     v0, (DIMM_INFO_IN_CACHE_OFFS + MC_PARAM_IN_CACHE_OFFS)
    dli     t1, 744
    daddiu  a2, t8, 0x0
1:
    ld      a1, 0x0(a2)
    sd      a1, 0x0(v0)
    daddiu  t1, t1, -1
    daddiu  a2, a2, 0x8
    daddiu  v0, v0, 0x8
    bnez    t1, 1b
    nop
3:

#if (SEL_DBL ==1)
    dsrl    v0, s5, 32
    and     v0, 1
    beqz    v0, 1f
    nop
    /* set dcc couple for dbl_clk_training */
    lb      t4, DDR4_DBL_CTRL_CKCA_OFFSET(t8)
    and     t4, ~(0x3f<<2)
    GET_DBL_DCC_TRAIN_CURRENT_DCC_VALUE
    and     v1, v0, 0x70
    dsrl    v1, 1
    and     v0, 0x7
    or      v0, v1
    dsll    v0, 2
    or      t4, v0
    sb      t4, DDR4_DBL_CTRL_CKCA_OFFSET(t8)
    b       2f
    nop
1:
    /* load dcc value from flash */
    /* t1:save flash base */
    GET_NODE_ID_a1
    mul     a1, DIMM_INFO_SIZE
    dli     t1, (0xffffffff00000000 | DIMM_INFO_IN_FLASH_OFFS)
    daddu   t1, a1
    mul     a0, k0, MC_INFO_SIZE	//if k0==1, add mc1 offset
    daddu   t1, a0
    lb      t0, DIMM_OFFS_DBL_CFG(t1)
    and     v0, t0, 7<<4
    dsrl    v0, 1
    and     v1, t0, 7
    or      v0, v1
    dsll    v0, 2
    lb      v1, DDR4_DBL_CTRL_CKCA_OFFSET(t8)
    and     v1, 3
    or      v0, v1
    sb      v0, DDR4_DBL_CTRL_CKCA_OFFSET(t8)
2:
#endif

//choose clk_2x of ckca according to MC version
    lhu     t3, (PHY_ADDRESS)(t8)
    bne     t3, 0x11, 1f
    nop
#ifdef CLK2X_CKCA_SEL
//ds2 clk_2x choose clk_2x of ckca
    lbu     t3, (DLL_CTRL + 1)(t8)
    ori     t3, (0x1 << 5)
    sb      t3, (DLL_CTRL + 1)(t8)
//ds6 clk_2x choose clk_2x of ckca
    lbu     t3, (DLL_CTRL + 2)(t8)
    ori     t3, (0x1 << 5)
    sb      t3, (DLL_CTRL + 2)(t8)

//ds2 and ds6 choose pll to pd
    lb      t3, 0x78(t8)
    li      t4, ~(0x1)
    and     t3, t4
    sb      t3, 0x78(t8)
    lb      t3, 0xb8(t8)
    li      t4, ~(0x1)
    and     t3, t4
    sb      t3, 0xb8(t8)
#else
//ds2 clk_2x chosse itself
    lbu     t3, (DLL_CTRL + 1)(t8)
    dli     t4, (0x1 << 5)
    not     t4
    and     t3, t4
    sb      t3, (DLL_CTRL + 1)(t8)
//ds6 clk_2x choose itself
    lbu     t3, (DLL_CTRL + 2)(t8)
    dli     t4, (0x1 << 5)
    not     t4
    and     t3, t4
    sb      t3, (DLL_CTRL + 2)(t8)

#endif

//config clk2x dll bypass
    lw      t3, (DLL_CTRL)(t8)
    dli     t4, 0x101010
    not     t4, t4
    and     t3, t3, t4
    sw      t3, (DLL_CTRL)(t8)

//config clk2x predly bypass
    lw      t3, (DLL_CTRL)(t8)
    dli     t4, 0x010101
    or      t3, t3, t4
    sw      t3, (DLL_CTRL)(t8)

1:
//pd pll of ckca ds2 and ds6
    lb      t3, 0x4f(t8)
    ori     t3, 0x1
    sb      t3, 0x4f(t8)

    lb      t3, 0x7f(t8)
    ori     t3, 0x1
    sb      t3, 0x7f(t8)

    lb      t3, 0xbf(t8)
    ori     t3, 0x1
    sb      t3, 0xbf(t8)

//enable dbl dcc of ckca ds2 and ds6
#if 1 // DCC_CTR
    lw      t3, 0x44(t8)
    dli     t4, (0x1 << 14)
    or      t3, t4
    sw      t3, 0x44(t8)

    lw      t3, 0x74(t8)
    dli     t4, 0x1<<14
    or      t3, t3, t4
    sw      t3, 0x74(t8)

    lw      t3, 0xb4(t8)
    dli     t4, 0x1<<14
    or      t3, t3, t4
    sw      t3, 0xb4(t8)
#endif

//set soft rest_en
    lhu     t3, (PHY_ADDRESS)(t8)
    bne     t3, 0x11, 1f
    nop

    lw      t3, (DLL_CTRL)(t8)
    dli     t4, 0x404040
    or      t3, t3, t4
    sw      t3, (DLL_CTRL)(t8)
1:

    lb      t3, (DLL_CTRL)(t8)
    lb      t3, (DLL_CTRL)(t8)
    lb      t3, (DLL_CTRL)(t8)
    lb      t3, (DLL_CTRL)(t8)
    lb      t3, (DLL_CTRL)(t8)

    ld      t3, (PHY_ADDRESS + 0x010)(t8)
    ori     t3, (0x1 << 1)     //phy_init_start
    sd      t3, (PHY_ADDRESS + 0x010)(t8)

//dll init done check
1:
    ld      t3, (PHY_ADDRESS + 0x030)(t8)
    dli     t4, (0x1 << 40)
    and     t3, t3, t4
    beqz    t3, 1b
    nop

dll_lockcheck:
//get MC version
    lhu     t3, (PHY_ADDRESS)(t8)
    bne     t3, 0x11, 2f
    nop

//dll resetn
    lw      t3, (DLL_CTRL)(t8)
    dli     t4, 0x7f7f7f
    and     t3, t3, t4
    sw      t3, (DLL_CTRL)(t8)

    dli     t4, WAIT_ITEM
1:
    dsubu   t4, 1
    bnez    t4, 1b
    nop
//clear dll resetn
    lw      t3, (DLL_CTRL)(t8)
    dli     t4, 0x808080
    or      t3, t3, t4
    sw      t3, (DLL_CTRL)(t8)

    lbu     t3, (DLL_CTRL)(t8)
    lbu     t3, (DLL_CTRL)(t8)
    lbu     t3, (DLL_CTRL)(t8)
    lbu     t3, (DLL_CTRL)(t8)
    lbu     t3, (DLL_CTRL)(t8)

2:
    li      t6,0x100000
1:
    lb      t3, 0x4a(t8)
    andi    t3, t3, 0x1
    lb      t1, 0x7a(t8)
    andi    t1, t1, 0x1
    lb      t2, 0xba(t8)
    andi    t2, t2, 0x1
    and     t1, t1, t2
    and     t3, t1, t3
    subu    t6, 1
    beqz    t6, 1f
    nop

    beqz    t3, 1b
    nop

1:
    beqz    t3, dll_lockcheck
    nop


set_clken:
#if 1 // DCC_CTR
//bypass clk2x dcc
    lw      t3, 0x44(t8)
    dli     t4, (0x1 << 14)
    not     t4
    and     t3, t4
    sw      t3, 0x44(t8)
//bypass clk2x dcc
    lw      t3, 0x74(t8)
    dli     t4, (0x1 << 14)
    not     t4
    and     t3, t4
    sw      t3, 0x74(t8)
//bypass clk2x dcc
    lw      t3, 0xb4(t8)
    dli     t4, (0x1 << 14)
    not     t4
    and     t3, t4
    sw      t3, 0xb4(t8)

	WAIT_FOR(WAIT_ITEM)
#endif

//clk2x enable
    ld      t3, 0x48(t8)
    ori     t3, t3, (1 << 8)
    sd      t3, 0x48(t8)
    ld      t3, 0x78(t8)
    ori     t3, t3, (1 << 8)
    sd      t3, 0x78(t8)
    ld      t3, 0xb8(t8)
    ori     t3, t3, (1 << 8)
    sd      t3, 0xb8(t8)

//pm_init_start
    ld      t3, (PHY_ADDRESS + 0x010)(t8)
    ori     t3, t3, 0x1         //init_start
    sd      t3, (PHY_ADDRESS + 0x010)(t8)

//dram_init_chk
1:
    ld      t3, (CTL_ADDRESS + 0x100)(t8)
    andi    t1, t3, 0xff     //cs_enable
    ld      t3, (PHY_ADDRESS + DRAM_INIT)(t8)
    andi    t3, t3, 0xff00
    dsrl    t3, t3, 0x8
    bne     t3, t1, 1b
    nop

    GET_DIMM_TYPE_V1
    andi    a1, a1, 1
    beqz    a1, rcd_configdone
    nop

#ifndef DDR3_DIMM
//RDIMM
//set mirror and RDIMM snoop in F0RC0D in RCD
    GET_ADDR_MIRROR_V1
    beqz    a1, 1f
    nop

    li      mrs_cmd_a, 0xdc
    b       2f
    nop
1:
    li      mrs_cmd_a, 0xd4
2:
    RDIMM_MR7_SEND(mrs_cmd_a)

//set CA CS drive strength in F0RC03 in RCD
    GET_SPD(0x89)
    dsrl    mrs_cmd_a, v0, 4
    or      mrs_cmd_a, 0x30
    RDIMM_MR7_SEND(mrs_cmd_a)

//set ODT CKE drive strength in F0RC04 in RCD
    GET_SPD(0x89)
    and     mrs_cmd_a, v0, 3
    sll     mrs_cmd_a, 2
    srl     v0, 2
    andi    v0, 0x3
    or      mrs_cmd_a, v0
    or      mrs_cmd_a, 0x40
    RDIMM_MR7_SEND(mrs_cmd_a)

//set Clock drive strength in F0RC05 in RCD
    GET_SPD(0x8a)
    and     mrs_cmd_a, v0, 3
    sll     mrs_cmd_a, 2
    srl     v0, 2
    andi    v0, 0x3
    or      mrs_cmd_a, v0
    or      mrs_cmd_a, 0x50
    RDIMM_MR7_SEND(mrs_cmd_a)

//set RDIMM operate speed in F0RC0A in RCD
    dli     a0, 500
    dsll    a0, 2
    bgtu    a0, 1600, 1f
    nop
    dli     a0, 0
    b       2f
    nop
1:
    bgtu    a0, 1867, 1f
    nop
    dli     a0, 1
    b       2f
    nop
1:
    bgtu    a0, 2134, 1f
    nop
    dli     a0, 2
    b       2f
    nop
1:
    bgtu    a0, 2400, 1f
    nop
    dli     a0, 3
    b       2f
    nop
1:
    bgtu    a0, 2667, 1f
    nop
    dli     a0, 4
    b       2f
    nop
1:
    dli     a0, 0x5
2:
#if (SEL_DBL ==1)
    dsrl    v0, s5, 32
    and     v0, 1
    beqz    v0, 1f
    nop
    /* bypass RCD pll */
    GET_DBL_DCC_TRAIN_CURRENT_LOOP_NUM
    bgeu    v0, DBL_DCC_TRAIN_LOOP_TIMES, 1f
    nop
    dli     a0, 7
1:
#endif
    or      mrs_cmd_a, a0, 0xa0
    RDIMM_MR7_SEND(mrs_cmd_a)

//set RDIMM fine granularity of RDIMM operating speed in RC3x in RCD
    dli     a0, 500
    dsll    a0, 2
    bgtu    a0, 1240, 1f
    nop
    dli     a0, 0
    b       2f
    nop
1:
    dsubu   a0, 1240
    ddivu   a0, a0, 20
    mfhi    t0
    bnez    t0, 2f
    nop
    dsubu   a0, 1
2:
    or      mrs_cmd_a, a0, 0x300
    RDIMM_MR7_SEND(mrs_cmd_a)

    /* power saving setting */
    /* F0RC09*/
    GET_CS_NUM_DDR4
    bgtu    v0, 1, 1f
    nop
    dli     mrs_cmd_a, 0x93
    RDIMM_MR7_SEND(mrs_cmd_a)
1:
    /* F0RC08*/
    dli     mrs_cmd_a, 0x84
    /* disable QC[2:0] */
    dli     a1, 0x3
    GET_CS_NUM_DDR4
    bne     v0, 4, 1f
    nop
    and     a1, 0x1
1:
    or      mrs_cmd_a, a1
    /* disable DA17 */
    GET_ROW_SIZE_V1
    beqz    a1, 1f
    nop
    or      mrs_cmd_a, 0x8
1:
    RDIMM_MR7_SEND(mrs_cmd_a)
    /* F0RC02 */
    dli     mrs_cmd_a, 0x22
    GET_ROW_SIZE_V1
    beqz    a1, 1f
    nop
    or      mrs_cmd_a, 0x1
1:
    RDIMM_MR7_SEND(mrs_cmd_a)

    /*send mrs to side B for rdimm*/
    /*get cs number in t1 */
    GET_CS_NUM_DDR4
    move    t1, v0

    /*mrs send for different cs loop*/
    li      t0, 0       //cs number ctrl
5:
    /*mrs send for different mrs loop, mrs_num followring thr ddr init sequence:3,6,5,4,2,1,0*/
    li      t2, 0       //mrs number ctrl
2:
    dsll    t3, t0, 4
    daddu   t3, t8
    /*translate mrs_number ctrl t2 to mrs_num */
    bnez    t2, 1f
    nop
    li      mrs_num, 3
    b       3f
    nop
1:
    subu    mrs_num, t2, 7
    abs     mrs_num
    bleu    t2, 3, 3f
    nop
    subu    mrs_num, 1
3:
    dsll    t4, mrs_num, 1
    daddu   t3, t4
    lh      mrs_cmd_a, DDR4_MR0_CS0_REG(t3)
    move    mrs_cs, t0
    MRS_SEND(mrs_cmd_a,mrs_cs,mrs_num)
    /*mrs send for different mrs loop ctrl*/
    daddu   t2, 1
    bleu    t2, 6, 2b
    nop

    /*mrs send for different cs loop ctrl */
    daddu   t0, 1
    bltu    t0, t1, 5b
    nop

    //sync time
    ld      t3, (PHY_ADDRESS + 0x010)(t8)
    ld      t3, (PHY_ADDRESS + 0x010)(t8)
    ld      t3, (PHY_ADDRESS + 0x010)(t8)

#endif

rcd_configdone:
#if (SEL_DBL ==1)
    daddu   k1, k1, 0x1
    dsrl    v0, s5, 32
    and     v0, 1
    beqz    v0, 4f
    nop
    GET_DBL_DCC_TRAIN_CURRENT_LOOP_NUM
    bgeu    v0, DBL_DCC_TRAIN_LOOP_TIMES, 2f
    nop
    beqz    v0, 1f
    nop
#ifdef DBL_CK_TRAINING_DEBUG
    PRINTSTR("\r\nThe ")
    GET_DBL_DCC_TRAIN_CURRENT_LOOP_NUM
    move    a0, v0
    DDR_TTYBYTE
    PRINTSTR("th loop ")
#endif
1:
#ifdef DBL_CK_TRAINING_DEBUG
    PRINTSTR("\r\np = ")
    GET_DBL_DCC_TRAIN_CURRENT_DCC(P)
    move    a0, v0
    DDR_TTYBIT
    PRINTSTR(", n = ")
    GET_DBL_DCC_TRAIN_CURRENT_DCC(N)
    move    a0, v0
    DDR_TTYBIT
#endif
    /* DBL CLK training */
    bal     dbl_clk_training
    nop

    /* mc reset */
    move    a1, k0
    bal     enable_mc_reset
    nop
    WAIT_FOR(0x30000)
    move    a1, k0
    bal     disable_mc_reset
    nop

    /* loop control */
    GET_DBL_DCC_TRAIN_CURRENT_LOOP_NUM
    bnez    v0, mcreset
    nop

    /* dcc couple loop ctrl */
    GET_DBL_DCC_TRAIN_CURRENT_DCC(N)
    beq     v0, 7, 1f
    nop
    daddu   v0, 1
    STORE_DBL_DCC_TRAIN_CURRENT_DCC(N)
    b       mcreset
    nop
1:
    GET_DBL_DCC_TRAIN_CURRENT_DCC(P)
    beq     v0, 1, 1f
    nop
    daddu   v0, 1
    STORE_DBL_DCC_TRAIN_CURRENT_DCC(P)
    dli     v0, 0
    STORE_DBL_DCC_TRAIN_CURRENT_DCC(N)
    b       mcreset
    nop

    /* dcc couple loop end */
1:
    dli     v0, 1
    STORE_DBL_DCC_TRAIN_CURRENT_LOOP_NUM
    dli     t1, 0
    GET_DBL_DCC_TRAIN_STORED_DCC(t1)
    STORE_DBL_DCC_TRAIN_CURRENT_DCC_VALUE
    b       mcreset
    nop
2:
#if 0
    PRINTSTR("\r\ns5 = ")
    dsrl    a0, s5, 32
    bal     hexserial
    nop
    PRINTSTR("_")
    move    a0, s5
    bal     hexserial
    nop

    PRINTSTR(", final result is : p = ")
    GET_DBL_DCC_TRAIN_CURRENT_DCC(P)
    move    a0, v0
    DDR_TTYBIT
    PRINTSTR(", n = ")
    GET_DBL_DCC_TRAIN_CURRENT_DCC(N)
    move    a0, v0
    DDR_TTYBIT
#endif

    GET_DBL_DCC_TRAIN_CURRENT_DCC(P)
    move    t0, v0
    GET_DBL_DCC_TRAIN_CURRENT_DCC(N)
    move    t1, v0
    dsll    t0, t0, 4
    or      t0, t0, t1
    bne     t0, 0x07, 3f
    nop

    WatchDog_Enable
    PRINTSTR("\r\n p = 0, n = 7")
11:
    b       11b
    nop

3:
    /* store dbl clk dcc */
    /* t3:save sdram base */
    GET_NODE_ID_a1
    mul     a1, DIMM_INFO_SIZE
    dli     t3, DIMM_INFO_IN_CACHE_OFFS
    daddu   t3, a1
    mul     a0, k0, MC_INFO_SIZE	//if k0==1, add mc1 offset
    daddu   t3, a0
    lb      t0, DDR4_DBL_CTRL_CKCA_OFFSET(t8)
    dsrl    t0, 2
    and     v0, t0, 7
    and     v1, t0, 7<<3
    dsll    v1, 1
    or      v0, v1
    bne     v0, 0x17, 1f
    nop
    dsubu   v0, v0, 0x14
1:
    daddu   v0, 1
    sb      v0, DIMM_OFFS_DBL_CFG(t3)

    PRINTSTR("\r\n")
    PRINTSTR("Final result is : p = ")
    lb      v0, DIMM_OFFS_DBL_CFG(t3)
    dsrl    a0, v0, 4
    and     a0, 0xf
    DDR_TTYBIT
    PRINTSTR(", n = ")
    lb      v0, DIMM_OFFS_DBL_CFG(t3)
    and     a0, v0, 0xf
    DDR_TTYBIT
    b       1f
    nop

4:
    /* store dbl clk dcc */
    /* t3:save sdram base */
    GET_NODE_ID_a1
    mul     a1, DIMM_INFO_SIZE
    dli     t3, DIMM_INFO_IN_CACHE_OFFS
    daddu   t3, a1
    mul     a0, k0, MC_INFO_SIZE	//if k0==1, add mc1 offset
    daddu   t3, a0
    lb      t0, DDR4_DBL_CTRL_CKCA_OFFSET(t8)
    dsrl    t0, 2
    and     v0, t0, 7
    and     v1, t0, 7<<3
    dsll    v1, 1
    or      v0, v1
    sb      v0, DIMM_OFFS_DBL_CFG(t3)
1:
#endif

    move    ra, t9
    jr      ra
    nop
END(mc_clk_train)
