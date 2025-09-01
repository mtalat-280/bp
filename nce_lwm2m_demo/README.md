# 1NCE Zephyr blueprint - LwM2M Demo

## Overview

The **1NCE LwM2M Demo** enables devices to communicate with 1NCE endpoints using the **LwM2M protocol** over CoAP, with optional DTLS for secure messaging. It supports control of LEDs, buzzers, sensors, and other objects via LwM2M standard object models.

On the `Thingy:91` device, LED indicators show the following statuses:

- 🔴 **RED** – the device is currently connecting to the network
- 🔵 **BLUE** – the device is currently bootstrapping
- 🟢 **GREEN (10 seconds)** – the device is registered with 1NCE LwM2M server


### ✅ Supported Objects for LwM2M Actions

🔗 LwM2M actions can be tested using the [1NCE Action API](https://help.1nce.com/dev-hub/reference/post_v1-devices-deviceid-actions) or from the device controller tab in 1NCE OS UI.

| Object                      | Path(s)                                      | Description                              | Supported Boards            |
|-----------------------------|----------------------------------------------|------------------------------------------|-----------------------------|
| Light Control               | `/3311/0/5850` (1 Thingy:91 LED) <br> `/3311/<0–3>/5850` (4 DK LEDS) | Boolean: LED on/off                      | All boards                  |
| Light Color                 | `/3311/0/5706`                                | RGB LED color in HEX (e.g., `0xFF0000`)  | Thingy:91 only              |
| Buzzer                      | `/3338/0/5850`                                | Boolean: Audible alert                   | Thingy:91 only              |

---

## ⚙️ Configuration Options

### 🔐 Authentication & Server Setup

| Config Option                                  | Description                                                              | Default              |
|------------------------------------------------|--------------------------------------------------------------------------|----------------------|
| `CONFIG_NCE_ICCID`                             | ICCID used as endpoint name and device identity                          | `""`                 |
| `CONFIG_NCE_LWM2M_BOOTSTRAP_PSK`               | Pre-shared key in HEX for bootstrap/auth                                 | `""`                 |
| `CONFIG_LWM2M_CLIENT_UTILS_SERVER`             | LwM2M server URI (e.g., `coaps://lwm2m.os.1nce.com:5684`)                 | -                    |
| `CONFIG_LWM2M_CLIENT_UTILS_BOOTSTRAP_TLS_TAG`  | Security tag for bootstrap server (credentials storage)                  | `1111`               |
| `CONFIG_LWM2M_CLIENT_UTILS_SERVER_TLS_TAG`     | Security tag for main server (replaced after bootstrap)                  | `1112`               |
| `CONFIG_LWM2M_ENGINE_DEFAULT_LIFETIME`         | Default LwM2M Server lifetime (in seconds)                               | `180`                |

📌 The PSK (Pre-Shared Key) must match the credentials registered using the [1NCE PSK API](https://help.1nce.com/dev-hub/reference/post_v1-integrate-devices-deviceid-presharedkey)

> ⚠️ The PSK **must be provided in HEX format**, not plain text.

💡 **Example:**  
If your desired PSK is the string `KeyPass123`, you must convert it to its hexadecimal representation.

**Conversion:**  
- Input string: `KeyPass123`  
- HEX format: `4b657950617373313233`

Use this HEX value (`4b657950617373313233`) when setting `CONFIG_NCE_LWM2M_BOOTSTRAP_PSK`.

✅ Tools for conversion:
- Online: [RapidTables String to Hex](https://www.rapidtables.com/convert/number/ascii-to-hex.html)
- Terminal (Linux/macOS):
```bash
echo -n 'KeyPass123' | xxd -p
```

---

## 🔓 Unsecured LwM2M (Testing Only)

To run without DTLS (e.g., during integration testing):

```conf
CONFIG_LWM2M_DTLS_SUPPORT=n
CONFIG_LWM2M_CLIENT_UTILS_SERVER="coap://lwm2m.os.1nce.com:5683"
```

---

## 🧩 Feature Modules

### Input Controls

| Module                            | Description                          | Condition                                                                 |
|-----------------------------------|--------------------------------------|---------------------------------------------------------------------------|
| `CONFIG_APP_PUSH_BUTTON`   | Enable push button support (Object 3347)           | All boards:<br>• Thingy:91 (1 button)<br>• nRF9160 DK & nRF9151 DK (2 buttons) |
| `CONFIG_APP_ONOFF_SWITCH`  | Enable on/off switch input support (Object 3342)   | DK boards only:<br>• nRF9160 DK (2 switches)<br>• nRF9151 DK (Buttons 3 and 4 are used as switches) |

#### Main LwM2M Resources for Push Button and On/Off Switch Objects

| Resource ID | Name                     | Type     | Description                                               |
|-------------|--------------------------|----------|-----------------------------------------------------------|
| `5500`      | Digital Input State      | Boolean  | `true` if pressed/on, `false` if released/off             |
| `5501`      | Digital Input Counter    | Integer  | Number of times the button/switch has toggled            |
| `5518`      | Timestamp of Last Change | Time     | Time of the last change (press/release or on/off toggle) |

💡 **Note:**  
* Resource values can be monitored by sending an  `observe-start` request to the relevant object (e.g., `/3347`) using 1NCE OS device controller.


### Output Controls

| Module                  | Description                          | Condition                                      |
|-------------------------|--------------------------------------|------------------------------------------------|
| `CONFIG_APP_LIGHT_CONTROL` | Enable LED output (Object 3311)                 | All boards                                     |
| `CONFIG_APP_BUZZER`     | Enable buzzer output   (Object 3338)               | Thingy:91 only                                 |


---

## 🧾 Device Identity

Set device manufacturer and type:

```conf
CONFIG_APP_MANUFACTURER="Nordic Semiconductor ASA"
CONFIG_APP_DEVICE_TYPE="OMA-LWM2M Client"
```
💡 **Notes:**  
* Those values are stored in the `/3/0/0` and `/3/0/17` resources of the device object. 
* The device object is not included in passive reporting, but it can be retrieved by sending a `Read` request to object `/3` using 1NCE OS device controller.
---

## 🔧 Logging

Configure log levels for the application:

```conf
CONFIG_APP_LOG_LEVEL_INF=y
CONFIG_LOG=y
```

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
