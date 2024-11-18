class AudioProcessor extends AudioWorkletProcessor {
    constructor() {
        super();
        // Initialize separate buffers for each channel
        this.buffers = [new Float32Array(0), new Float32Array(0)];
        this.maxBufferLength = 48000 / 12;

        this.port.onmessage = event => {
            const audioData = event.data;
            // Assuming 16-bit PCM, stereo, 48kHz
            const int16Array = new Int16Array(audioData.buffer);
            const float32Array = new Float32Array(int16Array.length);

            for (let i = 0; i < int16Array.length; i++) {
                float32Array[i] = int16Array[i] / 32768;
            }

            const numChannels = 2; // Stereo
            const samplesPerChannel = float32Array.length / numChannels;

            // Split interleaved data into separate channels
            const newSamples = [new Float32Array(samplesPerChannel), new Float32Array(samplesPerChannel)];
            for (let i = 0; i < samplesPerChannel; i++) {
                newSamples[0][i] = float32Array[i * numChannels];
                newSamples[1][i] = float32Array[i * numChannels + 1];
            }

            // Append to buffers
            for (let c = 0; c < numChannels; c++) {
                this.buffers[c] = concatFloat32Arrays(this.buffers[c], newSamples[c]);

                // Prevent buffer from growing indefinitely
                if (this.buffers[c].length > this.maxBufferLength) {
                    console.log("Drop the oldest samples");
                    this.buffers[c] = this.buffers[c].subarray(this.buffers[c].length - this.maxBufferLength);
                }
            }
        };
    }

    process(inputs, outputs, parameters) {
        const output = outputs[0];
        const numChannels = output.length;
        const samplesPerChannel = output[0].length;

        for (let c = 0; c < numChannels; c++) {
            if (this.buffers[c].length >= samplesPerChannel) {
                // Copy samples to output
                output[c].set(this.buffers[c].subarray(0, samplesPerChannel));
                // Remove the copied samples from the buffer
                this.buffers[c] = this.buffers[c].subarray(samplesPerChannel);
            } else {
                // Not enough samples, output silence
                output[c].fill(0);
            }
        }

        return true;
    }
}

function concatFloat32Arrays(a, b) {
    const c = new Float32Array(a.length + b.length);
    c.set(a, 0);
    c.set(b, a.length);
    return c;
}

registerProcessor('audio-processor', AudioProcessor);
