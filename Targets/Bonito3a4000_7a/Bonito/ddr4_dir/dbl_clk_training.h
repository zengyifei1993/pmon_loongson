/* cache stored for DBL_DCC_TRAIN_STORED_NUM smallest dcc couples, from DBL_DCC_TRAIN_OFFS to (DBL_DCC_TRAIN_OFFS + DBL_DCC_TRAIN_STORED_NUM*8)
 * ----------------------------------------------------------------------------------------------------------------------------------
 * DBL_DCC_TRAIN_OFFS - (DBL_DCC_TRAIN_OFFS + DBL_DCC_TRAIN_STORED_NUM*8)  |  [39:36]  |  [35:32]  |   [31:16]   |      [15: 0]     |
 * ----------------------------------------------------------------------------------------------------------------------------------
 * detail                                                                  |   dcc_p   |   dcc_n   | ds_pass_sum | 2periods_sub_sum |
 * ----------------------------------------------------------------------------------------------------------------------------------
 */
/********************************
s5 should be init at beginning of mc_config.S
don't change s5 except refreshing the value
s5 :
    [32:32] training flag, 1-training, 0-no training
    [31:24] scaned effect number of dcc couples
    [23:20] current dcc p value
    [19:16] current dcc n value
    [15: 8] current loop number
    [ 7: 0] current count number in one loop
********************************/

//#define DBL_CK_TRAINING_DEBUG
#define     DBL_DCC_TRAIN_LOOP_TIMES                    15
#define     DBL_DCC_TRAIN_STORED_NUM                    16

#define     DBL_DCC_TRAIN_EFFECT_NUM_OFFSET             24
#define     DBL_DCC_TRAIN_CURR_DCC_P_OFFSET             20
#define     DBL_DCC_TRAIN_CURR_DCC_N_OFFSET             16
#define     DBL_DCC_TRAIN_CURR_LOOP_NUM_OFFSET          8
#define     DBL_DCC_TRAIN_CURR_CNT_IN_LOOP_OFFSET       0
#define     DBL_DCC_TRAIN_STORED_DCC_OFFSET             4
#define     DBL_DCC_TRAIN_STORED_DS_SUM_OFFSET          2
#define     DBL_DCC_TRAIN_STORED_PERIOD_SUB_OFFSET      0
#define     DBL_DCC_TRAIN_STORED_DCC_BIT_OFFSET         32
#define     DBL_DCC_TRAIN_STORED_DS_SUM_BIT_OFFSET      16
#define     DBL_DCC_TRAIN_STORED_PERIOD_SUB_BIT_OFFSET  0

#define     DBL_DCC_TRAIN_EFFECT_NUM_MASK               0xff
#define     DBL_DCC_TRAIN_CURR_DCC_PN_MASK              0xf
#define     DBL_DCC_TRAIN_CURR_DCC_MASK                 0xff
#define     DBL_DCC_TRAIN_CURR_LOOP_NUM_MASK            0xff
#define     DBL_DCC_TRAIN_CURR_CNT_IN_LOOP_MASK         0xff
#define     DBL_DCC_TRAIN_STORED_DS_SUM_MASK            0xffff
#define     DBL_DCC_TRAIN_STORED_PERIOD_SUB_MASK        0xffff

#define GET_DBL_DCC_TRAIN_EFFECT_NUM \
    dsrl    v0, s5, DBL_DCC_TRAIN_EFFECT_NUM_OFFSET;\
    and     v0, DBL_DCC_TRAIN_EFFECT_NUM_MASK

#define STORE_DBL_DCC_TRAIN_EFFECT_NUM \
    and     v0, DBL_DCC_TRAIN_EFFECT_NUM_MASK;\
    dsll    v0, DBL_DCC_TRAIN_EFFECT_NUM_OFFSET;\
    and     s5, ~(DBL_DCC_TRAIN_EFFECT_NUM_MASK << DBL_DCC_TRAIN_EFFECT_NUM_OFFSET);\
    or      s5, v0

#define GET_DBL_DCC_TRAIN_CURRENT_DCC_VALUE \
    dsrl    v0, s5, DBL_DCC_TRAIN_CURR_DCC_N_OFFSET;\
    and     v0, DBL_DCC_TRAIN_CURR_DCC_MASK

#define STORE_DBL_DCC_TRAIN_CURRENT_DCC_VALUE \
    and     v0, DBL_DCC_TRAIN_CURR_DCC_MASK;\
    dsll    v0, DBL_DCC_TRAIN_CURR_DCC_N_OFFSET;\
    and     s5, ~(DBL_DCC_TRAIN_CURR_DCC_MASK << DBL_DCC_TRAIN_CURR_DCC_N_OFFSET);\
    or      s5, v0

#define GET_DBL_DCC_TRAIN_CURRENT_DCC(x) \
    dsrl    v0, s5, DBL_DCC_TRAIN_CURR_DCC_##x##_OFFSET;\
    and     v0, DBL_DCC_TRAIN_CURR_DCC_PN_MASK

#define STORE_DBL_DCC_TRAIN_CURRENT_DCC(x) \
    and     v0, DBL_DCC_TRAIN_CURR_DCC_PN_MASK;\
    dsll    v0, DBL_DCC_TRAIN_CURR_DCC_##x##_OFFSET;\
    and     s5, ~(DBL_DCC_TRAIN_CURR_DCC_PN_MASK << DBL_DCC_TRAIN_CURR_DCC_##x##_OFFSET);\
    or      s5, v0

#define GET_DBL_DCC_TRAIN_CURRENT_LOOP_NUM \
    dsrl    v0, s5, DBL_DCC_TRAIN_CURR_LOOP_NUM_OFFSET;\
    and     v0, DBL_DCC_TRAIN_CURR_LOOP_NUM_MASK

#define STORE_DBL_DCC_TRAIN_CURRENT_LOOP_NUM \
    and     v0, DBL_DCC_TRAIN_CURR_LOOP_NUM_MASK;\
    dsll    v0, DBL_DCC_TRAIN_CURR_LOOP_NUM_OFFSET;\
    and     s5, ~(DBL_DCC_TRAIN_CURR_LOOP_NUM_MASK << DBL_DCC_TRAIN_CURR_LOOP_NUM_OFFSET);\
    or      s5, v0

#define GET_DBL_DCC_TRAIN_CURRENT_CNT_IN_LOOP_NUM \
    dsrl    v0, s5, DBL_DCC_TRAIN_CURR_CNT_IN_LOOP_OFFSET;\
    and     v0, DBL_DCC_TRAIN_CURR_CNT_IN_LOOP_MASK

#define STORE_DBL_DCC_TRAIN_CURRENT_CNT_IN_LOOP_NUM \
    and     v0, DBL_DCC_TRAIN_CURR_CNT_IN_LOOP_MASK;\
    dsll    v0, DBL_DCC_TRAIN_CURR_CNT_IN_LOOP_OFFSET;\
    and     s5, ~(DBL_DCC_TRAIN_CURR_CNT_IN_LOOP_MASK << DBL_DCC_TRAIN_CURR_CNT_IN_LOOP_OFFSET);\
    or      s5, v0

#define GET_DBL_DCC_TRAIN_STORED_DATA(x) \
    dsll    v0, x, 3;\
    daddu   v0, (DIMM_INFO_IN_CACHE_OFFS + DBL_DCC_TRAIN_OFFS);\
    ld      v0, 0x0(v0)

#define STORE_DBL_DCC_TRAIN_DATA(x,y) \
    dsll    v0, x, 3;\
    daddu   v0, (DIMM_INFO_IN_CACHE_OFFS + DBL_DCC_TRAIN_OFFS);\
    sd      y, 0x0(v0)

#define GET_DBL_DCC_TRAIN_STORED_DS_SUM(x) \
    dsll    v0, x, 3;\
    daddu   v0, (DIMM_INFO_IN_CACHE_OFFS + DBL_DCC_TRAIN_OFFS);\
    lhu     v0, DBL_DCC_TRAIN_STORED_DS_SUM_OFFSET(v0)

#define STORE_DBL_DCC_TRAIN_DS_SUM(x, y) \
    move    v0, y;\
    bleu    v0, DBL_DCC_TRAIN_STORED_DS_SUM_MASK, 80f;\
    nop;\
    dli     v0, DBL_DCC_TRAIN_STORED_DS_SUM_MASK;\
80:;\
    and     v0, DBL_DCC_TRAIN_STORED_DS_SUM_MASK;\
    dsll    v1, x, 3;\
    daddu   v1, (DIMM_INFO_IN_CACHE_OFFS + DBL_DCC_TRAIN_OFFS);\
    sh      v0, DBL_DCC_TRAIN_STORED_DS_SUM_OFFSET(v1)

#define GET_DBL_DCC_TRAIN_STORED_PERIOD_SUB(x) \
    dsll    v0, x, 3;\
    daddu   v0, (DIMM_INFO_IN_CACHE_OFFS + DBL_DCC_TRAIN_OFFS);\
    lhu     v0, DBL_DCC_TRAIN_STORED_PERIOD_SUB_OFFSET(v0)

#define STORE_DBL_DCC_TRAIN_PERIOD_SUB(x, y) \
    move    v0, y;\
    bleu    v0, DBL_DCC_TRAIN_STORED_PERIOD_SUB_MASK, 80f;;\
    nop;\
    dli     v0, DBL_DCC_TRAIN_STORED_PERIOD_SUB_MASK;\
80:;\
    and     v0, DBL_DCC_TRAIN_STORED_PERIOD_SUB_MASK;\
    dsll    v1, x, 3;\
    daddu   v1, (DIMM_INFO_IN_CACHE_OFFS + DBL_DCC_TRAIN_OFFS);\
    sh      v0, DBL_DCC_TRAIN_STORED_PERIOD_SUB_OFFSET(v1)

#define GET_DBL_DCC_TRAIN_STORED_DCC(x) \
    dsll    v0, x, 3;\
    daddu   v0, (DIMM_INFO_IN_CACHE_OFFS + DBL_DCC_TRAIN_OFFS);\
    lbu     v0, DBL_DCC_TRAIN_STORED_DCC_OFFSET(v0)

#define CONFIG_DBL_DCC_TRAIN_DATA(x_ds_sum, y_period_sub) \
    GET_DBL_DCC_TRAIN_CURRENT_DCC_VALUE;\
    dsll    v1, v0, DBL_DCC_TRAIN_STORED_DCC_BIT_OFFSET;\
    move    v0, x_ds_sum;\
    bleu    v0, DBL_DCC_TRAIN_STORED_DS_SUM_MASK, 80f;\
    nop;\
    dli     v0, DBL_DCC_TRAIN_STORED_DS_SUM_MASK;\
80:;\
    and     v0, DBL_DCC_TRAIN_STORED_DS_SUM_MASK;\
    dsll    v0, DBL_DCC_TRAIN_STORED_DS_SUM_BIT_OFFSET;\
    or      v1, v0;\
    move    v0, y_period_sub;\
    bleu    v0, DBL_DCC_TRAIN_STORED_PERIOD_SUB_MASK, 80f;;\
    nop;\
    dli     v0, DBL_DCC_TRAIN_STORED_PERIOD_SUB_MASK;\
80:;\
    and     v0, DBL_DCC_TRAIN_STORED_PERIOD_SUB_MASK;\
    dsll    v0, DBL_DCC_TRAIN_STORED_PERIOD_SUB_BIT_OFFSET;\
    or      v0, v1
