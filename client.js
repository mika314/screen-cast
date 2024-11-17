const canvas = document.getElementById('videoCanvas');
const startButton = document.getElementById('startButton');
const ctx = canvas.getContext('2d');


// Start fullscreen and WebSocket connection on button click
startButton.addEventListener('click', async () => {
    // Request fullscreen
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

    // Hide the button after entering fullscreen
    startButton.style.display = 'none';

    console.log("connecting to ws:/localhost:8090/");
    const ws = new WebSocket('ws://localhost:8090/');
    ws.binaryType = 'arraybuffer';

    let decoder = null;

    ws.onopen = async function() {
        console.log('WebSocket connection opened');
    };

    ws.onmessage = async function(event) {
        const data = event.data;
        const buffer = new Uint8Array(data);

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
                        // Draw decoded frame on the canvas
                        ctx.drawImage(frame, 0, 0, canvas.width, canvas.height);
                        frame.close();
                    },
                    error: err => {
                        console.error('Decoder error:', err);
                    }
                });

            } catch (err) {
                console.error('Error decoder creation:', err);
                return;
            }
            try {
                decoder.configure(config);
            } catch (err) {
                console.error('Error setting the configuration:', err);
                return;
            }
        }

        // Parse NAL units to determine frame type
        const type = buffer[4] & 0x1F;
        const frameType = (type === 5) ? 'key' : 'delta';

        const chunk = new EncodedVideoChunk({
            type: frameType,
            timestamp: performance.now(),
            data: buffer
        });

        decoder.decode(chunk);
    };
});
