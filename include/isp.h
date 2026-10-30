#ifndef ISP_H
#define ISP_H

#include "alias.h"
#include "isp_macros.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
	BAYER_RGGB = 0,
	BAYER_GRBG = 1,
	BAYER_GBRG = 2,
	BAYER_BGGR = 3
} bayer_pat_t;

struct rgb_px {
	utiny r;
	utiny g;
	utiny b;
};

struct raw_frame {
	uint width;
	uint height;
	utiny depth;
	bayer_pat_t pat;
	ushort *data;
};

struct rgb_img {
	uint width;
	uint height;
	struct rgb_px *px;
};

struct awb_stats {
	double r_gain;
	double g_gain;
	double b_gain;
	double avg_r;
	double avg_g;
	double avg_b;
};

struct ae_stats {
	double mean_y;
	double target_y;
	double gain;
	uint clipped;
};

struct af_zone {
	uint zx;
	uint zy;
	double tenengrad;
	double lap_var;
};

struct af_stats {
	double global_tenengrad;
	double global_lap_var;
	struct af_zone zones[AF_ZONE_COUNT];
};

struct raw_frame raw_frame_alloc(uint width, uint height, utiny depth, bayer_pat_t pat);
void raw_frame_free(struct raw_frame *raw);

struct rgb_img rgb_img_alloc(uint width, uint height);
void rgb_img_free(struct rgb_img *img);

struct raw_frame isp_gen_test_frame(uint width, uint height, double defocus);
struct rgb_img isp_bayer_vis(struct raw_frame *raw);

void isp_blc_dpc(struct raw_frame *raw, ushort black_lvl);
struct rgb_img isp_demosaic_bilinear(struct raw_frame *raw);
struct rgb_img isp_demosaic_malvar(struct raw_frame *raw);

struct awb_stats isp_awb_gray_world(struct rgb_img *img);
struct ae_stats isp_ae_luma(struct rgb_img *img, double target_y);
void isp_gamma_init(double gamma);
void isp_ccm_gamma(struct rgb_img *img);

struct af_stats isp_af_eval(struct rgb_img *img, struct rgb_img *heatmap);
bool ppm_save(char *path, struct rgb_img *img);

static inline ushort *raw_at(struct raw_frame *f, uint x, uint y)
{
	return &f->data[(umax)y * f->width + x];
}

static inline struct rgb_px *rgb_at(struct rgb_img *img, uint x, uint y)
{
	return &img->px[(umax)y * img->width + x];
}

#ifdef __cplusplus
}
#endif

#endif // ISP_H
