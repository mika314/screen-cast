const canvas = document.getElementById('videoCanvas');
const startButton = document.getElementById('startButton');
const fullscreenToggle = document.getElementById('fullscreenToggle');
const ctx = canvas.getContext('2d');

let audioContext = null;
let decoder = null;

// Desired aspect ratio
const aspectRatio = 16 / 9;

// Function to resize canvas while maintaining aspect ratio
function resizeCanvas() {
    const windowWidth = window.innerWidth;
    const windowHeight = window.innerHeight;

    if (windowWidth / windowHeight > aspectRatio) {
        // Window is too wide, adjust based on height
        canvas.height = windowHeight;
        canvas.width = windowHeight * aspectRatio;
    } else {
        // Window is too tall, adjust based on width
        canvas.width = windowWidth;
        canvas.height = windowWidth / aspectRatio;
    }

    canvas.style.width = `${canvas.width}px`;
    canvas.style.height = `${canvas.height}px`;

    console.log(`Canvas resized to: ${canvas.width}x${canvas.height}`);
}

// Resize canvas on window resize
window.addEventListener('resize', resizeCanvas);

// Handle start button for initial fullscreen and WebSocket setup
startButton.addEventListener('click', async () => {
    resizeCanvas(); // Ensure canvas is resized before starting

    if (!audioContext || audioContext.state === 'closed') {
        audioContext = new AudioContext({ sampleRate: 48000, latencyHint: 'interactive' });
        console.log('AudioContext sample rate:', audioContext.sampleRate);

        try {
            await audioContext.audioWorklet.addModule('audio-worklet-processor.js');
            const audioNode = new AudioWorkletNode(audioContext, 'audio-processor', {
                outputChannelCount: [2], // Explicitly specify two output channels
            });
            audioNode.connect(audioContext.destination);
            window.audioNode = audioNode;
            console.log('AudioWorklet loaded and connected');
        } catch (err) {
            console.error('Failed to load AudioWorklet:', err);
            return;
        }
    } else if (audioContext.state === 'suspended') {
        await audioContext.resume();
    }

    if (document.fullscreenEnabled) {
        try {
            await document.body.requestFullscreen();
            console.log('Fullscreen activated');
        } catch (err) {
            console.error('Error attempting fullscreen:', err);
            return;
        }
    } else {
        console.error('Fullscreen API is not supported by this browser.');
        return;
    }

    startButton.style.display = 'none';

    console.log("connecting to ws://localhost:8090/");
    const ws = new WebSocket('ws://localhost:8090/');
    ws.binaryType = 'arraybuffer';

    ws.onopen = async function() {
        console.log('WebSocket connection opened');
    };

    ws.onmessage = async function(event) {
        const data = event.data;
        const buffer = new Uint8Array(data);

        const messageType = buffer[0];

        if (messageType === 0x01) {
            const videoData = buffer.slice(1);

            if (!decoder) {
                const config = {
                    codec: 'avc1.42E01E',
                    codedWidth: 1920,
                    codedHeight: 1080,
                    hardwareAcceleration: 'no-preference'
                };

                try {
                    const support = await VideoDecoder.isConfigSupported(config);
                    if (!support.supported) {
                        console.error('Configuration not supported:', support.config);
                        return;
                    }
                } catch (err) {
                    console.error('Error checking configuration:', err);
                    return;
                }

                try {
                    decoder = new VideoDecoder({
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
                    decoder.configure(config);
                    console.log('VideoDecoder configured');
                } catch (err) {
                    console.error('Error configuring decoder:', err);
                    return;
                }
            }

            // Parse NAL units to determine frame type
            if (videoData.length < 5) {
                console.error('Video data too short to determine frame type');
                return;
            }

            const type = videoData[4] & 0x1F;
            const frameType = (type === 5) ? 'key' : 'delta';

            const chunk = new EncodedVideoChunk({
                type: frameType,
                timestamp: performance.now(),
                data: videoData
            });

            decoder.decode(chunk);
        } else if (messageType === 0x02) {
            const audioData = buffer.slice(1);
            if (window.audioNode && window.audioNode.port) {
                window.audioNode.port.postMessage(audioData);
            }
        } else {
            console.error('Unknown message type:', messageType);
        }
    };
});

// Add toggle fullscreen functionality to the floating button
fullscreenToggle.addEventListener('click', async () => {
    if (document.fullscreenElement) {
        try {
            await document.exitFullscreen();
            console.log('Fullscreen exited');
        } catch (err) {
            console.error('Error exiting fullscreen:', err);
        }
    } else if (document.fullscreenEnabled) {
        try {
            await document.body.requestFullscreen();
            console.log('Fullscreen activated');
        } catch (err) {
            console.error('Error entering fullscreen:', err);
        }
    }
});

let isDragging = false;
let offsetX, offsetY;

// Start dragging with touchstart
fullscreenToggle.addEventListener('touchstart', (event) => {
    isDragging = true;

    // Get the first touch point
    const touch = event.touches[0];

    // Calculate the offset between the touch point and the button's position
    const rect = fullscreenToggle.getBoundingClientRect();
    offsetX = touch.clientX - rect.left;
    offsetY = touch.clientY - rect.top;

    fullscreenToggle.style.cursor = 'grabbing';
});

// Drag with touchmove
fullscreenToggle.addEventListener('touchmove', (event) => {
    if (isDragging) {
        const touch = event.touches[0];

        // Update the button's position
        fullscreenToggle.style.left = `${touch.clientX - offsetX}px`;
        fullscreenToggle.style.top = `${touch.clientY - offsetY}px`;
    }
});

// Stop dragging with touchend
fullscreenToggle.addEventListener('touchend', () => {
    isDragging = false;
    fullscreenToggle.style.cursor = 'grab';
});

// Initial canvas resize
resizeCanvas();
