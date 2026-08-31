const canvas = document.getElementById('videoCanvas');
const startButton = document.getElementById('startButton');
const fullscreenToggle = document.getElementById('fullscreenToggle');
const ctx = canvas.getContext('2d');
let audioContext = null;
let videoDecoder = null;
let audioDecoder = null;
let ws;
// Start fullscreen, audio, and the WebSocket connection.
// Keep fullscreen best-effort: on mobile browsers it may be unavailable, and
// awaiting AudioWorklet setup first can consume the transient user activation
// required by requestFullscreen().
window.startScreenCastClient = async () => {
    window.screenCastActive = false;
    startButton.style.display = 'none';

    if (!audioContext || audioContext.state === 'closed') {
        audioContext = new AudioContext({ sampleRate: 48000, latencyHint: 'interactive' });
        console.log('AudioContext sample rate:', audioContext.sampleRate);

        try {
            await audioContext.audioWorklet.addModule('/ui/screen-cast-audio-worklet.js');
            const audioNode = new AudioWorkletNode(audioContext, 'audio-processor', {
                outputChannelCount: [2],
            });
            audioNode.connect(audioContext.destination);
            window.audioNode = audioNode;
            console.log('AudioWorklet loaded and connected');
        } catch (err) {
            // Video should still work if the optional audio path fails.
            console.error('Failed to load AudioWorklet; continuing without audio:', err);
        }
    } else if (audioContext.state === 'suspended') {
        try {
            await audioContext.resume();
        } catch (err) {
            console.warn('Could not resume AudioContext:', err);
        }
    }

    console.log('connecting to screen-cast WebSocket');
    ws = new WebSocket((location.protocol === 'https:' ? 'wss://' : 'ws://') + location.host + '/ui/api/screen-cast/ws');
    ws.binaryType = 'arraybuffer';

    ws.onopen = async function() {
        window.screenCastActive = true;
        console.log('WebSocket connection opened');
        // Screen Cast is view-only. Input is intentionally disabled.
    };

    ws.onclose = function() { window.screenCastActive = false; };

    ws.onmessage = async function(event) {
        const data = event.data;
        const buffer = new Uint8Array(data);

        const messageType = buffer[0];

        if (messageType === 0x01) {
            const videoData = buffer.slice(1);

            if (!videoDecoder) {
                const videoConfig = {
                    codec: 'avc1.42E01E',
                    codedWidth: 1920,
                    codedHeight: 1080,
                    hardwareAcceleration: 'no-preference'
                };

                try {
                    const support = await VideoDecoder.isConfigSupported(videoConfig);
                    if (!support.supported) {
                        console.error('Configuration not supported:', support.config);
                        return;
                    }
                } catch (err) {
                    console.error('Error checking configuration:', err);
                    return;
                }

                try {
                    videoDecoder = new VideoDecoder({
                        output: frame => {
                            ctx.drawImage(frame, 0, 0, canvas.width, canvas.height);
                            frame.close();
                        },
                        error: err => {
                            console.error('Decoder error:', err);
                        }
                    });
                    console.log('VideoDecoder created');
                } catch (err) {
                    console.error('Error creating decoder:', err);
                    return;
                }
                try {
                    videoDecoder.configure(videoConfig);
                    console.log('VideoDecoder configured');
                } catch (err) {
                    console.error('Error configuring decoder:', err);
                    return;
                }
            }

            // Correctly identify key frames by searching for SPS (7) or IDR (5) NAL units
            let isKeyFrame = false;
            for (let i = 0; i < Math.min(videoData.length - 4, 100); i++) {
                if (videoData[i] === 0 && videoData[i + 1] === 0 && videoData[i + 2] === 1) {
                    const type = videoData[i + 3] & 0x1F;
                    if (type === 5 || type === 7) {
                        isKeyFrame = true;
                        break;
                    }
                } else if (videoData[i] === 0 && videoData[i + 1] === 0 && videoData[i + 2] === 0 && videoData[i + 3] === 1) {
                    const type = videoData[i + 4] & 0x1F;
                    if (type === 5 || type === 7) {
                        isKeyFrame = true;
                        break;
                    }
                }
            }

            const chunk = new EncodedVideoChunk({
                type: isKeyFrame ? 'key' : 'delta',
                timestamp: performance.now() * 1000, // Timestamps should be in microseconds
                data: videoData
            });

            videoDecoder.decode(chunk);
        } else if (messageType === 0x02) {
            const opusData = buffer.slice(1);

            if (!audioDecoder){
                const audioConfig = {
                    codec: 'opus',
                    sampleRate: 48000,
                    numberOfChannels: 2,
                };
                try {
                    const support = await AudioDecoder.isConfigSupported(audioConfig);
                    if (!support.supported) {
                        console.error('Opus configuration not supported:', support.config);
                        return;
                    }
                } catch (err) {
                    console.error('Error checking Opus configuration:', err);
                    return;
                }
                try {
                    audioDecoder = new AudioDecoder({
                        output: (audioData) => {
                            // Create a Float32Array to hold the audio samples
                            const numChannels = audioData.numberOfChannels;
                            const numFrames = audioData.numberOfFrames;
                            const format = audioData.format; // Should be 'f32' (float32)

                            const audioBuffer = new Float32Array(numFrames * numChannels);

                            // Copy the data from the AudioData object
                            audioData.copyTo(audioBuffer, {
                                planeIndex: 0, // Only plane 0 for interleaved formats like f32
                                format: format,
                            });

                            audioData.close(); // Free the AudioData resource

                            // Send the extracted data to the AudioWorkletNode
                            if (window.audioNode && window.audioNode.port) {
                                window.audioNode.port.postMessage(audioBuffer.buffer, [audioBuffer.buffer]); // Transfer the ArrayBuffer
                            }
                        },
                        error: (err) => {
                            console.error('AudioDecoder error:', err);
                        },
                    });
                    audioDecoder.configure(audioConfig);
                } catch (err) {
                    console.error('Error creating AudioDecoder:', err);
                }

            }

            if (audioDecoder) {
                try {
                    const chunk = new EncodedAudioChunk({
                        type: 'key', // All Opus packets are treated as key frames
                        timestamp: performance.now(),
                        data: opusData,
                    });

                    audioDecoder.decode(chunk);
                } catch (err) {
                    console.error('Error decoding audio chunk:', err);
                }
            }
        } else {
            console.error('Unknown message type:', messageType);
        }
    };
};

// Standalone Screen Cast fullscreen support. The HITL UI has its own control.
if (fullscreenToggle) fullscreenToggle.addEventListener('click', async (event) => {
    event.stopPropagation();
    const target = document.getElementById('screenFrame') || canvas;
    if (document.fullscreenElement) {
        try { await document.exitFullscreen(); } catch (err) { console.error('Error exiting fullscreen:', err); }
    } else if (document.fullscreenEnabled && target) {
        try { await target.requestFullscreen(); console.log('Screen Cast fullscreen activated'); }
        catch (err) { console.error('Error entering Screen Cast fullscreen:', err); }
    }
});

let isDragging = false;
let offsetX, offsetY;

