#ifndef ISP_MACROS_H
#define ISP_MACROS_H

#define MAX_U8       255
#define MAX_U8_F     255.0
#define LUT_SIZE     256
#define ROUND_HALF   0.5

#define DEFAULT_WIDTH       1280
#define DEFAULT_HEIGHT      720
#define DEFAULT_GAMMA       2.2
#define RAW_DEFAULT_DEPTH   10
#define RAW_BLACK_LEVEL     64
#define RAW_MAX_VAL(depth)  ((1u << (depth)) - 1u)

#define BAYER_PHASE_X(x, pat) (((x) & 1u) ^ ((pat) & 1u))
#define BAYER_PHASE_Y(y, pat) (((y) & 1u) ^ (((pat) >> 1u) & 1u))

#define DPC_RADIUS        2
#define DPC_NEIGHBOR_SHF  2
#define DPC_THRESH_RATIO  0.35

#define PATCH_COLS        6
#define PATCH_ROWS        4
#define PATCH_COUNT       (PATCH_COLS * PATCH_ROWS)
#define PATCH_CHANNELS    3
#define PATCH_BORDER      4
#define PATCH_BORDER_VAL  28

#define SIEMENS_RAD_RATIO  0.22
#define SIEMENS_SPOKES     24.0
#define SIEMENS_FREQ       0.35
#define SIEMENS_HI         235.0
#define SIEMENS_LO         25.0
#define SIEMENS_RING_W     3.0
#define SIEMENS_RING_R     240
#define SIEMENS_RING_G     200
#define SIEMENS_RING_B     30

#define SCENE_EXP_SCALE    0.65
#define WB_BIAS_R          0.58
#define WB_BIAS_G          1.00
#define WB_BIAS_B          0.66

#define HOT_PX_MARGIN_Y    16
#define HOT_PX_MARGIN_X    24
#define HOT_PX_STEP_Y      97
#define HOT_PX_STEP_X      131

#define DEFOCUS_MIN_SIGMA  0.15
#define DEFOCUS_RAD_SCALE  2.5
#define DEFOCUS_MAX_RAD    12
#define DEFOCUS_MAX_KSIZE  (2 * DEFOCUS_MAX_RAD + 1)

#define AWB_SAT_HI         250
#define AWB_DARK_LO        4
#define AWB_MIN_GAIN       0.5
#define AWB_MAX_GAIN       3.5
#define AWB_FALLBACK_AVG   128.0

#define AE_TARGET_LUMA     118.0
#define AE_CLIP_THRESH     254
#define AE_MIN_GAIN        0.5
#define AE_MAX_GAIN        4.0

#define REC709_R           0.2126
#define REC709_G           0.7152
#define REC709_B           0.0722

#define CCM_DIM            3

#define LUMA_COEF_R        77
#define LUMA_COEF_G        150
#define LUMA_COEF_B        29
#define LUMA_SHIFT         8

#define AF_GRID_DIM        3
#define AF_ZONE_COUNT      (AF_GRID_DIM * AF_GRID_DIM)
#define AF_CENTER_ZONE     ((AF_GRID_DIM / 2) * AF_GRID_DIM + (AF_GRID_DIM / 2))
#define AF_HEAT_SCALE      0.35
#define AF_HEAT_G_SCALE    0.65
#define AF_HEAT_HI_THRESH  120

#define AF_SWEEP_WIDTH     320
#define AF_SWEEP_HEIGHT    240
#define AF_SWEEP_STEPS     7

#define MS_PER_SEC         1000.0
#define NS_PER_MS          1e6
#define PX_PER_MPIX        1e6
#define OUT_DIR_MODE       0755

#define V4L2_DEFAULT_DEV     "/dev/video0"
#define V4L2_DEFAULT_WIDTH   640
#define V4L2_DEFAULT_HEIGHT  480
#define V4L2_RING_COUNT      4
#define V4L2_MIN_BUFS        2
#define V4L2_DEFAULT_FRAMES  8
#define V4L2_POLL_TIMEOUT_MS 2000

#define YUYV_BPP       2
#define YUV_Y_OFFSET   16
#define YUV_UV_OFFSET  128
#define YUV_ROUND      128
#define YUV_SHIFT      8
#define YUV_C_Y        298
#define YUV_C_RV       409
#define YUV_C_GU       100
#define YUV_C_GV       208
#define YUV_C_BU       516

#define HAL3_DEFAULT_WIDTH   640
#define HAL3_DEFAULT_HEIGHT  480
#define HAL3_DEFAULT_FRAMES  8
#define HAL3_DAC_INIT        150
#define HAL3_DAC_STEP        75
#define HAL3_DAC_MIN         100
#define HAL3_DAC_MAX         700
#define HAL3_DAC_OPT         400
#define HAL3_DAC_SIGMA_SCALE 100.0
#define HAL3_AE_TOLERANCE    6.0
#define HAL3_AE_KP           0.45
#define HAL3_BASE_EXP_US     8000
#define HAL3_BASE_ISO        100
#define JNI_STATS_LEN        6

#endif // ISP_MACROS_H
