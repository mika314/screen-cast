# Screen Cast

Screen Cast allows you to stream your computer's screen directly to your Oculus Quest via a browser.

## **Screen Cast Demo**

[![Screen Cast Demo](https://img.youtube.com/vi/sL3Lxmemyn0/0.jpg)](https://youtu.be/sL3Lxmemyn0)


## **Getting Started**

### **Building Instructions on Ubuntu 22.04**

1. **Install Dependencies**
   ```bash
   sudo apt update
   sudo apt install -y clang git libavcodec-dev libavformat-dev libavutil-dev libboost1.74-dev libx11-dev libxext-dev libxfixes-dev pkg-config
   ```

2. **Install and Build `coddle` (Build Tool)**
   ```bash
   git clone https://github.com/coddle-cpp/coddle.git
   cd coddle
   ./build.sh
   sudo ./deploy.sh
   cd ..
   ```

3. **Clone and Build Screen Cast**
   ```bash
   git clone https://github.com/mika314/screen-cast.git
   cd screen-cast
   make
   ```

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
1. **Tethered Mode**:
   - Connect your Oculus Quest to your computer using a USB cable.
   - In the terminal, run:
     ```bash
     adb devices
     ```
     Accept any permissions on the Oculus Quest.

2. **Wi-Fi Mode**:
   - Ensure the Oculus Quest and your computer are on the same Wi-Fi network.
   - Tether the Oculus Quest via USB and set up ADB over Wi-Fi:
     ```bash
     adb tcpip 5555
     adb connect <IP_ADDRESS_OF_OCULUS_QUEST>:5555
     ```
   - Disconnect the USB cable and ensure the device remains connected over Wi-Fi:
     ```bash
     adb devices
     ```

---

### **Running Screen Cast**

1. **Start the ADB Port Forwarding**
   ```bash
   adb reverse tcp:8090 tcp:8090
   ```

2. **Start the Screen Cast Server**
   ```bash
   ./screen-cast
   ```

3. **Open the Oculus Quest Browser**
   - Navigate to: `http://localhost:8090`

---

## **Disclaimer**

- This project uses FFmpeg and Xlib for screen capture and encoding. While FFmpeg and Xlib are open-source, they may include components that may not be compatible with the MIT license.
