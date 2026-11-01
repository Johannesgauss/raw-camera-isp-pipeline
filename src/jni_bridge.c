#include "isp.h"
#include <jni.h>
#include <stdlib.h>
#include <math.h>

struct jni_cam_ctx {
	uint width;
	uint height;
	ushort black_lvl;
};

JNIEXPORT jlong JNICALL Java_CameraHalBridge_createSession(JNIEnv *env, jclass cls, jint width, jint height)
{
	(void)env;
	(void)cls;
	struct jni_cam_ctx *ctx = calloc(1, sizeof(*ctx));
	if (!ctx)
		return 0;
	ctx->width = (uint)width;
	ctx->height = (uint)height;
	ctx->black_lvl = RAW_BLACK_LEVEL;
	isp_gamma_init(DEFAULT_GAMMA);
	return (jlong)(umax)ctx;
}

JNIEXPORT jint JNICALL Java_CameraHalBridge_submitRequest(JNIEnv *env, jclass cls, jlong handle, jint dac_pos, jdouble target_luma, jdoubleArray stats_out)
{
	(void)cls;
	struct jni_cam_ctx *ctx = (struct jni_cam_ctx *)(umax)handle;
	if (!ctx || !stats_out)
		return -1;

	double sigma = fabs((double)(dac_pos - HAL3_DAC_OPT)) / HAL3_DAC_SIGMA_SCALE;
	struct raw_frame raw = isp_gen_test_frame(ctx->width, ctx->height, sigma);
	isp_blc_dpc(&raw, ctx->black_lvl);

	struct rgb_img rgb = isp_demosaic_bilinear(&raw);
	struct awb_stats awb = isp_awb_gray_world(&rgb);
	struct ae_stats ae = isp_ae_luma(&rgb, (double)target_luma);
	isp_ccm_gamma(&rgb);
	struct af_stats af = isp_af_eval(&rgb, NULL);

	jdouble buf[JNI_STATS_LEN];
	buf[0] = awb.r_gain;
	buf[1] = awb.g_gain;
	buf[2] = awb.b_gain;
	buf[3] = ae.mean_y;
	buf[4] = ae.gain;
	buf[5] = af.zones[AF_CENTER_ZONE].tenengrad;

	(*env)->SetDoubleArrayRegion(env, stats_out, 0, JNI_STATS_LEN, buf);

	rgb_img_free(&rgb);
	raw_frame_free(&raw);
	return 0;
}

JNIEXPORT void JNICALL Java_CameraHalBridge_destroySession(JNIEnv *env, jclass cls, jlong handle)
{
	(void)env;
	(void)cls;
	struct jni_cam_ctx *ctx = (struct jni_cam_ctx *)(umax)handle;
	if (ctx)
		free(ctx);
}
