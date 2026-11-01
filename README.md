# raw-camera-isp-pipeline
A camera ISP pipeline and V4L2 capture prototype I made to study image processing && Android's camera stack.
## What works for now?
* Black Level Subtraction (BLC) && Bad Pixel Correction (DPC)
* Bayer demosaicing: Bilinear and Malvar-He-Cutler (src/isp.c)
* 3A algorithms: Gray-World AWB, Luma AE and 3x3 ROI Tenengrad/Laplacian AF
* Color Correction Matrix (CCM) && sRGB Gamma LUT
* V4L2 zero-copy mmap ring buffer capture loop (src/v4l2.c)
* A dummy Android Camera HAL3 request/result session in C++ (src/hal3.cpp)
* JNI bridge for Java CameraCaptureSession testing (src/jni_bridge.c && java/CameraHalBridge.java)
* Python ctypes script for 3A tuning/validation (scripts/tune_isp.py)
## How to compile?
Simple, just guarantee you have `make`, `gcc`, `g++`, `python3` and `jdk` installed.

Then, just do:
```bash
make run
```
Or if you want to test the V4L2 streamer, HAL3 session, JNI bridge and Python script:
```bash
make run-v4l2
make run-hal3
make run-jni
make run-py
```
