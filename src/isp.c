#include "isp.h"
#include <stdlib.h>
#include <stdio.h>
#include <math.h>

static utiny s_gamma_lut[LUT_SIZE];
static bool s_gamma_ready = false;

static utiny s_patches[PATCH_COUNT][PATCH_CHANNELS] = {
	{115,  82,  68}, {194, 150, 130}, { 98, 122, 157}, { 87, 108,  67},
	{133, 128, 177}, {103, 189, 170}, {214, 126,  44}, { 80,  91, 166},
	{193,  90,  99}, { 94,  60, 108}, {157, 188,  64}, {224, 163,  46},
	{ 56,  61, 150}, { 70, 148,  73}, {175,  54,  60}, {231, 199,  31},
	{187,  86, 149}, {  8, 133, 161}, {243, 243, 242}, {200, 200, 200},
	{160, 160, 160}, {122, 122, 121}, { 85,  85,  85}, { 52,  52,  52}
};

static double s_ccm[CCM_DIM][CCM_DIM] = {
	{ 1.18, -0.12, -0.06},
	{-0.08,  1.16, -0.08},
	{-0.05, -0.15,  1.20}
};

static inline utiny clamp_u8(double v)
{
	if (v <= 0.0) return 0;
	if (v >= MAX_U8_F) return MAX_U8;
	return (utiny)(v + ROUND_HALF);
}

static inline int clamp_i(int v, int lo, int hi)
{
	if (v < lo) return lo;
	if (v > hi) return hi;
	return v;
}

static inline int max4(int a, int b, int c, int d)
{
	int m1 = (a > b) ? a : b;
	int m2 = (c > d) ? c : d;
	return (m1 > m2) ? m1 : m2;
}

static inline int min4(int a, int b, int c, int d)
{
	int m1 = (a < b) ? a : b;
	int m2 = (c < d) ? c : d;
	return (m1 < m2) ? m1 : m2;
}

struct raw_frame raw_frame_alloc(uint width, uint height, utiny depth, bayer_pat_t pat)
{
	struct raw_frame self;
	self.width = width;
	self.height = height;
	self.depth = depth;
	self.pat = pat;
	self.data = calloc((umax)width * height, sizeof(ushort));
	return self;
}

void raw_frame_free(struct raw_frame *raw)
{
	if (raw && raw->data) {
		free(raw->data);
		raw->data = NULL;
	}
}

struct rgb_img rgb_img_alloc(uint width, uint height)
{
	struct rgb_img self;
	self.width = width;
	self.height = height;
	self.px = calloc((umax)width * height, sizeof(struct rgb_px));
	return self;
}

void rgb_img_free(struct rgb_img *img)
{
	if (img && img->px) {
		free(img->px);
		img->px = NULL;
	}
}

void isp_gamma_init(double gamma)
{
	double inv = 1.0 / gamma;
	for (int i = 0; i < LUT_SIZE; i++) {
		double n = (double)i / MAX_U8_F;
		s_gamma_lut[i] = clamp_u8(pow(n, inv) * MAX_U8_F);
	}
	s_gamma_ready = true;
}

struct raw_frame isp_gen_test_frame(uint width, uint height, double defocus)
{
	struct rgb_img scene = rgb_img_alloc(width, height);

	double cx = width * ROUND_HALF;
	double cy = height * ROUND_HALF;
	double rad = ((width < height) ? width : height) * SIEMENS_RAD_RATIO;

	for (uint y = 0; y < height; y++) {
		for (uint x = 0; x < width; x++) {
			uint gx = (x * PATCH_COLS) / width;
			uint gy = (y * PATCH_ROWS) / height;
			uint idx = (gy * PATCH_COLS + gx) % PATCH_COUNT;

			uint cw = width / PATCH_COLS;
			uint ch = height / PATCH_ROWS;
			uint lx = x % (cw ? cw : 1);
			uint ly = y % (ch ? ch : 1);

			struct rgb_px p = {s_patches[idx][0], s_patches[idx][1], s_patches[idx][2]};
			if (lx < PATCH_BORDER || ly < PATCH_BORDER || lx + PATCH_BORDER >= cw || ly + PATCH_BORDER >= ch)
				p = (struct rgb_px){PATCH_BORDER_VAL, PATCH_BORDER_VAL, PATCH_BORDER_VAL};

			double dx = (double)x - cx;
			double dy = (double)y - cy;
			double dist = sqrt(dx * dx + dy * dy);
			if (dist < rad) {
				double ang = atan2(dy, dx);
				double v = (sin(ang * SIEMENS_SPOKES) * cos(dist * SIEMENS_FREQ) > 0.0) ? SIEMENS_HI : SIEMENS_LO;
				p.r = clamp_u8(v);
				p.g = clamp_u8(v);
				p.b = clamp_u8(v);
			} else if (fabs(dist - rad) < SIEMENS_RING_W) {
				p = (struct rgb_px){SIEMENS_RING_R, SIEMENS_RING_G, SIEMENS_RING_B};
			}

			double lr = pow(p.r / MAX_U8_F, DEFAULT_GAMMA) * SCENE_EXP_SCALE;
			double lg = pow(p.g / MAX_U8_F, DEFAULT_GAMMA) * SCENE_EXP_SCALE;
			double lb = pow(p.b / MAX_U8_F, DEFAULT_GAMMA) * SCENE_EXP_SCALE;

			*rgb_at(&scene, x, y) = (struct rgb_px){
				clamp_u8(lr * MAX_U8_F),
				clamp_u8(lg * MAX_U8_F),
				clamp_u8(lb * MAX_U8_F)
			};
		}
	}

	if (defocus > DEFOCUS_MIN_SIGMA) {
		int krad = (int)ceil(defocus * DEFOCUS_RAD_SCALE);
		if (krad < 1) krad = 1;
		if (krad > DEFOCUS_MAX_RAD) krad = DEFOCUS_MAX_RAD;

		double kern[DEFOCUS_MAX_KSIZE];
		double ksum = 0.0;
		for (int i = -krad; i <= krad; i++) {
			double w = exp(-(i * i) / (2.0 * defocus * defocus));
			kern[i + krad] = w;
			ksum += w;
		}
		for (int i = 0; i < 2 * krad + 1; i++) {
			kern[i] /= ksum;
		}

		struct rgb_img tmp = rgb_img_alloc(width, height);
		for (int y = 0; y < (int)height; y++) {
			for (int x = 0; x < (int)width; x++) {
				double r = 0, g = 0, b = 0;
				for (int k = -krad; k <= krad; k++) {
					int sx = clamp_i(x + k, 0, (int)width - 1);
					struct rgb_px *p = rgb_at(&scene, sx, y);
					double w = kern[k + krad];
					r += p->r * w; g += p->g * w; b += p->b * w;
				}
				*rgb_at(&tmp, x, y) = (struct rgb_px){clamp_u8(r), clamp_u8(g), clamp_u8(b)};
			}
		}
		for (int y = 0; y < (int)height; y++) {
			for (int x = 0; x < (int)width; x++) {
				double r = 0, g = 0, b = 0;
				for (int k = -krad; k <= krad; k++) {
					int sy = clamp_i(y + k, 0, (int)height - 1);
					struct rgb_px *p = rgb_at(&tmp, x, sy);
					double w = kern[k + krad];
					r += p->r * w; g += p->g * w; b += p->b * w;
				}
				*rgb_at(&scene, x, y) = (struct rgb_px){clamp_u8(r), clamp_u8(g), clamp_u8(b)};
			}
		}
		rgb_img_free(&tmp);
	}

	struct raw_frame raw = raw_frame_alloc(width, height, RAW_DEFAULT_DEPTH, BAYER_RGGB);
	ushort max_dn = RAW_MAX_VAL(raw.depth);
	ushort black = RAW_BLACK_LEVEL;
	double swing = (double)max_dn - black;

	for (uint y = 0; y < height; y++) {
		uint py = BAYER_PHASE_Y(y, raw.pat);
		for (uint x = 0; x < width; x++) {
			uint px = BAYER_PHASE_X(x, raw.pat);
			struct rgb_px *p = rgb_at(&scene, x, y);
			double cfa = 0.0;

			if (!py && !px)
				cfa = (p->r / MAX_U8_F) * WB_BIAS_R;
			else if (py && px)
				cfa = (p->b / MAX_U8_F) * WB_BIAS_B;
			else
				cfa = (p->g / MAX_U8_F) * WB_BIAS_G;

			double dn = black + cfa * swing;
			if (dn < 0.0) dn = 0.0;
			if (dn > (double)max_dn) dn = (double)max_dn;
			*raw_at(&raw, x, y) = (ushort)(dn + ROUND_HALF);
		}
	}

	for (uint y = HOT_PX_MARGIN_Y; y + HOT_PX_MARGIN_Y < height; y += HOT_PX_STEP_Y) {
		for (uint x = HOT_PX_MARGIN_X; x + HOT_PX_MARGIN_X < width; x += HOT_PX_STEP_X) {
			*raw_at(&raw, x, y) = ((x + y) & 1u) ? max_dn : 0;
		}
	}

	rgb_img_free(&scene);
	return raw;
}

struct rgb_img isp_bayer_vis(struct raw_frame *raw)
{
	struct rgb_img out = rgb_img_alloc(raw->width, raw->height);
	double scale = MAX_U8_F / RAW_MAX_VAL(raw->depth);

	for (uint y = 0; y < raw->height; y++) {
		uint py = BAYER_PHASE_Y(y, raw->pat);
		for (uint x = 0; x < raw->width; x++) {
			uint px = BAYER_PHASE_X(x, raw->pat);
			utiny v = clamp_u8((*raw_at(raw, x, y)) * scale);
			if (!py && !px)
				*rgb_at(&out, x, y) = (struct rgb_px){v, 0, 0};
			else if (py && px)
				*rgb_at(&out, x, y) = (struct rgb_px){0, 0, v};
			else
				*rgb_at(&out, x, y) = (struct rgb_px){0, v, 0};
		}
	}
	return out;
}

void isp_blc_dpc(struct raw_frame *raw, ushort black_lvl)
{
	ushort max_v = RAW_MAX_VAL(raw->depth);
	double scale = (max_v > black_lvl) ? ((double)max_v / (max_v - black_lvl)) : 1.0;
	umax n = (umax)raw->width * raw->height;

	for (umax i = 0; i < n; i++) {
		ushort v = raw->data[i];
		if (v <= black_lvl) {
			raw->data[i] = 0;
		} else {
			double r = (v - black_lvl) * scale;
			raw->data[i] = (r > max_v) ? max_v : (ushort)(r + ROUND_HALF);
		}
	}

	int w = (int)raw->width;
	int h = (int)raw->height;
	int thresh = (int)(max_v * DPC_THRESH_RATIO);
	ushort *copy = malloc(n * sizeof(ushort));
	if (!copy) return;

	for (umax i = 0; i < n; i++) {
		copy[i] = raw->data[i];
	}

	for (int y = DPC_RADIUS; y < h - DPC_RADIUS; y++) {
		for (int x = DPC_RADIUS; x < w - DPC_RADIUS; x++) {
			int c  = copy[(umax)y * w + x];
			int n0 = copy[(umax)y * w + (x - DPC_RADIUS)];
			int n1 = copy[(umax)y * w + (x + DPC_RADIUS)];
			int n2 = copy[(umax)(y - DPC_RADIUS) * w + x];
			int n3 = copy[(umax)(y + DPC_RADIUS) * w + x];

			int hi = max4(n0, n1, n2, n3);
			int lo = min4(n0, n1, n2, n3);
			if (c > hi + thresh || c < lo - thresh)
				*raw_at(raw, x, y) = (ushort)((n0 + n1 + n2 + n3) >> DPC_NEIGHBOR_SHF);
		}
	}

	free(copy);
}

static inline double raw_sample(struct raw_frame *raw, int x, int y, double scale)
{
	int cx = clamp_i(x, 0, (int)raw->width - 1);
	int cy = clamp_i(y, 0, (int)raw->height - 1);
	return (*raw_at(raw, cx, cy)) * scale;
}

struct rgb_img isp_demosaic_bilinear(struct raw_frame *raw)
{
	int w = (int)raw->width;
	int h = (int)raw->height;
	double scale = MAX_U8_F / RAW_MAX_VAL(raw->depth);
	struct rgb_img out = rgb_img_alloc(raw->width, raw->height);

	for (int y = 0; y < h; y++) {
		uint py = BAYER_PHASE_Y((uint)y, raw->pat);
		for (int x = 0; x < w; x++) {
			uint px = BAYER_PHASE_X((uint)x, raw->pat);
			double r, g, b;

			if (!py && !px) {
				r = raw_sample(raw, x, y, scale);
				g = 0.25 * (raw_sample(raw, x - 1, y, scale) + raw_sample(raw, x + 1, y, scale) +
				            raw_sample(raw, x, y - 1, scale) + raw_sample(raw, x, y + 1, scale));
				b = 0.25 * (raw_sample(raw, x - 1, y - 1, scale) + raw_sample(raw, x + 1, y - 1, scale) +
				            raw_sample(raw, x - 1, y + 1, scale) + raw_sample(raw, x + 1, y + 1, scale));
			} else if (!py && px) {
				r = 0.5 * (raw_sample(raw, x - 1, y, scale) + raw_sample(raw, x + 1, y, scale));
				g = raw_sample(raw, x, y, scale);
				b = 0.5 * (raw_sample(raw, x, y - 1, scale) + raw_sample(raw, x, y + 1, scale));
			} else if (py && !px) {
				r = 0.5 * (raw_sample(raw, x, y - 1, scale) + raw_sample(raw, x, y + 1, scale));
				g = raw_sample(raw, x, y, scale);
				b = 0.5 * (raw_sample(raw, x - 1, y, scale) + raw_sample(raw, x + 1, y, scale));
			} else {
				r = 0.25 * (raw_sample(raw, x - 1, y - 1, scale) + raw_sample(raw, x + 1, y - 1, scale) +
				            raw_sample(raw, x - 1, y + 1, scale) + raw_sample(raw, x + 1, y + 1, scale));
				g = 0.25 * (raw_sample(raw, x - 1, y, scale) + raw_sample(raw, x + 1, y, scale) +
				            raw_sample(raw, x, y - 1, scale) + raw_sample(raw, x, y + 1, scale));
				b = raw_sample(raw, x, y, scale);
			}

			*rgb_at(&out, x, y) = (struct rgb_px){clamp_u8(r), clamp_u8(g), clamp_u8(b)};
		}
	}
	return out;
}

struct rgb_img isp_demosaic_malvar(struct raw_frame *raw)
{
	int w = (int)raw->width;
	int h = (int)raw->height;
	double scale = MAX_U8_F / RAW_MAX_VAL(raw->depth);
	struct rgb_img out = rgb_img_alloc(raw->width, raw->height);

	for (int y = 0; y < h; y++) {
		uint py = BAYER_PHASE_Y((uint)y, raw->pat);
		for (int x = 0; x < w; x++) {
			uint px = BAYER_PHASE_X((uint)x, raw->pat);
			double c  = raw_sample(raw, x, y, scale);
			double n  = raw_sample(raw, x, y - 1, scale);
			double s  = raw_sample(raw, x, y + 1, scale);
			double w1 = raw_sample(raw, x - 1, y, scale);
			double e1 = raw_sample(raw, x + 1, y, scale);
			double n2 = raw_sample(raw, x, y - 2, scale);
			double s2 = raw_sample(raw, x, y + 2, scale);
			double w2 = raw_sample(raw, x - 2, y, scale);
			double e2 = raw_sample(raw, x + 2, y, scale);
			double nw = raw_sample(raw, x - 1, y - 1, scale);
			double ne = raw_sample(raw, x + 1, y - 1, scale);
			double sw = raw_sample(raw, x - 1, y + 1, scale);
			double se = raw_sample(raw, x + 1, y + 1, scale);

			double r, g, b;
			if (!py && !px) {
				r = c;
				g = (4.0 * c + 2.0 * (n + s + w1 + e1) - (n2 + s2 + w2 + e2)) / 8.0;
				b = (6.0 * c + 2.0 * (nw + ne + sw + se) - 1.5 * (n2 + s2 + w2 + e2)) / 8.0;
			} else if (!py && px) {
				g = c;
				r = (5.0 * c + 4.0 * (w1 + e1) - (nw + ne + sw + se + w2 + e2) + 0.5 * (n2 + s2)) / 8.0;
				b = (5.0 * c + 4.0 * (n + s) - (nw + ne + sw + se + n2 + s2) + 0.5 * (w2 + e2)) / 8.0;
			} else if (py && !px) {
				g = c;
				r = (5.0 * c + 4.0 * (n + s) - (nw + ne + sw + se + n2 + s2) + 0.5 * (w2 + e2)) / 8.0;
				b = (5.0 * c + 4.0 * (w1 + e1) - (nw + ne + sw + se + w2 + e2) + 0.5 * (n2 + s2)) / 8.0;
			} else {
				b = c;
				g = (4.0 * c + 2.0 * (n + s + w1 + e1) - (n2 + s2 + w2 + e2)) / 8.0;
				r = (6.0 * c + 2.0 * (nw + ne + sw + se) - 1.5 * (n2 + s2 + w2 + e2)) / 8.0;
			}

			*rgb_at(&out, x, y) = (struct rgb_px){clamp_u8(r), clamp_u8(g), clamp_u8(b)};
		}
	}
	return out;
}

struct awb_stats isp_awb_gray_world(struct rgb_img *img)
{
	umax sr = 0, sg = 0, sb = 0, cnt = 0;
	umax n = (umax)img->width * img->height;

	for (umax i = 0; i < n; i++) {
		struct rgb_px p = img->px[i];
		utiny hi = (p.r > p.g) ? ((p.r > p.b) ? p.r : p.b) : ((p.g > p.b) ? p.g : p.b);
		utiny lo = (p.r < p.g) ? ((p.r < p.b) ? p.r : p.b) : ((p.g < p.b) ? p.g : p.b);
		if (hi < AWB_SAT_HI && lo > AWB_DARK_LO) {
			sr += p.r;
			sg += p.g;
			sb += p.b;
			cnt++;
		}
	}

	if (!cnt)
		return (struct awb_stats){1.0, 1.0, 1.0, AWB_FALLBACK_AVG, AWB_FALLBACK_AVG, AWB_FALLBACK_AVG};

	double ar = (double)sr / cnt;
	double ag = (double)sg / cnt;
	double ab = (double)sb / cnt;

	double rg = (ar > 1.0) ? (ag / ar) : 1.0;
	double bg = (ab > 1.0) ? (ag / ab) : 1.0;
	if (rg < AWB_MIN_GAIN) rg = AWB_MIN_GAIN;
	if (rg > AWB_MAX_GAIN) rg = AWB_MAX_GAIN;
	if (bg < AWB_MIN_GAIN) bg = AWB_MIN_GAIN;
	if (bg > AWB_MAX_GAIN) bg = AWB_MAX_GAIN;

	for (umax i = 0; i < n; i++) {
		img->px[i].r = clamp_u8(img->px[i].r * rg);
		img->px[i].b = clamp_u8(img->px[i].b * bg);
	}

	return (struct awb_stats){rg, 1.0, bg, ar, ag, ab};
}

struct ae_stats isp_ae_luma(struct rgb_img *img, double target_y)
{
	double sy = 0.0;
	uint clipped = 0;
	umax n = (umax)img->width * img->height;

	for (umax i = 0; i < n; i++) {
		struct rgb_px p = img->px[i];
		sy += REC709_R * p.r + REC709_G * p.g + REC709_B * p.b;
		if (p.r >= AE_CLIP_THRESH || p.g >= AE_CLIP_THRESH || p.b >= AE_CLIP_THRESH)
			clipped++;
	}

	double mean_y = n ? (sy / n) : 1.0;
	double gain = (mean_y > 1.0) ? (target_y / mean_y) : 1.0;
	if (gain < AE_MIN_GAIN) gain = AE_MIN_GAIN;
	if (gain > AE_MAX_GAIN) gain = AE_MAX_GAIN;

	for (umax i = 0; i < n; i++) {
		img->px[i].r = clamp_u8(img->px[i].r * gain);
		img->px[i].g = clamp_u8(img->px[i].g * gain);
		img->px[i].b = clamp_u8(img->px[i].b * gain);
	}

	return (struct ae_stats){mean_y, target_y, gain, clipped};
}

void isp_ccm_gamma(struct rgb_img *img)
{
	if (!s_gamma_ready)
		isp_gamma_init(DEFAULT_GAMMA);

	umax n = (umax)img->width * img->height;
	for (umax i = 0; i < n; i++) {
		struct rgb_px p = img->px[i];
		double r = s_ccm[0][0] * p.r + s_ccm[0][1] * p.g + s_ccm[0][2] * p.b;
		double g = s_ccm[1][0] * p.r + s_ccm[1][1] * p.g + s_ccm[1][2] * p.b;
		double b = s_ccm[2][0] * p.r + s_ccm[2][1] * p.g + s_ccm[2][2] * p.b;

		img->px[i].r = s_gamma_lut[clamp_u8(r)];
		img->px[i].g = s_gamma_lut[clamp_u8(g)];
		img->px[i].b = s_gamma_lut[clamp_u8(b)];
	}
}

struct af_stats isp_af_eval(struct rgb_img *img, struct rgb_img *heatmap)
{
	int w = (int)img->width;
	int h = (int)img->height;
	umax n = (umax)w * h;

	utiny *luma = malloc(n);
	for (umax i = 0; i < n; i++) {
		struct rgb_px p = img->px[i];
		luma[i] = (utiny)((LUMA_COEF_R * p.r + LUMA_COEF_G * p.g + LUMA_COEF_B * p.b) >> LUMA_SHIFT);
	}

	struct af_stats st = {0};
	double z_ten[AF_ZONE_COUNT] = {0};
	double z_lap[AF_ZONE_COUNT] = {0};
	double z_lap2[AF_ZONE_COUNT] = {0};
	umax z_cnt[AF_ZONE_COUNT] = {0};

	double g_ten = 0, g_lap = 0, g_lap2 = 0;
	umax g_cnt = 0;

	for (int y = 1; y < h - 1; y++) {
		int zy = (y * AF_GRID_DIM) / h;
		if (zy >= AF_GRID_DIM) zy = AF_GRID_DIM - 1;
		for (int x = 1; x < w - 1; x++) {
			int zx = (x * AF_GRID_DIM) / w;
			if (zx >= AF_GRID_DIM) zx = AF_GRID_DIM - 1;
			int zi = zy * AF_GRID_DIM + zx;

			int gx = -luma[(y - 1) * w + (x - 1)] + luma[(y - 1) * w + (x + 1)]
			         - 2 * luma[y * w + (x - 1)] + 2 * luma[y * w + (x + 1)]
			         - luma[(y + 1) * w + (x - 1)] + luma[(y + 1) * w + (x + 1)];

			int gy = -luma[(y - 1) * w + (x - 1)] - 2 * luma[(y - 1) * w + x] - luma[(y - 1) * w + (x + 1)]
			         + luma[(y + 1) * w + (x - 1)] + 2 * luma[(y + 1) * w + x] + luma[(y + 1) * w + (x + 1)];

			double ten = (double)(gx * gx + gy * gy);
			double lap = (double)(4 * luma[y * w + x] - luma[y * w + (x - 1)] - luma[y * w + (x + 1)] -
			                      luma[(y - 1) * w + x] - luma[(y + 1) * w + x]);

			z_ten[zi] += ten;
			z_lap[zi] += lap;
			z_lap2[zi] += lap * lap;
			z_cnt[zi]++;

			g_ten += ten;
			g_lap += lap;
			g_lap2 += lap * lap;
			g_cnt++;

			if (heatmap && heatmap->px) {
				double mag = sqrt(ten) * AF_HEAT_SCALE;
				utiny m = clamp_u8(mag);
				*rgb_at(heatmap, x, y) = (struct rgb_px){
					m,
					clamp_u8(mag * AF_HEAT_G_SCALE),
					(utiny)(m > AF_HEAT_HI_THRESH ? MAX_U8 : (m >> 1))
				};
			}
		}
	}

	for (int z = 0; z < AF_ZONE_COUNT; z++) {
		umax c = z_cnt[z] ? z_cnt[z] : 1;
		double ml = z_lap[z] / c;
		st.zones[z].zx = z % AF_GRID_DIM;
		st.zones[z].zy = z / AF_GRID_DIM;
		st.zones[z].tenengrad = z_ten[z] / c;
		st.zones[z].lap_var = (z_lap2[z] / c) - (ml * ml);
	}

	umax gc = g_cnt ? g_cnt : 1;
	double gml = g_lap / gc;
	st.global_tenengrad = g_ten / gc;
	st.global_lap_var = (g_lap2 / gc) - (gml * gml);

	free(luma);
	return st;
}

bool ppm_save(char *path, struct rgb_img *img)
{
	FILE *f = fopen(path, "wb");
	if (!f)
		return false;
	fprintf(f, "P6\n%u %u\n%d\n", img->width, img->height, MAX_U8);
	fwrite(img->px, sizeof(struct rgb_px), (umax)img->width * img->height, f);
	fclose(f);
	return true;
}
