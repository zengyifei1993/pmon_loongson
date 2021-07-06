#define DBL_CLK_SAMPLE_DS0                  0x3300
#define DBL_CLK_TRAIN_TRANSITION_NUMBER     0x0
#define DBL_CLK_TRAIN_2ND_COUNT             0x1
#define DBL_CLK_TRAIN_3RD_COUNT             0x2
#define DBL_CLK_TRAIN_4TH_COUNT             0x3
#define DBL_CLK_TRAIN_5TH_COUNT             0x4
#define DBL_CLK_TRAIN_CURRENT_COUNT         0x5
#define DBL_CLK_TRAIN_CURRENT_VALUE         0x6
#define DBL_CLK_TRAIN_PASS_FLAG             0x0
#define DBL_CLK_TRAIN_2PERIODS_SUB          0x1
/* DS0-DS8, period store for 2 cycles
 * -----------------------------------------------------------------------------------------------------------------------------
 * 0x3300-0x3340        |    [55:48]    |    [47:40]    |  [39:32]  |  [31:24]  |  [23:16]  |   [15: 8]    |      [ 7: 0]      |
 * -----------------------------------------------------------------------------------------------------------------------------
 * clk_sample_scan      | current_value | current_count | 5th_count | 4th_count | 3rd_count |   2nd_count  | tranisiton_number |
 * -----------------------------------------------------------------------------------------------------------------------------
 * check appropriate DS |               |               |           |           |           | 2periods_sub |      pass_flag    |
 * -----------------------------------------------------------------------------------------------------------------------------
 */

/* cache stored for DBL_DCC_TRAINING_STORED_NUM smallest dcc couples, from DBL_DCC_TRANING_OFFS to (DBL_DCC_TRANING_OFFS + DBL_DCC_TRAINING_STORED_NUM*8)
 * -----------------------------------------------------------------------------------------------------------------------------------------
 * DBL_DCC_TRANING_OFFS - (DBL_DCC_TRANING_OFFS + DBL_DCC_TRAINING_STORED_NUM*8)  |  [47:40]  |  [39:32]  |   [31:16]   |      [15: 0]     |
 * -----------------------------------------------------------------------------------------------------------------------------------------
 * detail                                                                         |   dcc_p   |   dcc_n   | ds_pass_sum | 2periods_sub_sum |
 * -----------------------------------------------------------------------------------------------------------------------------------------
 */
#define period_sub      t0
#define ds_sum          t4
//#if (DDR_FREQ == 533)
#define FINISH_FLAG 5
//#else
//#define FINISH_FLAG 3
//#endif
#define LVL_REPEAT_NUM  10
/********************************
s5 should be init at beginning of mc_config.S
don't change s5 except refreshing the value
s5 :
    [32:32] training flag, 1-training, 0-no training
    [31:24] scaned effect number of dcc couples
    [24:21] current dcc p value
    [20:16] current dcc n value
    [15: 8] current loop number
    [ 7: 0] current count number in one loop
********************************/

//#define DBL_CK_TRAINING_DEBUG_DETAIL
//#define DBL_CK_TRAINING_PATTERN_DEBUG

    .text
    .set    noreorder
    .set    mips3
    .global dbl_clk_training
    .ent    dbl_clk_training
dbl_clk_training:
    move    t5, ra

    GET_DBL_DCC_TRAIN_CURRENT_LOOP_NUM
    beqz    v0, 1f
    nop
    GET_DBL_DCC_TRAIN_CURRENT_CNT_IN_LOOP_NUM
    bnez    v0, 2f
    nop
    PRINTSTR(".")
    b       2f
    nop
1:
    GET_DBL_DCC_TRAIN_CURRENT_DCC(N)
    bnez    v0, 2f
    nop
    PRINTSTR(".")
2:
#ifdef  DBL_CK_TRAINING_DEBUG
    PRINTSTR("\r\ns5 = ")
    move    a0, s5
    bal     ddr_hexserial64
    nop
#endif

#if 0//def  DBL_CK_TRAINING_DEBUG
    bal     clk_pattern_print
    nop
#endif

    /* init dll value */
    GET_SLICE_NUM
    move    t3, v0
    dli     t0, 0
1:
    dsll    t1, t0, 7
    daddu   t1, t8
#if (FINISH_FLAG == 3)
    dli     v0, 0x80002080
#elif   (FINISH_FLAG == 5)
    dli     v0, 0x80008080
#endif
    sd      v0, DDR4_DLL_WRDQ_OFFSET(t1)
    sb      zero, WRDQS0_BDLY_OFFSET(t1)
    daddu   t0, 1
    bltu    t0, t3, 1b
    nop

    /* init stored value */
    dli     t0, 0
1:
    dsll    t1, t0, 3
    daddu   t1, t8
    sd      zero, DBL_CLK_SAMPLE_DS0(t1)
    daddu   t0, 1
    bltu    t0, 9, 1b
    nop

    /* enable wrlvl mode */
    GET_CS_NUM_DDR4
    move    t2, v0      //cs number in t2
    GET_LVL_CS_NUM
    move    t4, v0      //lvl cs in t4

    /*set DQ off for all cs other than lvl cs loop */
    dli     t0, 0
1:
    beq     t0, t4, 2f
    nop
    /*set DQ off*/
#ifndef DDR3_DIMM
    dsll    t1, t0, 4
    daddu   t1, t8
    lh      mrs_cmd_a, DDR4_MR1_CS0_REG(t1)
#else
    dsll    t1, t0, 3
    daddu   t1, t8
    lh      mrs_cmd_a, DDR3_MR1_CS0_REG(t1)
#endif
    or      mrs_cmd_a, (1<<12)
    move    mrs_cs, t0
    li      mrs_num, 1
    MRS_SEND(mrs_cmd_a,mrs_cs,mrs_num)
2:
    daddu   t0, 1
    bltu    t0, t2, 1b
    nop

#ifndef DDR3_DIMM
    /*enable write lvl to side B for rdimm*/
    GET_LVL_CS_NUM
    move    mrs_cs, v0
    dsll    t1, v0, 4
    daddu   t1, t8
    lh      mrs_cmd_a, DDR4_MR1_CS0_REG(t1)
    or      mrs_cmd_a, (1<<7)
    li      mrs_num, 1
    MRS_SEND(mrs_cmd_a,mrs_cs,mrs_num)
#endif

    dli     v0, 1
    sb      v0, LVL_MODE_OFFSET(t8)
1:
    lb      v0, LVL_RDY_OFFSET(t8)
    beqz    v0, 1b
    nop

    /* scan DBL_CK */
/********************************************************************
t0 : dll value, 1st dll_wrdq, 2nd dll_wrdqs, 3rd wrdqs_bdly
t1 : slice num reg
t2 : elastic reg
t3 : total slice number
t4 : DQ/DQS/BDLY select, 0-DQ, 1-BDLY_DQS, 2-DQS
********************************************************************/
    dli     t0, 0x80
    dli     t4, 0
dbl_clk_scan_loop:
    dli     t1, 0
1:
    dsll    t2, t1, 7
    daddu   t2, t8
    and     v0, t0, 0xff
    bnez    t4, 2f
    nop
    sb      v0, DDR4_DLL_WRDQ_OFFSET(t2)
    sb      v0, DLL_1XDLY_OFFSET(t2)
    b       3f
    nop
2:
#if (FINISH_FLAG == 5)
    beq     t4, 2, 2f
    nop
#endif
    sb      v0, WRDQS0_BDLY_OFFSET(t2)
#if (FINISH_FLAG == 5)
    b       3f
    nop
2:
    sb      v0, DLL_WRDQS_OFFSET(t2)
#endif
3:
#if 0//def  DBL_CK_TRAINING_DEBUG
    PRINTSTR("\r\n0x")
    dsll    t2, t1, 7
    dadd    a0, t2, DDR4_DLL_WRDQ_OFFSET
    bal     hexserial
    nop
    PRINTSTR(" : ")
    daddu   v1, t2, t8
    ld      a0, DDR4_DLL_WRDQ_OFFSET(v1)
    bal     ddr_hexserial64
    nop
    PRINTSTR("\r\n0x")
    dadd    a0, t2, WRDQS0_BDLY_OFFSET
    bal     hexserial
    nop
    PRINTSTR(" : ")
    daddu   v1, t2, t8
    lb      a0, WRDQS0_BDLY_OFFSET(v1)
    DDR_TTYBIT
#endif
    daddu   t1, 1
    bltu    t1, t3, 1b
    nop

    WAIT_FOR(0x30000)
    /* store result */
    dli     t1, 0
3:
    daddu   v1, t1, t8

    /* lvl 5 times. if sum >3, then lvl result is 1, else 0 */
    dli     a0, 0
    dli     t2, 0
2:
    dli     v0, 1
    sb      v0, LVL_REQ_OFFSET(t8)

1:
    lb      v0, LVL_DONE_OFFSET(t8)
    beqz    v0, 1b
    nop

    lb      v0, LVL_RESP_OFFSET(v1)
    and     v0, 1
    daddu   a0, v0
    daddu   t2, 1
    bltu    t2, LVL_REPEAT_NUM, 2b
    nop
    dli     v0, 0
    bltu    a0, (LVL_REPEAT_NUM - 1), 1f
    nop
    daddu   v0, 1
1:
#ifdef  DBL_CK_TRAINING_PATTERN_DEBUG
    move    t2, v0
    move    a0, v0
    DDR_TTYBIT
    move    v0, t2
#endif
    dsll    t2, t1, 3
    daddu   t2, t8
    /* check current slice finished */
    lb      v1, (DBL_CLK_TRAIN_TRANSITION_NUMBER + DBL_CLK_SAMPLE_DS0)(t2)
    beq     v1, FINISH_FLAG, 2f
    nop
    /* check start position */
    /* first store start value */
    lb      v1, (DBL_CLK_TRAIN_TRANSITION_NUMBER + DBL_CLK_SAMPLE_DS0)(t2)
    bnez    v1, 1f
    nop
    lb      v1, (DBL_CLK_TRAIN_CURRENT_COUNT + DBL_CLK_SAMPLE_DS0)(t2)
    bnez    v1, 1f
    nop
    /* refresh current value */
    sb      v0, (DBL_CLK_TRAIN_CURRENT_VALUE + DBL_CLK_SAMPLE_DS0)(t2)
    dli     v1, 1
    sb      v1, (DBL_CLK_TRAIN_CURRENT_COUNT + DBL_CLK_SAMPLE_DS0)(t2)
    b       2f
    nop
1:
    /* check transition */
    /* if check transition pass, store current value, current count and trainsition number add 1 */
    lb      v1, (DBL_CLK_TRAIN_CURRENT_VALUE + DBL_CLK_SAMPLE_DS0)(t2)
    beq     v0, v1, 1f
    nop
    /* check transition pass */
    /* refresh current value */
    sb      v0, (DBL_CLK_TRAIN_CURRENT_VALUE + DBL_CLK_SAMPLE_DS0)(t2)
    /* store current count number */
    lb      v1, (DBL_CLK_TRAIN_TRANSITION_NUMBER + DBL_CLK_SAMPLE_DS0)(t2)
    daddu   v1, 1
    sb      v1, (DBL_CLK_TRAIN_TRANSITION_NUMBER + DBL_CLK_SAMPLE_DS0)(t2)
    beq     v1, 1, 4f
    nop
    lb      v0, (DBL_CLK_TRAIN_CURRENT_COUNT + DBL_CLK_SAMPLE_DS0)(t2)
    dsubu   v1, 2
    daddu   v1, t2
    sb      v0, (DBL_CLK_TRAIN_2ND_COUNT + DBL_CLK_SAMPLE_DS0)(v1)
4:
    dli     v1, 1
    sb      v1, (DBL_CLK_TRAIN_CURRENT_COUNT + DBL_CLK_SAMPLE_DS0)(t2)
    b       2f
    nop
1:
    /* check transition fail */
    lb      v0, (DBL_CLK_TRAIN_CURRENT_COUNT + DBL_CLK_SAMPLE_DS0)(t2)
    daddu   v0, 1
    sb      v0, (DBL_CLK_TRAIN_CURRENT_COUNT + DBL_CLK_SAMPLE_DS0)(t2)
2:
    daddu   t1, 1
    bltu    t1, t3, 3b
    nop
#ifdef  DBL_CK_TRAINING_PATTERN_DEBUG
    PRINTSTR("\r\n")
#endif

    /* DBL_CK scan loop ctrl */
    /* check if all dataslice has finished */
    dli     t1, 0
    dli     v0, FINISH_FLAG
1:
    dsll    t2, t1, 3
    daddu   t2, t8
    /* check current slice finished */
    lb      v1, (DBL_CLK_TRAIN_TRANSITION_NUMBER + DBL_CLK_SAMPLE_DS0)(t2)
    and     v0, v1
    daddu   t1, 1
    bltu    t1, t3, 1b
    nop
    /* check all DS finished pass */
    beq     v0, FINISH_FLAG, 2f
    nop
    /* check all DS finished fail */
    daddu   t0, 1
    bnez    t4, 1f
    nop
    bltu    t0, 0x100, dbl_clk_scan_loop
    nop
    daddu   t4, 1
    dli     t0, 0x1
1:
#if (FINISH_FLAG == 5)
    beq     t4, 2, 1f
    nop
#endif
    bltu    t0, 0x10, dbl_clk_scan_loop
    nop
#if (FINISH_FLAG == 5)
    daddu   t4, 1
    dli     t0, 0x81
1:
    bleu    t0, 0x9e, dbl_clk_scan_loop
    nop
#endif
2:
    /* DBL CLK scan finished */
#ifdef  DBL_CK_TRAINING_DEBUG_DETAIL
    PRINTSTR("\r\nDBL scan finished")
    dli     t1, 0
1:
    dsll    t2, t1, 3
    PRINTSTR("\r\n0x")
    daddu   a0, t2, DBL_CLK_SAMPLE_DS0
    bal     hexserial
    nop
    PRINTSTR(" : ")
    daddu   t2, t8
    ld      a0, DBL_CLK_SAMPLE_DS0(t2)
    bal     ddr_hexserial64
    nop
    daddu   t1, 1
    bltu    t1, t3, 1b
    nop
#endif
    /* disable wrlvl mode */
    dli     v0, 0
    sb      v0, LVL_MODE_OFFSET(t8)
    sb      v0, LVL_REQ_OFFSET(t8)

#ifndef DDR3_DIMM
    /*disable write lvl to side B for rdimm*/
    GET_LVL_CS_NUM
    move    mrs_cs, v0
    dsll    t1, v0, 4
    daddu   t1, t8
    lh      mrs_cmd_a, DDR4_MR1_CS0_REG(t1)
    and     mrs_cmd_a, ~(1<<7)
    li      mrs_num, 1
    MRS_SEND(mrs_cmd_a,mrs_cs,mrs_num)
#endif
    WAIT_FOR(20000)

    GET_CS_NUM_DDR4
    move    t2, v0      //cs number in t2

    /*set DQ on for all cs other than lvl cs loop */
    dli     t0, 0
1:
    /*set DQ on*/
#ifndef DDR3_DIMM
    dsll    t1, t0, 4
    daddu   t1, t8
    lh      mrs_cmd_a, DDR4_MR1_CS0_REG(t1)
#else
    dsll    t1, t0, 3
    daddu   t1, t8
    lh      mrs_cmd_a, DDR3_MR1_CS0_REG(t1)
#endif
    and     mrs_cmd_a, ~(1<<12)
    move    mrs_cs, t0
    li      mrs_num, 1
    MRS_SEND(mrs_cmd_a,mrs_cs,mrs_num)
    daddu   t0, 1
    bltu    t0, t2, 1b
    nop
    /* check wheter one or two whole periods have been scaned */
/******************************************
t0 : sum of 2 periods sub
t1 : slice num reg
t2 : elastic reg
t3 : total slice number
t4 : number of dataslice pass
******************************************/
    /* check pass set pass_flag 1 */
    dli     period_sub, 0
    dli     ds_sum, 0
    dli     t1, 0
3:
    dsll    t2, t1, 3
    daddu   t2, t8
    lb      v1, (DBL_CLK_TRAIN_TRANSITION_NUMBER + DBL_CLK_SAMPLE_DS0)(t2)
    beq     v1, FINISH_FLAG, 1f
    nop
    sb      zero, (DBL_CLK_TRAIN_PASS_FLAG + DBL_CLK_SAMPLE_DS0)(t2)
    b       2f
    nop
1:
    dli     v1, 1
    sb      v1, (DBL_CLK_TRAIN_PASS_FLAG + DBL_CLK_SAMPLE_DS0)(t2)
    lb      v0, (DBL_CLK_TRAIN_2ND_COUNT + DBL_CLK_SAMPLE_DS0)(t2)
    lb      v1, (DBL_CLK_TRAIN_3RD_COUNT + DBL_CLK_SAMPLE_DS0)(t2)
    daddu   v0, v1
#if (FINISH_FLAG == 5)
    lb      v1, (DBL_CLK_TRAIN_4TH_COUNT + DBL_CLK_SAMPLE_DS0)(t2)
    lb      a0, (DBL_CLK_TRAIN_5TH_COUNT + DBL_CLK_SAMPLE_DS0)(t2)
    daddu   v1, a0
#elif   (FINISH_FLAG == 3)
    lb      v1, DLL_VALUE_OFFSET(t8)
#endif
    dsubu   v1, v0
    abs     v1
    sb      v1, (DBL_CLK_TRAIN_2PERIODS_SUB + DBL_CLK_SAMPLE_DS0)(t2)
	/* check outliers, if sub >0x14, disable pass_flag */
	bleu	v1, 0x14, 1f
	nop
    sb      zero, (DBL_CLK_TRAIN_PASS_FLAG + DBL_CLK_SAMPLE_DS0)(t2)
1:
    daddu   period_sub, v1
    daddu   ds_sum, 1
2:
    daddu   t1, 1
    bltu    t1, t3, 3b
    nop

#ifdef  DBL_CK_TRAINING_DEBUG
    PRINTSTR("\r\nDBL check finished")
    dli     t1, 0
1:
    dsll    t2, t1, 3
    PRINTSTR("\r\n0x")
    daddu   a0, t2, DBL_CLK_SAMPLE_DS0
    bal     hexserial
    nop
    PRINTSTR(" : ")
    daddu   t2, t8
    ld      a0, DBL_CLK_SAMPLE_DS0(t2)
    bal     ddr_hexserial64
    nop
    daddu   t1, 1
    bltu    t1, t3, 1b
    nop
    PRINTSTR("\r\nsum of 2 periods sub = ")
    move    a0, period_sub
    bal     hexserial
    nop
    PRINTSTR("\r\nsum of ds pass = ")
    and     a0, ds_sum, 0xff
    DDR_TTYBYTE
#endif

/******************************************
t0 : sum of 2 periods sub
t1-t3 : elastic reg
t4 : number of dataslice pass
******************************************/
    /* check whether to scan 8*8 dcc couples */
    GET_DBL_DCC_TRAIN_CURRENT_LOOP_NUM
    bnez    v0, 2f
    nop
    /* scan 8*8 dcc couples */
    beqz    ds_sum, 10f
    nop
    GET_DBL_DCC_TRAIN_EFFECT_NUM
    daddu   v0, 1
    STORE_DBL_DCC_TRAIN_EFFECT_NUM
    /* reorder DBL_DCC_TRAIN_STORED_NUM smallest dcc couples */
    dli     t2, DBL_DCC_TRAIN_STORED_NUM
1:
    dsubu   t2, 1
    bltz    t2, 1f
    nop
    /* compare current dcc couple with t2 dcc couple */
    GET_DBL_DCC_TRAIN_STORED_PERIOD_SUB(t2)
    move    a2, v0
    GET_DBL_DCC_TRAIN_STORED_DS_SUM(t2)
    move    a3, v0
    dmulou  a0, ds_sum, a2  //t2 dcc couple
    dmulou  a1, a3, period_sub //curret dcc couple
    bltu    a1, a0, 1b //current<stored data in t2 position
    nop
    beq     t2, (DBL_DCC_TRAIN_STORED_NUM - 1), 10f
    nop
1:
    daddu   t1, t2, 1
    /* exchange data, e.t. current->6->7->8.... */
    dli     t2, (DBL_DCC_TRAIN_STORED_NUM - 1)
1:
    dsubu   t3, t2, 1
    GET_DBL_DCC_TRAIN_STORED_DATA(t3)
    move    a0, v0
    STORE_DBL_DCC_TRAIN_DATA(t2,a0)
    dsubu   t2, 1
    bgtu    t2, t1, 1b
    nop
1:
    CONFIG_DBL_DCC_TRAIN_DATA(ds_sum, period_sub)
    move    a0, v0
    STORE_DBL_DCC_TRAIN_DATA(t1,a0)
    b       3f
    nop
2:
    /* scan 8*8 dcc couples finished, repeat loops */

    /* compare DBL_DCC_TRAIN_STORED_NUM with scaned effect number of dcc couples, set loop ctrl num t3 */
    dli     t3, DBL_DCC_TRAIN_STORED_NUM
    GET_DBL_DCC_TRAIN_EFFECT_NUM
    bgeu    v0, DBL_DCC_TRAIN_STORED_NUM, 1f
    nop
    move    t3, v0
1:
    /* store current training value */
    GET_DBL_DCC_TRAIN_CURRENT_CNT_IN_LOOP_NUM
    move    t1, v0
    GET_DBL_DCC_TRAIN_STORED_DS_SUM(t1)
    daddu   a1, ds_sum, v0
    STORE_DBL_DCC_TRAIN_DS_SUM(t1, a1)
    GET_DBL_DCC_TRAIN_STORED_PERIOD_SUB(t1)
    daddu   a1, period_sub, v0
    STORE_DBL_DCC_TRAIN_PERIOD_SUB(t1, a1)

    daddu   t1, 1
    bne     t1, t3, 1f
    nop
    dli     t1, 0
    GET_DBL_DCC_TRAIN_CURRENT_LOOP_NUM
    daddu   v0, 1
    STORE_DBL_DCC_TRAIN_CURRENT_LOOP_NUM
1:
    move    v0, t1
    STORE_DBL_DCC_TRAIN_CURRENT_CNT_IN_LOOP_NUM

    GET_DBL_DCC_TRAIN_STORED_DCC(t1)
    STORE_DBL_DCC_TRAIN_CURRENT_DCC_VALUE

    GET_DBL_DCC_TRAIN_CURRENT_LOOP_NUM
    bltu    v0, DBL_DCC_TRAIN_LOOP_TIMES, 3f
    nop

    /* compare dcc couples and set smallest as 1st dcc and current dcc */
    dli     t1, 0
    dli     t2, 1
2:
    GET_DBL_DCC_TRAIN_STORED_DS_SUM(t1)
    move    a0, v0
    GET_DBL_DCC_TRAIN_STORED_PERIOD_SUB(t1)
    move    a1, v0
    GET_DBL_DCC_TRAIN_STORED_DS_SUM(t2)
    move    a2, v0
    GET_DBL_DCC_TRAIN_STORED_PERIOD_SUB(t2)
    move    a3, v0
    dmulou  a0, a3 //t2 dcc couple
    dmulou  a1, a2 //t1 dcc couple
    /* t1<t2, next couple; t1>=t2, exchange t1 and t2 dcc couple data */
    bltu    a1, a0, 1f
    nop
    /* exchange t1 and t2 dcc couple data */
    GET_DBL_DCC_TRAIN_STORED_DATA(t1)
    move    a1, v0
    GET_DBL_DCC_TRAIN_STORED_DATA(t2)
    move    a0, v0
    STORE_DBL_DCC_TRAIN_DATA(t1,a0)
    STORE_DBL_DCC_TRAIN_DATA(t2,a1)
1:
    daddu   t2, 1
    bltu    t2, t3, 2b
    nop
    dli     t1, 0
    GET_DBL_DCC_TRAIN_STORED_DCC(t1)
    STORE_DBL_DCC_TRAIN_CURRENT_DCC_VALUE
3:
#ifdef  DBL_CK_TRAINING_DEBUG
    PRINTSTR("\r\nStored data changed, now is\r\n                   p, n,     ds,    sub")
    dli     t0, 0
1:
    PRINTSTR("\r\nDCC couple No.")
    move    a0, t0
    DDR_TTYBYTE
    PRINTSTR(" : ")
    GET_DBL_DCC_TRAIN_STORED_DCC(t0)
    move    t1, v0
    dsrl    a0, v0, 4
    DDR_TTYBIT
    PRINTSTR(", ")
    move    a0, t1
    DDR_TTYBIT
    PRINTSTR(", 0x")
    GET_DBL_DCC_TRAIN_STORED_DS_SUM(t0)
    move    a0, v0
    bal     ddr_hexserial16
    nop
    PRINTSTR(", 0x")
    GET_DBL_DCC_TRAIN_STORED_PERIOD_SUB(t0)
    move    a0, v0
    bal     ddr_hexserial16
    nop
    daddu   t0, 1
    bltu    t0, DBL_DCC_TRAIN_STORED_NUM, 1b
    nop
#endif

10:

    move    ra, t5
    jr      ra
    nop
    .end    dbl_clk_training
LEAF(clk_pattern_print)
/***************************
reg used: a0, a1, a2, v0, v1, t8
a2: [ 7: 0] dll value
    [15: 8] dataslice
    [23:16] DQ/DQS select 0-DQ 1-bdly_DQS 2-DQS
a3: ra store
v0, v1, a0, a1 elastic regs
do not change t8
***************************/
#define WRLVL_PRINT_DATASLICE_NUM   9
#if (FINISH_FLAG == 3)
#define WRLVL_PRINT_DLL_LEVEL_NUM   143
#elif   (FINISH_FLAG == 5)
#define WRLVL_PRINT_DLL_LEVEL_NUM   206
#endif
#define WRLVL_PRINT_DLL_LEVEL_START_NUM 0

    move    a3, ra

    /* init dll value of DQ/DQS */
    GET_SLICE_NUM
    move    a2, v0
    dli     a0, 0
1:
    dsll    a1, a0, 7
    daddu   a1, t8
#if (FINISH_FLAG == 3)
    dli     v0, 0x80002080
#elif   (FINISH_FLAG == 5)
    dli     v0, 0x80008080
#endif
    sd      v0, DDR4_DLL_WRDQ_OFFSET(a1)
    sb      zero, WRDQS0_BDLY_OFFSET(a1)
    daddu   a0, 1
    bltu    a0, a2, 1b
    nop

    /* enable wrlvl mode */

    /*set DQ off for all cs other than lvl cs loop */
    dli     t0, 0
1:
    GET_LVL_CS_NUM
    beq     t0, v0, 2f
    nop
    /*set DQ off*/
#ifndef DDR3_DIMM
    dsll    v1, t0, 4
    daddu   v1, t8
    lh      mrs_cmd_a, DDR4_MR1_CS0_REG(v1)
#else
    dsll    v1, t0, 3
    daddu   v1, t8
    lh      mrs_cmd_a, DDR3_MR1_CS0_REG(v1)
#endif
    or      mrs_cmd_a, (1<<12)
    move    mrs_cs, t0
    li      mrs_num, 1
    MRS_SEND(mrs_cmd_a,mrs_cs,mrs_num)
2:
    daddu   t0, 1
    GET_CS_NUM_DDR4
    bltu    t0, v0, 1b
    nop

#ifndef DDR3_DIMM
    /*enable write lvl to side B for rdimm*/
    GET_LVL_CS_NUM
    move    mrs_cs, v0
    dsll    v1, v0, 4
    daddu   v1, t8
    lh      mrs_cmd_a, DDR4_MR1_CS0_REG(v1)
    or      mrs_cmd_a, (1<<7)
    li      mrs_num, 1
    MRS_SEND(mrs_cmd_a,mrs_cs,mrs_num)
#endif

    dli     v0, 1
    sb      v0, LVL_MODE_OFFSET(t8)
1:
    lb      v0, LVL_RDY_OFFSET(t8)
    beqz    v0, 1b
    nop

    /* print dll level */
    dli     a2, 100
1:
    beq     a2, 1, 2f
    nop
    DDR_TTYSTRING("\r\n     ")
    b       3f
    nop
2:
    DDR_TTYSTRING("\r\ndll: ")
3:
    dli     a1, WRLVL_PRINT_DLL_LEVEL_START_NUM
4:
    ddivu   a0, a1, a2
    dremu   a0, 10
    DDR_TTYBIT
    daddu   a1, 1
    bltu    a1, (WRLVL_PRINT_DLL_LEVEL_START_NUM + WRLVL_PRINT_DLL_LEVEL_NUM), 4b
    nop
    ddivu   a2, 10
    bnez    a2, 1b
    nop
    DDR_TTYSTRING("\r\n")

    /* print wrlvl result loop */
    dli     a2, 0
11:
    DDR_TTYSTRING("\r\nDS")
    GET_REG_B(a2,1)
    move    a0, v0
    DDR_TTYBIT
    DDR_TTYSTRING(": ")

    dli     v0, 0x80
    STORE_REG_B(a2,0,v0)
10:
    GET_REG_B(a2,1)
    dsll    v1, v0, 7
    daddu   v1, t8
    GET_REG_B(a2,0)
    and     a1, v0, 0xff
    GET_REG_B(a2,2)
    bnez    v0, 1f
    nop
    sb      a1, DDR4_DLL_WRDQ_OFFSET(v1)
    sb      a1, DLL_1XDLY_OFFSET(v1)
    b       2f
    nop
1:
#if   (FINISH_FLAG == 5)
    beq     v0, 2, 1f
    nop
#endif
    sb      a1, WRDQS0_BDLY_OFFSET(v1)
#if   (FINISH_FLAG == 5)
    b       2f
    nop
1:
    sb      a1, DLL_WRDQS_OFFSET(v1)
#endif
2:
    WAIT_FOR(0x30000)

    GET_REG_B(a2,1)
    daddu   v1, v0, t8
    dli     a1, 0
    dli     v0, 0
    STORE_REG_B(a2,3,v0)
2:
    dli     v0, 1
    sb      v0, LVL_REQ_OFFSET(t8)

1:
    lb      v0, LVL_DONE_OFFSET(t8)
    beqz    v0, 1b
    nop
    lb      a0, LVL_RESP_OFFSET(v1)
    and     a0, 1
    daddu   a1, a0
    GET_REG_B(a2,3)
    daddu   v0, 1
    STORE_REG_B(a2,3,v0)
    GET_REG_B(a2,3)
    bltu    v0, LVL_REPEAT_NUM, 2b
    nop
    dli     a0, 0
    bltu    a1, (LVL_REPEAT_NUM - 1), 1f
    nop
    daddu   a0, 1
1:
    DDR_TTYBIT

    /* dll loop ctrl */
    GET_REG_B(a2,0)
    daddu   v0, 1
    STORE_REG_B(a2,0,v0)
    GET_REG_B(a2,2)
    bnez    v0, 1f
    nop
    GET_REG_B(a2,0)
    bnez    v0, 10b
    nop
    b       2f
    nop
1:
#if   (FINISH_FLAG == 5)
    beq     v0, 2, 1f
    nop
#endif
    GET_REG_B(a2,0)
    bltu    v0, 0x10, 10b
    nop
#if   (FINISH_FLAG == 5)
    b       2f
    nop
1:
    GET_REG_B(a2,0)
    bltu    v0, 0xc0, 10b
    nop
#endif
2:

    /* DQ/DQS loop ctrl */
    GET_REG_B(a2,2)
    daddu   v0, 1
    STORE_REG_B(a2,2,v0)
#if   (FINISH_FLAG == 5)
    GET_REG_B(a2,2)
    beq     v0, 2, 1f
    nop
#endif
    dli     v0, 0x1
#if   (FINISH_FLAG == 5)
    b       2f
    nop
1:
    dli     v0, 0x81
2:
#endif
    STORE_REG_B(a2,0,v0)
    GET_REG_B(a2,2)
#if   (FINISH_FLAG == 3)
    bleu    v0, 1, 10b
#elif   (FINISH_FLAG == 5)
    bleu    v0, 2, 10b
#endif
    nop

    /* slice loop ctrl */
    GET_REG_B(a2,1)
    daddu   v0, 1
    STORE_REG_B(a2,1,v0)
    dli     v0, 0
    STORE_REG_B(a2,2,v0)
    GET_SLICE_NUM
    move    v1, v0
    GET_REG_B(a2,1)
    bltu    v0, v1, 11b
    nop
    DDR_TTYSTRING("\r\n")

    /* disable wrlvl mode */
    dli     v0, 0
    sb      v0, LVL_MODE_OFFSET(t8)
    sb      v0, LVL_REQ_OFFSET(t8)

#ifndef DDR3_DIMM
    /*disable write lvl to side B for rdimm*/
    GET_LVL_CS_NUM
    move    mrs_cs, v0
    dsll    v1, v0, 4
    daddu   v1, t8
    lh      mrs_cmd_a, DDR4_MR1_CS0_REG(v1)
    and     mrs_cmd_a, ~(1<<7)
    li      mrs_num, 1
    MRS_SEND(mrs_cmd_a,mrs_cs,mrs_num)
#endif
    WAIT_FOR(20000)

    /*set DQ on for all cs other than lvl cs loop */
    dli     t0, 0
1:
    /*set DQ on*/
#ifndef DDR3_DIMM
    dsll    v1, t0, 4
    daddu   v1, t8
    lh      mrs_cmd_a, DDR4_MR1_CS0_REG(v1)
#else
    dsll    v1, t0, 3
    daddu   v1, t8
    lh      mrs_cmd_a, DDR3_MR1_CS0_REG(v1)
#endif
    and     mrs_cmd_a, ~(1<<12)
    move    mrs_cs, t0
    li      mrs_num, 1
    MRS_SEND(mrs_cmd_a,mrs_cs,mrs_num)
    daddu   t0, 1
    GET_CS_NUM_DDR4
    bltu    t0, v0, 1b
    nop

    move    ra, a3
    jr      ra
    nop
END(clk_pattern_print)
