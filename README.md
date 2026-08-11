# Screen Cast

Screen Cast allows you to stream your computer's screen directly to your Oculus Quest via a browser.

## **Screen Cast Demo**

[![Screen Cast Demo](https://img.youtube.com/vi/sL3Lxmemyn0/0.jpg)](https://youtu.be/sL3Lxmemyn0)


## **Getting Started**

### **Building Instructions on Ubuntu 22.04**

1. **Install Dependencies**
   ```bash
   sudo apt update
   sudo apt install -y \
   clang \
   git \
   libavcodec-dev \
   libavformat-dev \
   libavutil-dev \
   libboost1.74-dev \
   libgl-dev \
   libglx-dev \
   libopus-dev \
   libpulse-dev \
   libx11-dev \
   libxext-dev \
   libxfixes-dev \
   libxtst-dev \
   pkg-config
   ```

2. **Clone and Build Screen Cast**
   ```bash
   git clone --recurse-submodules https://github.com/mika314/screen-cast.git
   cd screen-cast
   make
   ```

   `make` builds the `coddle` submodule, then builds the project with it.
   If you already cloned without submodules, run `git submodule update --init`.

---

### **Setting Up ADB**

ADB is used to connect your Oculus Quest to your computer.

#### **Install ADB**
If you don't already have ADB installed:
```bash
sudo apt install adb
```

#### **Enable Developer Mode on Oculus Quest**
1. Open the Oculus app on your phone.
2. Navigate to **Devices > Developer Mode**.
3. Enable **Developer Mode**.
4. Reboot your Oculus Quest.

#### **Connect Oculus Quest to Your Computer**
1. Connect your Oculus Quest to your computer using a USB cable.
2. For Wi-Fi mode, ensure the Quest and your computer are on the same Wi-Fi network.
3. In the terminal, run:
   ```bash
   adb devices
   ```
   Accept any permissions on the Oculus Quest.

---

### **Running Screen Cast**

1. **Start Screen Cast**
   ```bash
   ./screen-cast.sh
   ```
   Choose tethered (USB) or Wi-Fi when prompted. The script reverses port `8090` and launches the server. In Wi-Fi mode, disconnect the USB cable when asked.

2. **Open the Oculus Quest Browser**
   - Navigate to: `http://localhost:8090`

---

## **Disclaimer**

- This project uses FFmpeg and Xlib for screen capture and encoding. While FFmpeg and Xlib are open-source, they may include components that may not be compatible with the MIT license.
