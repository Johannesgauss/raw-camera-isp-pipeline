#ifndef HAL3_HPP
#define HAL3_HPP

#include "isp.h"

enum hal3_template_t {
	HAL3_TEMPLATE_PREVIEW = 1,
	HAL3_TEMPLATE_STILL_CAPTURE = 2,
	HAL3_TEMPLATE_VIDEO_RECORD = 3
};

enum hal3_af_state_t {
	AF_STATE_INACTIVE = 0,
	AF_STATE_PASSIVE_SCAN = 1,
	AF_STATE_ACTIVE_SCAN = 3,
	AF_STATE_FOCUSED_LOCKED = 4
};

enum hal3_ae_state_t {
	AE_STATE_INACTIVE = 0,
	AE_STATE_SEARCHING = 1,
	AE_STATE_CONVERGED = 2
};

enum hal3_awb_state_t {
	AWB_STATE_INACTIVE = 0,
	AWB_STATE_SEARCHING = 1,
	AWB_STATE_CONVERGED = 2
};

struct hal3_request {
	uint frame_number;
	hal3_template_t tpl;
	bool af_trigger;
	double target_luma;
};

struct hal3_result {
	uint frame_number;
	int lens_dac;
	uint exposure_us;
	uint sensor_iso;
	hal3_af_state_t af_state;
	hal3_ae_state_t ae_state;
	hal3_awb_state_t awb_state;
	awb_stats awb;
	ae_stats ae;
	double af_score;
	double proc_ms;
};

class Hal3Session {
public:
	Hal3Session(uint width, uint height);
	~Hal3Session();

	hal3_result process_capture_request(hal3_request *req);

private:
	uint m_width;
	uint m_height;
	int m_dac;
	int m_dac_dir;
	double m_prev_af;
	double m_exp_gain;
	hal3_af_state_t m_af_state;
	hal3_ae_state_t m_ae_state;
	hal3_awb_state_t m_awb_state;
};

#endif // HAL3_HPP
