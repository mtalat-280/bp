# 1NCE Zephyr blueprint - 1NCE Memfault Demo

## Overview

The **1NCE Memfault Demo** enables Zephyr-based devices to send diagnostics and fault data via **CoAP** using the **1NCE CoAP Proxy**. This is useful for tracking faults, crashes, and network issues in IoT devices. Communication can optionally be secured using **DTLS**.

On the `Thingy:91` device, LED indicators show the following statuses:

- 🔵 **BLUE** – Network connected  
- 🟢 **GREEN** – Memfault data sent successfully  
- 🔴 **RED** – Failed to send Memfault data

---

## 🔌 Memfault Integration

To use this demo, install and enable the [Memfault Plugin](https://help.1nce.com/dev-hub/docs/1nce-os-plugins-device-observability-memfault) for 1NCE OS.

📦 SDK Requirement: [nRF Connect SDK v2.8.0](https://docs.nordicsemi.com/bundle/ncs-2.8.0/page/nrf/gsg_guides.html)

---

## ▶️ Running the Demo

### 1️⃣ Build & Flash

**Build** the project for either `thingy91/nrf9160/ns` or `nrf9160dk/nrf9160/ns` or `nrf9151dk/nrf9151/ns`.

- Flash using **VS Code** for DKs or **nRF Connect Programmer** for Thingy:91.
- Firmware path (Thingy:91):  
  `build/nce_debug_memfault_demo/zephyr/zephyr.signed.hex`

> ⚠️ On Windows, avoid long folder paths to prevent build errors.  
> Use something like `C:\dev\memfault_demo`

---

### 2️⃣ Upload Symbol File to Memfault

To enable metric processing:

- Go to **Memfault Dashboard > Symbol Files**
- Upload:  
  `build/nce_debug_memfault_demo/zephyr/zephyr.elf`

📘 [Symbol File Guide](https://docs.memfault.com/docs/mcu/symbol-file-build-ids)

<p align="center"><img src="./images/symbol_file_1.png"><br></p>
<p align="center"><img src="./images/symbol_file_2.png"><br></p>

---

### 3️⃣ Authorize the Device

The device will register itself using the SIM’s **ICCID** as its serial number.  
You’ll see it appear in the **Devices** view of Memfault.

<p align="center"><img src="./images/device.png"><br></p>

--- 

### 4️⃣ Generate Events

On boot, two events are sent automatically:

- `heartbeat`: Contains standard metrics  
- `reboot`: Reports cause of last reboot

Use the CLI to trigger more:

```bash
nce post_chunks     # Push buffered data now
nce divby0          # Trigger division-by-zero crash
nce sw1             # Increment switch_1_toggle_count
nce sw2             # Log switch_2_toggled event
nce disconnect      # Simulate disconnection and reconnection
```

The overview dashboard shows a summary of recent device issues:
<p align="center"><img src="./images/overview.png"><br></p>

---

## 🔘 Fault Injection via Buttons

| Button / Switch | Description                         |
|-----------------|-------------------------------------|
| Button 1        | Stack overflow                      |
| Button 2        | Division by zero                    |
| Switch 1        | Custom metric: toggle count         |
| Switch 2        | Event trace: switch toggled         |

💡 On Thingy:91, use `nce` CLI instead (only Button 1 available)

---

## 📶 Connectivity Metrics

Enabled by default with:

```conf
CONFIG_MEMFAULT_NCS_LTE_METRICS=y
CONFIG_NCE_MEMFAULT_DEMO_COAP_SYNC_METRICS=y
CONFIG_NCE_MEMFAULT_DEMO_CONNECTIVITY_METRICS=y
```

### Standard LTE Metrics

- `ncs_lte_time_to_connect_ms`  
- `ncs_lte_connection_loss_count`  
- `ncs_lte_tx_kilobytes`  
- `ncs_lte_rx_kilobytes`

### Additional Metrics

- `ncs_lte_nce_operator`  
- `ncs_lte_nce_bands`  
- `ncs_lte_nce_current_band`  
- `ncs_lte_nce_apn`  
- `ncs_lte_nce_rsrp_dbm`

### Sample Connectivity dashboard configuration:
<p align="center"><img src="./images/connectivity_metrics.png"><br></p>

#### Sync Succes chart configuration:
<p align="center"><img src="./images/sync_success_config.png"><br></p>

#### To create a new metrics chart:
<p align="center"><img src="./images/create_chart.png"><br></p>

#### Signal quality chart configuration:
<p align="center"><img src="./images/signal_quality_config.png"><br></p>

#### Sent KB chart configuration:
<p align="center"><img src="./images/sent_kb_config.png"><br></p>

---

## 🔐 DTLS Configuration

To enable secure communication:

```conf
CONFIG_NCE_MEMFAULT_DEMO_ENABLE_DTLS=y
CONFIG_NCE_SDK_ENABLE_DTLS=y
CONFIG_NCE_DEVICE_AUTHENTICATOR=y
CONFIG_NCE_SDK_DTLS_SECURITY_TAG=<your_tag>
```

- If onboarding is required, set `<your_tag>` to an empty tag and the demo will authenticate via 1NCE automatically.
- On failure (3x), re-onboarding is triggered automatically.

---


## ⚙️ Configuration Options

### General Options

| Config Option                                         | Description                                             | Default        |
|------------------------------------------------------|---------------------------------------------------------|----------------|
| `CONFIG_NCE_MEMFAULT_DEMO_PERIODIC_UPDATE`           | Enable periodic Memfault updates                        | `y`            |
| `CONFIG_NCE_MEMFAULT_DEMO_PERIODIC_UPDATE_FREQUENCY_SECONDS` | Interval between updates (seconds)               | `30`           |
| `MEMFAULT_METRICS_HEARTBEAT_INTERVAL_SECS`           | Heartbeat interval (in header file)  in `config/memfault_platform_config.h`                   | `30`           |
| `CONFIG_NCE_MEMFAULT_DEMO_CONNECTIVITY_METRICS`      | Collect Additional connectivity metrics                            | `y`            |
| `CONFIG_NCE_MEMFAULT_DEMO_COAP_SYNC_METRICS`         | Tracks successful/failed syncs                          | `y`            |
| `CONFIG_NCE_MEMFAULT_DEMO_PRINT_HEARTBEAT_METRICS`   | Print heartbeat metrics to serial log                   | `y`            |
| `CONFIG_NCE_MEMFAULT_DEMO_DISCONNECT_DURATION_SECONDS` | Simulated disconnect duration                        | `20`           |
| `CONFIG_NCE_MEMFAULT_DEMO_ENABLE_DTLS`               | Enable secure CoAP over DTLS                            | `n`            |

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