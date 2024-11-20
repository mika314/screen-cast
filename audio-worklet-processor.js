class AudioProcessor extends AudioWorkletProcessor {
    constructor() {
        super();
        // Initialize separate buffers for each channel
        this.buffers = [new Float32Array(0), new Float32Array(0)];
        this.maxBufferLength = 8 * 1024;

        this.port.onmessage = (event) => {
            const float32Array = new Float32Array(event.data);
            const numChannels = 2; // Stereo
            const samplesPerChannel = float32Array.length / numChannels;

            // Split interleaved data into separate channels
            const newSamples = [new Float32Array(samplesPerChannel), new Float32Array(samplesPerChannel)];
            for (let i = 0; i < samplesPerChannel; i++) {
                newSamples[0][i] = float32Array[i * numChannels];
                newSamples[1][i] = float32Array[i * numChannels + 1];
            }

            for (let c = 0; c < numChannels; c++) {
                // Check for overflow and clear buffer if necessary
                if (this.buffers[c].length + newSamples[c].length > this.maxBufferLength) {
                    console.log(`Buffer overflow detected on channel ${c}. Clearing buffer.`);
                    this.buffers[c] = new Float32Array(0);
                }

                // Append new samples to the buffer
                this.buffers[c] = concatFloat32Arrays(this.buffers[c], newSamples[c]);
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
