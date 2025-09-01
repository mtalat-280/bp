# 1NCE Zephyr blueprint - CoAP Demo

## Overview

1NCE Zephyr CoAP Demo allows customers to establish a secure communication with 1NCE endpoints via CoAPs after receiving DTLS credentials from Device Authenticator using the SDK. It can also send compressed payload using the Energy Saver feature. 

On the `Thingy:91` device, LED indicators show the following statuses:

- 🔴 **RED** – Connecting to the network  
- 🔵 **BLUE** – Network connection established  
- 🟢 **GREEN** – Message sent to 1NCE OS

## Secure Communication with DTLS using 1NCE SDK

By default, the demo uses 1NCE SDK to send a CoAP GET request to 1NCE OS Device Authenticator. The response is then processed by the SDK and the credentials are used to connect to 1NCE endpoint via CoAP with DTLS. 

> ⚠️ **Note:** If the Pre-shared Key for DTLS is set manually, **STRING** format should be used.  

## Unsecure CoAP Communication 

To test unsecure communication (plain CoAP), disable the device authenticator by adding the following flag to `prj.conf`

```
CONFIG_NCE_DEVICE_AUTHENTICATOR=n
``` 

 ## ⚡ Using 1NCE Energy saver
 The demo can send compressed, optimized payloads using 1NCE Energy Saver. This reduces payload size and improves energy efficiency.
 Enable in `prj.conf`:

```
CONFIG_NCE_ENERGY_SAVER=y
```

When enabled, the device will send compressed messages based on a translation template defined in 1NCE OS portal.

> 💡 **Note:**  
> Add the template located in `./nce_coap_demo/template/template.json` to the 1NCE OS portal, and enable it for the **COAP protocol** to ensure correct decoding of the compressed payload.

If disabled, a plain-text message will be sent instead.

## ⚙️ Configuration options

The following configuration options are available for customizing the CoAP client behavior:

| Config Option                                | Description                                                                 | Default                 |
|---------------------------------------------|-----------------------------------------------------------------------------|-------------------------|
| `CONFIG_COAP_SAMPLE_SERVER_HOSTNAME`        | CoAP server hostname                                                        | `coap.os.1nce.com`      |
| `CONFIG_COAP_SAMPLE_SERVER_PORT`            | CoAP server port (5684 if DTLS enabled, otherwise 5683)                     | Auto                    |
| `CONFIG_COAP_URI_QUERY`                     | URI query string used as topic parameter                                    | `t=test`                |
| `CONFIG_COAP_SAMPLE_REQUEST_INTERVAL_SECONDS` | Interval between uplink messages (in seconds)                              | `60`                   |
| `CONFIG_NCE_DEVICE_AUTHENTICATOR`           | Enables device onboarding with 1NCE SDK                                     | `y`                     |
| `CONFIG_NCE_UPLINK_MAX_RETRIES`             | Max retry attempts for uplink CoAP requests                                 | `5`                     |
| `CONFIG_NCE_DTLS_HANDSHAKE_TIMEOUT_SECONDS` | DTLS handshake timeout                                                      | `15`                    |
| `CONFIG_NCE_MAX_DTLS_CONNECTION_ATTEMPTS`   | Max DTLS failures before retrying onboarding                                | `3`                     |
| `CONFIG_NCE_DTLS_SECURITY_TAG`              | DTLS TAG used to store credentials on the modem                             | `1111`  |
| `CONFIG_NCE_ENABLE_DTLS`              | Enables DTLS for secure CoAP communication. This is **automatically enabled** when both `ZEPHYR_NCE_SDK_MODULE` and `NCE_DEVICE_AUTHENTICATOR` are enabled.                        | `y` if `ZEPHYR_NCE_SDK_MODULE && NCE_DEVICE_AUTHENTICATOR`, else `n` |

---


### 🔋 Payload Configuration

Depending on whether the Energy Saver feature is enabled:

- If `CONFIG_NCE_ENERGY_SAVER` is **disabled**:

| Config Option        | Description                                      | Default |
|----------------------|--------------------------------------------------|---------|
| `CONFIG_PAYLOAD`     | Message sent to 1NCE IoT Integrator              | `{"text": "Hi, this is a test message!"}` |

---

- If `CONFIG_NCE_ENERGY_SAVER` is **enabled**:

| Config Option             | Description                                    | Default |
|---------------------------|------------------------------------------------|---------|
| `CONFIG_NCE_PAYLOAD_DATA_SIZE` | Payload data size for the Energy Saver template | `10`     |

---

> ⚠️ **Note:** The default maximum length for `CONFIG_COAP_URI_QUERY` is **12 bytes**.  
> To increase this limit, set:
>
> ```conf
> CONFIG_COAP_EXTENDED_OPTIONS_LEN=y
> CONFIG_COAP_EXTENDED_OPTIONS_LEN_VALUE=<your_desired_length>
> ```

## 🧠 Device Controller

The **Device Controller** allows your device to receive CoAP downlink messages using the 1NCE Management API. It supports sending downlink requests that your device can process in real-time.

📘 More info: [1NCE DevHub – Device Controller](https://help.1nce.com/dev-hub/docs/1nce-os-device-controller)

### 🔁 Sending a Request

Use the following `curl` command to send a CoAP request to your device:

```
curl -X 'POST' 'https://api.1nce.com/management-api/v1/integrate/devices/<ICCID>/actions/COAP' \
-H 'accept: application/json' \
-H 'Authorization: Bearer <your Access Token >' \
-H 'Content-Type: application/json' \
-d '{
  "payload": "Data to send to the device",
  "payloadType": "STRING",
  "port": <NCE_RECV_PORT>,
  "path": "/example?param1=query_example1",
  "requestType": "POST",
  "requestMode": "SEND_NOW"
}'
```

Replace:

- `<ICCID>` with your SIM’s ICCID  
- `<your Access Token>` with your [OAuth token](https://help.1nce.com/dev-hub/reference/postaccesstokenpost)

---

#### 📩 Request Parameters

| Parameter     | Description                                                            | Example                        |
|---------------|------------------------------------------------------------------------|--------------------------------|
| `payload`     | Data to send to the device                                             | `"enable_sensor"`             |
| `payloadType` | Type of payload (`STRING`, `HEX`, etc.)                                | `"STRING"`                    |
| `port`        | Device port to receive the message                                      | `3000`                         |
| `path`        | Request path and optional query                                         | `"/example?param1=query"`     |
| `requestType` | CoAP method to use (`POST`, `GET`, etc.)                                | `"POST"`                      |
| `requestMode` | Request mode (`SEND_NOW`, `SEND_WHEN_ACTIVE`)                           | `"SEND_NOW"`                  |

## 🔧 Zephyr Device Controller Configuration

If `CONFIG_NCE_ENABLE_DEVICE_CONTROLLER` is enabled:

| Config Option                          | Description                                                               | Default  |
|---------------------------------------|---------------------------------------------------------------------------|----------|
| `CONFIG_NCE_ENABLE_DEVICE_CONTROLLER` | Enables the device controller feature                                     | `y`      |
| `CONFIG_NCE_RECV_PORT`                | UDP port to listen for incoming CoAP messages                             | `3000`   |
| `CONFIG_NCE_RECEIVE_BUFFER_SIZE`      | Buffer size for CoAP message handling                                     | `1024`   |
| `CONFIG_NCE_DOWNLINK_MAX_RETRIES`     | Max retry attempts for setting up downlink socket                         | `5`      |
| `CONFIG_NCE_COAP_MAX_URI_PATH_SEGMENTS`   | Maximum number of URI path segments to support in CoAP requests       | `5`      |
| `CONFIG_NCE_COAP_MAX_URI_QUERY_PARAMS`    | Maximum number of query parameters allowed in CoAP requests           | `5`      |

---

## ⚠️ CoAP Limitations

> CoAP messages — including **uplink and downlink** — are subject to strict option length limitations (especially for `URI-QUERY` and extended paths).  
> Make sure to increase buffer sizes if your topic or query strings exceed the default 12 bytes using:
>
> ```conf
> CONFIG_COAP_EXTENDED_OPTIONS_LEN=y
> CONFIG_COAP_EXTENDED_OPTIONS_LEN_VALUE=<length>
> ```

## 📤 Zephyr Output Example

When the Zephyr application receives a CoAP message from the 1NCE API:

```
[00:00:02.275,817] <inf> [downlink_thread] NCE_COAP_DEMO: Downlink thread started...
[00:00:02.276,336] <inf> [downlink_thread] NCE_COAP_DEMO: Listening on port: 3000

[00:00:07.847,869] <inf> [downlink_thread] NCE_COAP_DEMO: Received 72 bytes from server
[00:00:07.847,930] <inf> [downlink_thread] NCE_COAP_DEMO: Received raw data:
                                                          48 02 1e 02 98 73 d5 1f  d7 3a 5a 1c b7 65 78 61 |H....s.. .:Z..exa
                                                          6d 70 6c 65 10 3d 08 70  61 72 61 6d 31 3d 71 75 |mple.=.p aram1=qu
                                                          65 72 79 5f 65 78 61 6d  70 6c 65 31 ff 44 61 74 |ery_exam ple1.Dat
                                                          61 20 74 6f 20 73 65 6e  64 20 74 6f 20 74 68 65 |a to sen d to the
                                                          20 64 65 76 69 63 65 0a                          | device.         
[00:00:07.847,961] <inf> [downlink_thread] NCE_COAP_DEMO: CoAP Header:
[00:00:07.847,991] <inf> [downlink_thread] NCE_COAP_DEMO: Version: 1
[00:00:07.848,022] <inf> [downlink_thread] NCE_COAP_DEMO: Type: CON
[00:00:07.848,022] <inf> [downlink_thread] NCE_COAP_DEMO: CoAP Request Method: POST (0.02)
[00:00:07.848,052] <inf> [downlink_thread] NCE_COAP_DEMO: Message ID: 7682
[00:00:07.848,052] <inf> [downlink_thread] NCE_COAP_DEMO: CoAP Options:
[00:00:07.848,083] <inf> [downlink_thread] NCE_COAP_DEMO: Complete Path: 
[00:00:07.848,114] <inf> [downlink_thread] NCE_COAP_DEMO: /example

[00:00:07.848,175] <inf> [downlink_thread] NCE_COAP_DEMO: CoAP Payload (binary):
                                                          44 61 74 61 20 74 6f 20  73 65 6e 64 20 74 6f 20 |Data to  send to 
                                                          74 68 65 20 64 65 76 69  63 65 0a                |the devi ce.     
[00:00:07.848,236] <inf> [downlink_thread] NCE_COAP_DEMO: sent ack:
                                                          68 44 1e 02 98 73 d5 1f  d7 3a 5a 1c             |hD...s.. .:Z.    
[00:00:07.848,632] <inf> [downlink_thread] NCE_COAP_DEMO: CoAP ACK sent successfully
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
