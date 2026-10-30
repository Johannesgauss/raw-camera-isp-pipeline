#include "isp.h"
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <sys/stat.h>

struct af_step {
	int dac;
	double sigma;
};

static struct af_step s_sweep[AF_SWEEP_STEPS] = {
	{100, 3.2},
	{200, 2.1},
	{300, 1.1},
	{400, 0.0},
	{500, 0.9},
	{600, 1.9},
	{700, 3.0}
};

static inline double now_ms()
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (double)ts.tv_sec * MS_PER_SEC + (double)ts.tv_nsec / NS_PER_MS;
}

int main(int argc, char **argv)
{
	uint width = DEFAULT_WIDTH;
	uint height = DEFAULT_HEIGHT;
	ushort black_lvl = RAW_BLACK_LEVEL;
	double target_luma = AE_TARGET_LUMA;

	if (argc >= 3) {
		width = (uint)strtoul(argv[1], NULL, 10);
		height = (uint)strtoul(argv[2], NULL, 10);
	}
	if (argc >= 4)
		black_lvl = (ushort)strtoul(argv[3], NULL, 10);
	if (argc >= 5)
		target_luma = strtod(argv[4], NULL);

	mkdir("output", OUT_DIR_MODE);
	isp_gamma_init(DEFAULT_GAMMA);

	double mpix = ((double)width * height) / PX_PER_MPIX;
	printf("ISP pipeline (%ux%u, %.2f MP, %u-bit RGGB)\n\n", width, height, mpix, RAW_DEFAULT_DEPTH);

	struct raw_frame raw = isp_gen_test_frame(width, height, 0.0);
	struct rgb_img cfa_vis = isp_bayer_vis(&raw);
	ppm_save("output/01_raw_bayer.ppm", &cfa_vis);
	rgb_img_free(&cfa_vis);

	double t0 = now_ms();
	isp_blc_dpc(&raw, black_lvl);
	double t_blc = now_ms() - t0;

	t0 = now_ms();
	struct rgb_img rgb_bil = isp_demosaic_bilinear(&raw);
	double t_bil = now_ms() - t0;
	rgb_img_free(&rgb_bil);

	t0 = now_ms();
	struct rgb_img img = isp_demosaic_malvar(&raw);
	double t_mal = now_ms() - t0;
	ppm_save("output/02_demosaic.ppm", &img);

	t0 = now_ms();
	struct awb_stats awb = isp_awb_gray_world(&img);
	double t_awb = now_ms() - t0;
	ppm_save("output/03_awb.ppm", &img);

	t0 = now_ms();
	struct ae_stats ae = isp_ae_luma(&img, target_luma);
	double t_ae = now_ms() - t0;

	t0 = now_ms();
	isp_ccm_gamma(&img);
	double t_ccm = now_ms() - t0;
	ppm_save("output/04_final_srgb.ppm", &img);

	struct rgb_img heatmap = rgb_img_alloc(width, height);
	t0 = now_ms();
	struct af_stats af = isp_af_eval(&img, NULL);
	double t_af = now_ms() - t0;

	isp_af_eval(&img, &heatmap);
	ppm_save("output/05_af_edges.ppm", &heatmap);

	printf("[3A stats]\n");
	printf("  AWB gains : R=%.3f G=%.3f B=%.3f (avg: %.1f, %.1f, %.1f)\n",
	       awb.r_gain, awb.g_gain, awb.b_gain, awb.avg_r, awb.avg_g, awb.avg_b);
	printf("  AE luma   : mean=%.2f -> target=%.2f (gain=%.3fx)\n",
	       ae.mean_y, ae.target_y, ae.gain);
	printf("  AF global : tenengrad=%.1f, lap_var=%.1f\n\n",
	       af.global_tenengrad, af.global_lap_var);

	printf("[AF %dx%d ROI grid]\n", AF_GRID_DIM, AF_GRID_DIM);
	for (int y = 0; y < AF_GRID_DIM; y++) {
		printf("  ");
		for (int x = 0; x < AF_GRID_DIM; x++) {
			printf("ROI(%d,%d): %8.0f   ", x, y, af.zones[y * AF_GRID_DIM + x].tenengrad);
		}
		printf("\n");
	}
	printf("\n");

	printf("[AF lens sweep]\n");
	int best_dac = 0;
	double best_score = -1.0;
	for (umax i = 0; i < AF_SWEEP_STEPS; i++) {
		struct raw_frame sf = isp_gen_test_frame(AF_SWEEP_WIDTH, AF_SWEEP_HEIGHT, s_sweep[i].sigma);
		isp_blc_dpc(&sf, black_lvl);
		struct rgb_img srgb = isp_demosaic_bilinear(&sf);
		struct af_stats saf = isp_af_eval(&srgb, NULL);
		double sc = saf.zones[AF_CENTER_ZONE].tenengrad;
		if (sc > best_score) {
			best_score = sc;
			best_dac = s_sweep[i].dac;
		}
		printf("  dac=%3d sigma=%.1f score=%8.1f%s\n",
		       s_sweep[i].dac, s_sweep[i].sigma, sc, (s_sweep[i].sigma == 0.0) ? " <-- lock" : "");
		rgb_img_free(&srgb);
		raw_frame_free(&sf);
	}
	printf("  locked dac=%d (peak=%.1f)\n\n", best_dac, best_score);

	double tot_bil = t_blc + t_bil + t_awb + t_ae + t_ccm + t_af;
	double tot_mal = t_blc + t_mal + t_awb + t_ae + t_ccm + t_af;

	printf("[Benchmark]\n");
	printf("  BLC + DPC         : %6.2f ms (%6.1f MP/s)\n", t_blc, mpix / (t_blc / MS_PER_SEC));
	printf("  Demosaic Bilinear : %6.2f ms (%6.1f MP/s)\n", t_bil, mpix / (t_bil / MS_PER_SEC));
	printf("  Demosaic Malvar   : %6.2f ms (%6.1f MP/s)\n", t_mal, mpix / (t_mal / MS_PER_SEC));
	printf("  AWB Gray World    : %6.2f ms (%6.1f MP/s)\n", t_awb, mpix / (t_awb / MS_PER_SEC));
	printf("  AE Luma Gain      : %6.2f ms (%6.1f MP/s)\n", t_ae,  mpix / (t_ae / MS_PER_SEC));
	printf("  CCM + Gamma LUT   : %6.2f ms (%6.1f MP/s)\n", t_ccm, mpix / (t_ccm / MS_PER_SEC));
	printf("  AF %dx%d Tenengrad  : %6.2f ms (%6.1f MP/s)\n", AF_GRID_DIM, AF_GRID_DIM, t_af, mpix / (t_af / MS_PER_SEC));
	printf("  --------------------------------------------\n");
	printf("  Total (bilinear)  : %6.2f ms (%5.1f FPS)\n", tot_bil, MS_PER_SEC / tot_bil);
	printf("  Total (malvar)    : %6.2f ms (%5.1f FPS)\n", tot_mal, MS_PER_SEC / tot_mal);

	rgb_img_free(&heatmap);
	rgb_img_free(&img);
	raw_frame_free(&raw);
	return 0;
}
