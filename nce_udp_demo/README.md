# 1NCE Zephyr blueprint - UDP Demo

## Overview

1NCE Zephyr UDP Demo allows customers to communicate with 1NCE endpoints via UDP Protocol, and it can send compressed payload using the Energy Saver feature. 

On the `Thingy:91` device, LED indicators show the following statuses:

- 🔴 **RED** – Connecting to the network  
- 🔵 **BLUE** – Network connection established  
- 🟢 **GREEN** – Message sent to 1NCE OS


## ⚡ Using 1NCE Energy Saver
 The demo can send optimized payload using 1NCE Energy saver. To enable this feature, add the following flag to `prj.conf`

```
CONFIG_NCE_ENERGY_SAVER=y
```

When enabled, the device will send compressed messages based on a translation template defined in 1NCE OS portal.

> 💡 **Note:**  
> Add the template located in `./nce_udp_demo/template/template.json` to the 1NCE OS portal, and enable it for the **UDP protocol** to ensure correct decoding of the compressed payload.

## ⚙️ Configuration Options

The available configuration parameters for the UDP demo:

| Config Option                             | Description                                                                 | Default                 |
|------------------------------------------|-----------------------------------------------------------------------------|-------------------------|
| `CONFIG_UDP_SERVER_HOSTNAME`             | UDP server hostname                                                         | `udp.os.1nce.com`       |
| `CONFIG_UDP_SERVER_PORT`                 | UDP server port number                                                      | `4445`                  |
| `CONFIG_UDP_DATA_UPLOAD_FREQUENCY_SECONDS` | Interval between UDP transmissions (in seconds)                           | `20`                    |
| `CONFIG_UDP_PSM_ENABLE`                  | Enable LTE Power Saving Mode (PSM)                                          | `n`                     |
| `CONFIG_UDP_EDRX_ENABLE`                 | Enable LTE enhanced Discontinuous Reception (eDRX)                          | `n`                     |
| `CONFIG_UDP_RAI_ENABLE`                  | Enable LTE Release Assistance Indication (RAI)                              | `n`                     |

---

### 🔋 Payload Configuration

Depending on whether the Energy Saver feature is enabled:

- If `CONFIG_NCE_ENERGY_SAVER` is **disabled**:

| Config Option        | Description                                      | Default |
|----------------------|--------------------------------------------------|---------|
| `CONFIG_PAYLOAD`     | Message sent to 1NCE IoT Integrator              | `{"text": "Hi, this is a test message!"}` |

- If `CONFIG_NCE_ENERGY_SAVER` is **enabled**:

| Config Option             | Description                                    | Default |
|---------------------------|------------------------------------------------|---------|
| `CONFIG_NCE_PAYLOAD_DATA_SIZE` | Payload data size for the Energy Saver template | `10`     |


## 🧠 Device Controller

The **Device Controller** allows your device to receive CoAP downlink messages using the 1NCE Management API. It supports sending downlink requests that your device can process in real-time.

📘 More info: [1NCE DevHub – Device Controller](https://help.1nce.com/dev-hub/docs/1nce-os-device-controller)

### 🔁 Sending a Request

You can trigger a downlink using the following `curl` command:


```
curl -X 'POST' 'https://api.1nce.com/management-api/v1/integrate/devices/<ICCID>/actions/UDP' \
-H 'accept: application/json' \
-H 'Authorization: Bearer <your Access Token >' \
-H 'Content-Type: application/json' \
-d '{
  "payload": "enable_sensor",
  "payloadType": "STRING",
  "port": 3000,
  "requestMode": "SEND_NOW"
}'
```

Replace:

- `<ICCID>` with your SIM's ICCID
- `<your Access Token>` with your [OAuth token](https://help.1nce.com/dev-hub/reference/postaccesstokenpost)

---

### 📩 Request Parameters

| Parameter     | Description                                                             | Example           |
|---------------|-------------------------------------------------------------------------|-------------------|
| `payload`     | Data to send to the device                                              | `"enable_sensor"` |
| `payloadType` | Type of payload (`STRING`, `HEX`, etc.)                                 | `"STRING"`        |
| `port`        | UDP port to receive the message (`CONFIG_NCE_RECV_PORT`)                | `3000`            |
| `requestMode` | Request mode (`SEND_NOW` or `SEND_WHEN_ACTIVE`)                         | `"SEND_NOW"`      |

---

## 🔧 Zephyr Device Controller Configuration

To enable and handle downlink messages on your device, use the following configs:

| Config Option                          | Description                                                               | Default  |
|---------------------------------------|---------------------------------------------------------------------------|----------|
| `CONFIG_NCE_ENABLE_DEVICE_CONTROLLER` | Enables the device controller feature                                     | `y`      |
| `CONFIG_NCE_RECV_PORT`                | UDP port to listen for incoming messages                                  | `3000`   |
| `CONFIG_NCE_RECEIVE_BUFFER_SIZE`      | Buffer size for incoming UDP payloads                                     | `1024`   |

---

## 📤 Zephyr Output Example

When the Zephyr application receives a UDP downlink from the 1NCE API:

```
[00:00:02.996,978] <inf> [downlink_thread] NCE_UDP_DEMO: Downlink thread started...
[00:00:02.997,802] <inf> [downlink_thread] NCE_UDP_DEMO: Listening on port: 3000
[00:00:11.325,683] <inf> [downlink_thread] NCE_UDP_DEMO: Received message: enable_sensor
```

## 📦 Ready-to-Flash Firmware for Thingy:91

We provide a **prebuilt HEX file** for Thingy:91 that you can flash directly to your device for quick testing.  
No build setup is required — just flash and go.

👉 **Download:** [Thingy:91 Prebuilt HEX](thingy_binaries/zephyr.signed.hex)

> ⏳ **Note:** The firmware is configured with all LTE bands enabled, which may cause a delay of several minutes during the initial network connection while scanning for available bands. This is normal.
 
## 🆘 Need Help?

Open an issue on GitHub for:

- ❗ Bug reports  
- 🚀 Feature requests  
- 📝 Documentation issues  
- ❓ General questions  

👉 [Create a new issue](https://github.com/1NCE-GmbH/blueprint-zephyr/issues/new/choose)

---

Made with 💙 by the 1NCE Team.
