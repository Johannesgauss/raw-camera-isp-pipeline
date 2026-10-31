#include "isp.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <linux/videodev2.h>

struct v4l2_buf {
	void *start;
	umax len;
};

static double s_sim_sigmas[V4L2_DEFAULT_FRAMES] = {
	2.4, 1.8, 1.2, 0.6, 0.2, 0.0, 0.0, 0.0
};

static int xioctl(int fd, umax req, void *arg)
{
	int r;
	do {
		r = ioctl(fd, req, arg);
	} while (r == -1 && errno == EINTR);
	return r;
}

static inline utiny clamp_utiny(int v)
{
	if (v < 0) return 0;
	if (v > MAX_U8) return MAX_U8;
	return (utiny)v;
}

static struct rgb_img yuyv_to_rgb(utiny *yuyv, uint width, uint height)
{
	struct rgb_img img = rgb_img_alloc(width, height);
	for (uint y = 0; y < height; y++) {
		utiny *row = yuyv + (umax)y * width * YUYV_BPP;
		for (uint x = 0; x < width; x += YUYV_BPP) {
			int y0 = row[x * YUYV_BPP + 0] - YUV_Y_OFFSET;
			int u  = row[x * YUYV_BPP + 1] - YUV_UV_OFFSET;
			int y1 = row[x * YUYV_BPP + 2] - YUV_Y_OFFSET;
			int v  = row[x * YUYV_BPP + 3] - YUV_UV_OFFSET;

			*rgb_at(&img, x, y) = (struct rgb_px){
				clamp_utiny((YUV_C_Y * y0 + YUV_C_RV * v + YUV_ROUND) >> YUV_SHIFT),
				clamp_utiny((YUV_C_Y * y0 - YUV_C_GU * u - YUV_C_GV * v + YUV_ROUND) >> YUV_SHIFT),
				clamp_utiny((YUV_C_Y * y0 + YUV_C_BU * u + YUV_ROUND) >> YUV_SHIFT)
			};
			if (x + 1 < width) {
				*rgb_at(&img, x + 1, y) = (struct rgb_px){
					clamp_utiny((YUV_C_Y * y1 + YUV_C_RV * v + YUV_ROUND) >> YUV_SHIFT),
					clamp_utiny((YUV_C_Y * y1 - YUV_C_GU * u - YUV_C_GV * v + YUV_ROUND) >> YUV_SHIFT),
					clamp_utiny((YUV_C_Y * y1 + YUV_C_BU * u + YUV_ROUND) >> YUV_SHIFT)
				};
			}
		}
	}
	return img;
}

static int v4l2_sim_stream(uint width, uint height, int frames)
{
	struct v4l2_buf ring[V4L2_RING_COUNT];
	umax fbytes = (umax)width * height * sizeof(ushort);

	printf("[v4l2-sim] mapping %d ring buffers (%llu bytes each)\n", V4L2_RING_COUNT, fbytes);
	for (int i = 0; i < V4L2_RING_COUNT; i++) {
		void *p = mmap(NULL, fbytes, PROT_READ | PROT_WRITE,
		               MAP_SHARED | MAP_ANONYMOUS, -1, 0);
		if (p == MAP_FAILED) {
			perror("mmap");
			return 1;
		}
		ring[i].start = p;
		ring[i].len = fbytes;
	}

	isp_gamma_init(DEFAULT_GAMMA);

	for (int f = 0; f < frames; f++) {
		int bi = f % V4L2_RING_COUNT;
		int si = (f < V4L2_DEFAULT_FRAMES) ? f : (V4L2_DEFAULT_FRAMES - 1);
		double sig = s_sim_sigmas[si];

		struct raw_frame src = isp_gen_test_frame(width, height, sig);
		memcpy(ring[bi].start, src.data, fbytes);
		raw_frame_free(&src);

		struct raw_frame dma = raw_frame_alloc(width, height, RAW_DEFAULT_DEPTH, BAYER_RGGB);
		memcpy(dma.data, ring[bi].start, fbytes);

		isp_blc_dpc(&dma, RAW_BLACK_LEVEL);
		struct rgb_img rgb = isp_demosaic_bilinear(&dma);
		struct awb_stats awb = isp_awb_gray_world(&rgb);
		struct ae_stats ae = isp_ae_luma(&rgb, AE_TARGET_LUMA);
		isp_ccm_gamma(&rgb);
		struct af_stats af = isp_af_eval(&rgb, NULL);

		printf("  frame %02d [buf %d] sigma=%.2f | awb=(%.2f, %.2f, %.2f) | ae=%.2fx | af=%.1f\n",
		       f + 1, bi, sig, awb.r_gain, awb.g_gain, awb.b_gain,
		       ae.gain, af.zones[AF_CENTER_ZONE].tenengrad);

		rgb_img_free(&rgb);
		raw_frame_free(&dma);
	}

	for (int i = 0; i < V4L2_RING_COUNT; i++) {
		munmap(ring[i].start, ring[i].len);
	}
	return 0;
}

int main(int argc, char **argv)
{
	char *dev = V4L2_DEFAULT_DEV;
	bool sim = false;
	uint width = V4L2_DEFAULT_WIDTH;
	uint height = V4L2_DEFAULT_HEIGHT;
	int frames = V4L2_DEFAULT_FRAMES;

	for (int i = 1; i < argc; i++) {
		if (!strcmp(argv[i], "--simulate") || !strcmp(argv[i], "-s"))
			sim = true;
		else if (!strcmp(argv[i], "--device") && i + 1 < argc)
			dev = argv[++i];
		else if (!strcmp(argv[i], "--width") && i + 1 < argc)
			width = (uint)strtoul(argv[++i], NULL, 10);
		else if (!strcmp(argv[i], "--height") && i + 1 < argc)
			height = (uint)strtoul(argv[++i], NULL, 10);
		else if (!strcmp(argv[i], "--frames") && i + 1 < argc)
			frames = (int)strtol(argv[++i], NULL, 10);
	}

	if (sim)
		return v4l2_sim_stream(width, height, frames);

	int fd = open(dev, O_RDWR | O_NONBLOCK, 0);
	if (fd == -1) {
		printf("[v4l2] cannot open %s (%s), falling back to mmap sim\n", dev, strerror(errno));
		return v4l2_sim_stream(width, height, frames);
	}

	struct v4l2_capability cap = {0};
	if (xioctl(fd, VIDIOC_QUERYCAP, &cap) == -1 || !(cap.capabilities & V4L2_CAP_VIDEO_CAPTURE)) {
		close(fd);
		return v4l2_sim_stream(width, height, frames);
	}

	struct v4l2_format fmt = {0};
	fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	fmt.fmt.pix.width = width;
	fmt.fmt.pix.height = height;
	fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_YUYV;
	fmt.fmt.pix.field = V4L2_FIELD_NONE;

	if (xioctl(fd, VIDIOC_S_FMT, &fmt) == -1) {
		close(fd);
		return v4l2_sim_stream(width, height, frames);
	}

	width = fmt.fmt.pix.width;
	height = fmt.fmt.pix.height;

	struct v4l2_requestbuffers req = {0};
	req.count = V4L2_RING_COUNT;
	req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	req.memory = V4L2_MEMORY_MMAP;

	if (xioctl(fd, VIDIOC_REQBUFS, &req) == -1 || req.count < V4L2_MIN_BUFS) {
		close(fd);
		return v4l2_sim_stream(width, height, frames);
	}

	struct v4l2_buf *bufs = calloc(req.count, sizeof(*bufs));
	for (uint i = 0; i < req.count; i++) {
		struct v4l2_buffer b = {0};
		b.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		b.memory = V4L2_MEMORY_MMAP;
		b.index = i;

		if (xioctl(fd, VIDIOC_QUERYBUF, &b) == -1) {
			free(bufs);
			close(fd);
			return v4l2_sim_stream(width, height, frames);
		}

		bufs[i].len = b.length;
		bufs[i].start = mmap(NULL, b.length, PROT_READ | PROT_WRITE, MAP_SHARED, fd, b.m.offset);
		xioctl(fd, VIDIOC_QBUF, &b);
	}

	enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	xioctl(fd, VIDIOC_STREAMON, &type);
	isp_gamma_init(DEFAULT_GAMMA);

	for (int f = 0; f < frames; f++) {
		struct pollfd pfd = {fd, POLLIN, 0};
		if (poll(&pfd, 1, V4L2_POLL_TIMEOUT_MS) <= 0)
			break;

		struct v4l2_buffer b = {0};
		b.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		b.memory = V4L2_MEMORY_MMAP;
		if (xioctl(fd, VIDIOC_DQBUF, &b) == -1)
			break;

		struct rgb_img rgb = yuyv_to_rgb(bufs[b.index].start, width, height);
		struct awb_stats awb = isp_awb_gray_world(&rgb);
		struct ae_stats ae = isp_ae_luma(&rgb, AE_TARGET_LUMA);
		isp_ccm_gamma(&rgb);
		struct af_stats af = isp_af_eval(&rgb, NULL);

		printf("  frame %02d [buf %u] awb=(%.2f, %.2f, %.2f) ae=%.2fx af=%.1f\n",
		       f + 1, b.index, awb.r_gain, awb.g_gain, awb.b_gain, ae.gain, af.global_tenengrad);

		rgb_img_free(&rgb);
		xioctl(fd, VIDIOC_QBUF, &b);
	}

	xioctl(fd, VIDIOC_STREAMOFF, &type);
	for (uint i = 0; i < req.count; i++) {
		munmap(bufs[i].start, bufs[i].len);
	}
	free(bufs);
	close(fd);
	return 0;
}
