#include "hal3.hpp"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <ctime>

static inline double now_ms()
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (double)ts.tv_sec * MS_PER_SEC + (double)ts.tv_nsec / NS_PER_MS;
}

static char *af_state_str(hal3_af_state_t st)
{
	if (st == AF_STATE_FOCUSED_LOCKED) return (char *)"FOCUSED_LOCKED";
	if (st == AF_STATE_ACTIVE_SCAN) return (char *)"ACTIVE_SCAN   ";
	if (st == AF_STATE_PASSIVE_SCAN) return (char *)"PASSIVE_SCAN  ";
	return (char *)"INACTIVE      ";
}

static char *ae_state_str(hal3_ae_state_t st)
{
	if (st == AE_STATE_CONVERGED) return (char *)"CONVERGED";
	if (st == AE_STATE_SEARCHING) return (char *)"SEARCHING";
	return (char *)"INACTIVE ";
}

Hal3Session::Hal3Session(uint width, uint height)
{
	m_width = width;
	m_height = height;
	m_dac = HAL3_DAC_INIT;
	m_dac_dir = 1;
	m_prev_af = 0.0;
	m_exp_gain = 1.0;
	m_af_state = AF_STATE_INACTIVE;
	m_ae_state = AE_STATE_SEARCHING;
	m_awb_state = AWB_STATE_SEARCHING;
	isp_gamma_init(DEFAULT_GAMMA);
}

Hal3Session::~Hal3Session()
{
}

hal3_result Hal3Session::process_capture_request(hal3_request *req)
{
	double t0 = now_ms();

	if (req->af_trigger && m_af_state == AF_STATE_INACTIVE)
		m_af_state = AF_STATE_ACTIVE_SCAN;

	double sigma = fabs((double)(m_dac - HAL3_DAC_OPT)) / HAL3_DAC_SIGMA_SCALE;
	raw_frame raw = isp_gen_test_frame(m_width, m_height, sigma);
	isp_blc_dpc(&raw, RAW_BLACK_LEVEL);

	rgb_img rgb = isp_demosaic_bilinear(&raw);
	awb_stats awb = isp_awb_gray_world(&rgb);
	ae_stats ae = isp_ae_luma(&rgb, req->target_luma * m_exp_gain);
	isp_ccm_gamma(&rgb);
	af_stats af = isp_af_eval(&rgb, NULL);

	double score = af.zones[AF_CENTER_ZONE].tenengrad;
	int cur_dac = m_dac;

	if (m_af_state == AF_STATE_ACTIVE_SCAN) {
		if (m_prev_af > 0.0 && score < m_prev_af) {
			m_dac -= m_dac_dir * HAL3_DAC_STEP;
			cur_dac = m_dac;
			m_af_state = AF_STATE_FOCUSED_LOCKED;
		} else {
			m_prev_af = score;
			if (m_dac == HAL3_DAC_OPT) {
				m_af_state = AF_STATE_FOCUSED_LOCKED;
			} else {
				m_dac += m_dac_dir * HAL3_DAC_STEP;
				if (m_dac > HAL3_DAC_MAX) m_dac = HAL3_DAC_MAX;
				if (m_dac < HAL3_DAC_MIN) m_dac = HAL3_DAC_MIN;
			}
		}
	}

	double cur_y = ae.mean_y * m_exp_gain;
	double luma_err = req->target_luma - cur_y;
	if (fabs(luma_err) <= HAL3_AE_TOLERANCE) {
		m_ae_state = AE_STATE_CONVERGED;
	} else {
		m_ae_state = AE_STATE_SEARCHING;
		m_exp_gain += HAL3_AE_KP * ((req->target_luma / (cur_y > 1.0 ? cur_y : 1.0)) - 1.0) * m_exp_gain;
		if (m_exp_gain < AE_MIN_GAIN) m_exp_gain = AE_MIN_GAIN;
		if (m_exp_gain > AE_MAX_GAIN) m_exp_gain = AE_MAX_GAIN;
	}

	m_awb_state = AWB_STATE_CONVERGED;

	hal3_result res;
	res.frame_number = req->frame_number;
	res.lens_dac = cur_dac;
	res.exposure_us = (uint)(HAL3_BASE_EXP_US * m_exp_gain);
	res.sensor_iso = (uint)(HAL3_BASE_ISO * m_exp_gain);
	res.af_state = m_af_state;
	res.ae_state = m_ae_state;
	res.awb_state = m_awb_state;
	res.awb = awb;
	res.ae = ae;
	res.af_score = score;
	res.proc_ms = now_ms() - t0;

	rgb_img_free(&rgb);
	raw_frame_free(&raw);
	return res;
}

int main(int argc, char **argv)
{
	uint width = HAL3_DEFAULT_WIDTH;
	uint height = HAL3_DEFAULT_HEIGHT;
	uint frames = HAL3_DEFAULT_FRAMES;

	if (argc >= 3) {
		width = (uint)strtoul(argv[1], NULL, 10);
		height = (uint)strtoul(argv[2], NULL, 10);
	}
	if (argc >= 4)
		frames = (uint)strtoul(argv[3], NULL, 10);

	printf("[HAL3] Opening camera3_device_t session (%ux%u, %u capture requests)\n", width, height, frames);
	Hal3Session session(width, height);

	for (uint f = 0; f < frames; f++) {
		hal3_request req;
		req.frame_number = f + 1;
		req.tpl = (f + 1 == frames) ? HAL3_TEMPLATE_STILL_CAPTURE : HAL3_TEMPLATE_PREVIEW;
		req.af_trigger = (f == 0);
		req.target_luma = AE_TARGET_LUMA;

		hal3_result res = session.process_capture_request(&req);
		printf("  req #%02u | AF=%s (dac=%3d, af=%8.1f) | AE=%s (exp=%5uus, iso=%3u) | AWB=(%.2f, %.2f) | %5.2f ms\n",
		       res.frame_number, af_state_str(res.af_state), res.lens_dac, res.af_score,
		       ae_state_str(res.ae_state), res.exposure_us, res.sensor_iso,
		       res.awb.r_gain, res.awb.b_gain, res.proc_ms);
	}

	return 0;
}
