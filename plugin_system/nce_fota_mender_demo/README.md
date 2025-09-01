# 1NCE Zephyr blueprint - 1NCE FOTA Mender Demo

## Overview

The **1NCE FOTA Mender Demo** enables firmware-over-the-air (FOTA) updates through [Mender.io](https://mender.io) using the 1NCE CoAP Proxy for secure and efficient communication. The device securely connects, authenticates, checks for firmware updates, downloads new versions, and updates itself.

On the `Thingy:91`, the LED colors indicate the following statuses:

- ⚪ **Flashing White** – Connecting to the network  
- 🟢 **Solid Green** – Firmware version 1 running  
- 🟡 **Flashing Green / Flashing Blue** – Firmware is being downloaded  
- 🔵 **Solid Blue** – Firmware version 2 running 

#### 📟 Development Kits (nRF9160DK / nRF9151DK)

While the firmware is being downloaded, the DKs show a circular LED pattern across the four LEDs:

- 🔄 LEDs 1 → 2 → 3 → 4 blink in sequence, repeating until the download is complete. 

---

## Mender Integration

This demo requires the [1NCE Mender Plugin](https://help.1nce.com/dev-hub/docs/1nce-os-plugins-fota-management-mender) to be installed and enabled.

You can use prebuilt binaries and artifacts for quick testing.

---

## Running the demo

### 1️⃣ Build & Flash

Flash the demo to the board using VS Code or nRF Connect for Desktop:
   - For **Thingy:91**, use [nRF Connect Programmer](https://www.nordicsemi.com/Products/Development-tools/nRF-Connect-for-Desktop/Download).

   - For **nrf9151DK & nrf9160DK**, the firmware can be flashed directly from **VS Code**.

> ⚠️ **Windows Path Length Warning**  
> On **Windows**, long file paths may cause build errors during the demo compilation.  
> 👉 To avoid this issue, move the project folder to a shorter path such as:  
>
> ```bash
> C:\dev\fota_mender_demo
> ```

### 2️⃣ Accept the Device in Mender

When starting the demo for the first time, the device will attempt to register with the Mender server.

🛡️ **Manual Approval Required:**  
You must manually **accept the device** in the [Mender Dashboard](https://hosted.mender.io/ui/) before it can receive any updates.

#### 🔁 After acceptance:

- The device will **periodically check** for firmware updates
- Its **inventory** (such as IMEI, artifact name, and device type) will be updated in the Mender dashboard

> 💡 You can view this info under the **Devices** section after the device is authorized.
<p align="center"><img src="./images/v1.PNG"><br></p>


### 3️⃣  Bump Version & Rebuild

To simulate a firmware update:

1. Open your `prj.conf` file
2. Update the following configuration options to reflect the new version:

```conf
CONFIG_APPLICATION_VERSION=2
CONFIG_ARTIFACT_NAME="release-v2"
```
3. Rebuild the firmware using your preferred method (e.g., west build, VS Code)

### 4️⃣ Create & Upload Mender Artifact

📦 Firmware updates in Mender are distributed as **artifacts**.

#### 🛠️ Create Artifact with `mender-artifact`

1. **Install** the [Mender Artifact Tool](https://docs.mender.io/downloads#mender-artifact)

2. **Run** the following command to generate a new artifact:
> ⚙️ Replace the placeholders with your actual values.

```bash
mender-artifact write module-image \
  -t thingy \
  -o release-v2.mender \
  -T release-v2 \
  -n release-v2 \
  -f build/nce_fota_mender_demo/zephyr/zephyr.signed.bin \
  --compression none
```
📌 Replace values as needed for your device:

- `-t`: Device type (`CONFIG_MENDER_DEVICE_TYPE`)
- `-n`: Artifact name (`CONFIG_ARTIFACT_NAME`)
- `-T`: Payload type (e.g. release-v2)
- `-f`: Firmware binary file path (usually `build/nce_fota_mender_demo/zephyr/zephyr.signed.bin`)

3. **Upload** the generated `.mender` file to the **Releases** section in the Mender dashboard.

<p align="center"><img src="./images/artifact.PNG" width="600"></p>


## 5️⃣ Deployment Creation

Once your artifact is uploaded to the Mender **Releases** section, you're ready to deploy it to your device(s).

1. Navigate to the **Deployments** tab in the Mender dashboard.  
2. Click **Create Deployment** and follow the wizard:
   - Select the **target device** or **device group**.
   - Choose the **artifact** you previously uploaded.

<p align="center"><img src="./images/deployment_1.PNG" width="600"></p>

---

#### 🚦 Deployment Status Flow

After creation, the deployment will appear in the list with an initial status of `pending`.  
As your device contacts the Mender server, the status will progress automatically:

```
pending → downloading → rebooting → installing → success ✅ / failure ❌
```

<p align="center"><img src="./images/deployment_2.PNG" width="600"></p>  
<p align="center"><img src="./images/success.PNG" width="600"></p>


## ⚙️ Configuration Options

The following configuration options are available for customizing the Mender FOTA demo:

### 🧩 General Options

| Config Option                                      | Description                                         | Default         |
|---------------------------------------------------|-----------------------------------------------------|-----------------|
| `CONFIG_APPLICATION_VERSION`                      | Application version reported to Mender              | `1`             |
| `CONFIG_ARTIFACT_NAME`                            | Mender artifact name (used in artifact creation)    | `"release-v1"`  |
| `CONFIG_MENDER_DEVICE_TYPE`                       | Device type used for update compatibility           | `"thingy"`      |
| `CONFIG_MENDER_FW_UPDATE_CHECK_FREQUENCY_SECONDS`| Firmware update check interval (in seconds)         | `30`            |
| `CONFIG_MENDER_AUTH_CHECK_FREQUENCY_SECONDS`      | Auth check interval (when unauthorized)             | `30`            |

---

### 🔐 Secure Communication

| Config Option                           | Description                                             | Default                     |
|----------------------------------------|---------------------------------------------------------|-----------------------------|
| `CONFIG_MENDER_URL`                    | Mender backend URL                                      | `"eu.hosted.mender.io"`     |
| `CONFIG_NCE_MENDER_COAP_PROXY_HOST`    | CoAP proxy hostname provided by 1NCE                    | `"coap.proxy.os.1nce.com"`  |
| `CONFIG_COAP_SERVER_PORT`              | CoAP server port (5684 if DTLS is enabled, else 5683)   | `Auto`                      |
| `CONFIG_NCE_MENDER_COAP_URI_PATH`      | URI path for proxying CoAP requests to Mender           | `"mender"`                  |

--- 

### Unsecure CoAP Communication 

By default, the demo uses 1NCE SDK to send a CoAP GET request to 1NCE OS Device Authenticator. The response is then processed by the SDK and the credentials are used to connect to 1NCE endpoint via CoAP with DTLS. 

To test unsecure communication (plain CoAP), disable the device authenticator by adding the following flag to `prj.conf`

```
CONFIG_NCE_DEVICE_AUTHENTICATOR=n
``` 
---

## 📦 Ready-to-Flash Firmware for Thingy:91


For quick testing, we provide **prebuilt firmware binaries** that can be flashed directly to your Thingy:91 device — no build setup required.

Available prebuilt files:

| Version     | Binary (.bin)        | HEX (.hex)              | Mender Artifact (.mender)   |
|-------------|----------------------|--------------------------|-----------------------------|
| `release-v1`| `release-v1.bin`     | `release-v1.hex`         | `release-v1.mender`         |
| `release-v2`| `release-v2.bin`     | `release-v2.hex`         | `release-v2.mender`         |

👉 **Flash directly using:** [`release-v1.hex`](thingy_binaries/release-v1.hex) or [`release-v2.hex`](thingy_binaries/release-v2.hex)

> ⏳ **Note:** These builds enable all LTE bands, so the initial network registration may take several minutes while scanning.

---

## 🆘 Need Help?

Open an issue on GitHub for:

- ❗ Bug reports  
- 🚀 Feature requests  
- 📝 Documentation issues  
- ❓ General questions  

👉 [Create a new issue](https://github.com/1NCE-GmbH/blueprint-zephyr/issues/new/choose)

---

Made with 💙 by the 1NCE Team.
