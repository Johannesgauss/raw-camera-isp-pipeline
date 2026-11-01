public class CameraHalBridge {
	static {
		System.loadLibrary("isp_jni");
	}

	private static final int DEFAULT_WIDTH = 320;
	private static final int DEFAULT_HEIGHT = 240;
	private static final double TARGET_LUMA = 118.0;
	private static final int STATS_LEN = 6;
	private static final int[] DAC_STEPS = {150, 250, 325, 400, 475, 550};

	public static native long createSession(int width, int height);
	public static native int submitRequest(long handle, int dacPos, double targetLuma, double[] statsOut);
	public static native void destroySession(long handle);

	public static void main(String[] args) {
		int width = DEFAULT_WIDTH;
		int height = DEFAULT_HEIGHT;
		if (args.length >= 2) {
			width = Integer.parseInt(args[0]);
			height = Integer.parseInt(args[1]);
		}

		long session = createSession(width, height);
		if (session == 0) {
			System.err.println("Failed to allocate native ISP session");
			System.exit(1);
		}

		System.out.printf("[Java-JNI] CameraCaptureSession started (%dx%d)%n", width, height);
		double[] stats = new double[STATS_LEN];
		int bestDac = DAC_STEPS[0];
		double bestAf = -1.0;

		for (int i = 0; i < DAC_STEPS.length; i++) {
			int dac = DAC_STEPS[i];
			int rc = submitRequest(session, dac, TARGET_LUMA, stats);
			if (rc != 0)
				break;

			double rGain = stats[0];
			double gGain = stats[1];
			double bGain = stats[2];
			double meanY = stats[3];
			double aeGain = stats[4];
			double afScore = stats[5];

			if (afScore > bestAf) {
				bestAf = afScore;
				bestDac = dac;
			}

			System.out.printf("  onCaptureCompleted(req=%d, dac=%d) -> AWB=(%.2f, %.2f, %.2f) AE=%.2fx (Y=%.1f) AF=%.1f%n",
			                  i + 1, dac, rGain, gGain, bGain, aeGain, meanY, afScore);
		}

		System.out.printf("[Java-JNI] AF locked at DAC=%d (score=%.1f)%n", bestDac, bestAf);
		destroySession(session);
	}
}
