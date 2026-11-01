#!/usr/bin/env python3
import ctypes
import json
import os
import sys

BAYER_PATTERNS = {
	"RGGB": 0,
	"GRBG": 1,
	"GBRG": 2,
	"BGGR": 3,
}

TUNE_WIDTH = 320
TUNE_HEIGHT = 240
RAW_DEPTH = 10
BLACK_LEVEL = 64
DEFAULT_GAMMA = 2.2
AF_ZONE_COUNT = 9
AF_CENTER_ZONE = 4
AE_TARGETS = [90.0, 118.0, 145.0]
DEFOCUS_SWEEP = [3.0, 2.0, 1.0, 0.0, 1.0, 2.0]


class RgbPx(ctypes.Structure):
	_fields_ = [
		("r", ctypes.c_uint8),
		("g", ctypes.c_uint8),
		("b", ctypes.c_uint8),
	]


class RawFrame(ctypes.Structure):
	_fields_ = [
		("width", ctypes.c_uint),
		("height", ctypes.c_uint),
		("depth", ctypes.c_uint8),
		("pat", ctypes.c_int),
		("data", ctypes.POINTER(ctypes.c_uint16)),
	]


class RgbImg(ctypes.Structure):
	_fields_ = [
		("width", ctypes.c_uint),
		("height", ctypes.c_uint),
		("px", ctypes.POINTER(RgbPx)),
	]


class AwbStats(ctypes.Structure):
	_fields_ = [
		("r_gain", ctypes.c_double),
		("g_gain", ctypes.c_double),
		("b_gain", ctypes.c_double),
		("avg_r", ctypes.c_double),
		("avg_g", ctypes.c_double),
		("avg_b", ctypes.c_double),
	]


class AeStats(ctypes.Structure):
	_fields_ = [
		("mean_y", ctypes.c_double),
		("target_y", ctypes.c_double),
		("gain", ctypes.c_double),
		("clipped", ctypes.c_uint),
	]


class AfZone(ctypes.Structure):
	_fields_ = [
		("zx", ctypes.c_uint),
		("zy", ctypes.c_uint),
		("tenengrad", ctypes.c_double),
		("lap_var", ctypes.c_double),
	]


class AfStats(ctypes.Structure):
	_fields_ = [
		("global_tenengrad", ctypes.c_double),
		("global_lap_var", ctypes.c_double),
		("zones", AfZone * AF_ZONE_COUNT),
	]


def bind_lib(lib_path):
	lib = ctypes.CDLL(lib_path)

	lib.isp_gamma_init.argtypes = [ctypes.c_double]
	lib.isp_gamma_init.restype = None

	lib.isp_gen_test_frame.argtypes = [ctypes.c_uint, ctypes.c_uint, ctypes.c_double]
	lib.isp_gen_test_frame.restype = RawFrame

	lib.raw_frame_free.argtypes = [ctypes.POINTER(RawFrame)]
	lib.raw_frame_free.restype = None

	lib.rgb_img_free.argtypes = [ctypes.POINTER(RgbImg)]
	lib.rgb_img_free.restype = None

	lib.isp_blc_dpc.argtypes = [ctypes.POINTER(RawFrame), ctypes.c_uint16]
	lib.isp_blc_dpc.restype = None

	lib.isp_demosaic_malvar.argtypes = [ctypes.POINTER(RawFrame)]
	lib.isp_demosaic_malvar.restype = RgbImg

	lib.isp_awb_gray_world.argtypes = [ctypes.POINTER(RgbImg)]
	lib.isp_awb_gray_world.restype = AwbStats

	lib.isp_ae_luma.argtypes = [ctypes.POINTER(RgbImg), ctypes.c_double]
	lib.isp_ae_luma.restype = AeStats

	lib.isp_ccm_gamma.argtypes = [ctypes.POINTER(RgbImg)]
	lib.isp_ccm_gamma.restype = None

	lib.isp_af_eval.argtypes = [ctypes.POINTER(RgbImg), ctypes.POINTER(RgbImg)]
	lib.isp_af_eval.restype = AfStats

	return lib


def main():
	root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
	lib_path = os.path.join(root, "bin", "libisp.so")
	if not os.path.exists(lib_path):
		print(f"error: {lib_path} not found, run 'make lib' first", file=sys.stderr)
		sys.exit(1)

	lib = bind_lib(lib_path)
	lib.isp_gamma_init(DEFAULT_GAMMA)

	report = {"ae_sweep": [], "af_curve": [], "bayer_patterns": {}}
	print(f"[Python-ctypes] Loaded {lib_path} ({TUNE_WIDTH}x{TUNE_HEIGHT})")

	for target in AE_TARGETS:
		raw = lib.isp_gen_test_frame(TUNE_WIDTH, TUNE_HEIGHT, 0.0)
		lib.isp_blc_dpc(ctypes.byref(raw), BLACK_LEVEL)
		rgb = lib.isp_demosaic_malvar(ctypes.byref(raw))
		awb = lib.isp_awb_gray_world(ctypes.byref(rgb))
		ae = lib.isp_ae_luma(ctypes.byref(rgb), target)
		lib.isp_ccm_gamma(ctypes.byref(rgb))

		entry = {
			"target_y": target,
			"mean_y": round(ae.mean_y, 2),
			"ae_gain": round(ae.gain, 3),
			"awb_r": round(awb.r_gain, 3),
			"awb_b": round(awb.b_gain, 3),
		}
		report["ae_sweep"].append(entry)
		print(f"  AE target={target:5.1f} -> gain={ae.gain:.3f}x | AWB=({awb.r_gain:.2f}, {awb.b_gain:.2f})")

		lib.rgb_img_free(ctypes.byref(rgb))
		lib.raw_frame_free(ctypes.byref(raw))

	for sigma in DEFOCUS_SWEEP:
		raw = lib.isp_gen_test_frame(TUNE_WIDTH, TUNE_HEIGHT, sigma)
		lib.isp_blc_dpc(ctypes.byref(raw), BLACK_LEVEL)
		rgb = lib.isp_demosaic_malvar(ctypes.byref(raw))
		af = lib.isp_af_eval(ctypes.byref(rgb), None)
		score = af.zones[AF_CENTER_ZONE].tenengrad
		report["af_curve"].append({"sigma": sigma, "tenengrad": round(score, 1)})
		lib.rgb_img_free(ctypes.byref(rgb))
		lib.raw_frame_free(ctypes.byref(raw))

	for name, pat_id in BAYER_PATTERNS.items():
		raw = lib.isp_gen_test_frame(TUNE_WIDTH, TUNE_HEIGHT, 0.0)
		raw.pat = pat_id
		lib.isp_blc_dpc(ctypes.byref(raw), BLACK_LEVEL)
		rgb = lib.isp_demosaic_malvar(ctypes.byref(raw))
		awb = lib.isp_awb_gray_world(ctypes.byref(rgb))
		report["bayer_patterns"][name] = {
			"r_gain": round(awb.r_gain, 3),
			"b_gain": round(awb.b_gain, 3),
		}
		lib.rgb_img_free(ctypes.byref(rgb))
		lib.raw_frame_free(ctypes.byref(raw))

	peak = max(report["af_curve"], key=lambda x: x["tenengrad"])
	print(f"  AF sharpness peak at sigma={peak['sigma']:.1f} (tenengrad={peak['tenengrad']:.1f})")

	out_dir = os.path.join(root, "output")
	os.makedirs(out_dir, exist_ok=True)
	out_file = os.path.join(out_dir, "tuning_report.json")
	with open(out_file, "w", encoding="utf-8") as f:
		json.dump(report, f, indent=2)
	print(f"[Python-ctypes] Saved telemetry report to output/tuning_report.json")


if __name__ == "__main__":
	main()
