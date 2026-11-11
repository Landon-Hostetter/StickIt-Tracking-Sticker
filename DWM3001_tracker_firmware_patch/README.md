# DWM3001 Tracker firmware profile (QANI-backed prototype)

This patch makes two conservative product-identity changes to the already-built and tested DWM3001CDK QANI firmware:

1. The BLE GAP name becomes `DWM3001-TRACKER`.
2. The fixed factory-device-address suffix is no longer embedded in the advertised name.

It deliberately leaves the proven BLE/UWB protocol implementation untouched.

## Why this is the safe first firmware version

The supplied QANI source already contains the pieces needed for an iPhone accessory app:

- `BLE/ble.c` initializes the SoftDevice, QNIS GATT service, ANIS GATT service, and advertising.
- The QNIS two-way app data link is advertised and has a write RX characteristic and notify TX characteristic.
- `BLE/niq/ble_niq.c` handles the NI accessory protocol flow: request accessory configuration data, accept the Apple shareable configuration data, start UWB, and stop UWB.
- `BLE/anis/ble_anis.c` publishes the Apple Nearby Interaction accessory configuration characteristic.
- `Src/fira/QANI/fira_niq.c` configures FiRa ranging and requests AoA azimuth measurements when the board reports AoA support.

The QNIS service is the app-specific two-way data link. ANIS is the Apple Nearby Interaction Service used by the Apple device's Nearby Interaction subsystems; Apple says apps should not discover that service directly. A native iOS app uses CoreBluetooth for the app data link and NearbyInteraction (`NINearbyAccessoryConfiguration` / `NISession`) for distance and direction.

## BLE interface found in the supplied source

QNIS service UUID:

`2E938FD0-6A61-11ED-A1EB-0242AC120002`

QNIS characteristics (same 128-bit base UUID):

- RX / phone-to-accessory write: short UUID `0x998A`
- TX / accessory-to-phone notification: short UUID `0x9AF2`
- Secure/read characteristic: short UUID `0x941C` (source initializes its read value to `Qorvo`; leave it unchanged for compatibility with the tested reference app)

The source's message-handler path is:

- `MessageId_init`: `ble_niq.c` regenerates accessory configuration data and sends it to the phone.
- `MessageId_configure_and_start`: passes the phone's shareable UWB configuration to `niq_configure_and_start_uwb(...)` and acknowledges startup.
- `MessageId_stop`: calls `niq_stop_uwb()` and acknowledges stop.

The exact numeric message IDs and packet structure are defined by `niq.h` in the Qorvo NIQ include folder. That header was not present in the uploaded ZIP, so this note intentionally documents the symbolic protocol rather than guessing numeric opcodes.

Apple Nearby Interaction Service (ANIS) UUIDs implemented in `BLE/anis/ble_anis.c`:

- Service: `48fe3e40-0817-4bb2-8633-3073689c2dba`
- Accessory Single Configuration Data characteristic: `95e8d9d5-d8ef-4721-9a4e-807375f53328`

These match the Apple Nearby Interaction Accessory Protocol Specification R4 service and characteristic UUIDs.

## What this patch does not do

- It does not port the QANI FreeRTOS/UWB middleware into the separate Zephyr project at `C:\ncs\dwm_3001_tracker`.
- It does not create the iPhone app. The app must implement the QNIS transport and Apple's Nearby Interaction API.
- It does not add the crowdsourced location/server protocol, Find My certification, or rotating privacy identifiers.
- It does not alter UWB ranging role, FiRa settings, UWB middleware, or the already-validated QANI ranging behavior.

## Apply and build on the original Windows QANI tree

1. Extract this ZIP anywhere.
2. Open PowerShell in the extracted folder.
3. Run:

   ```powershell
   powershell -ExecutionPolicy Bypass -File .\Apply-DWM3001TrackerPatch.ps1
   ```

   The script backs up both original files before replacing them.
4. Open the existing `DWM3001CDK-QANI-FreeRTOS.emProject` in SEGGER Embedded Studio and build (F7).
5. The post-build step should create `DWM3001CDK-QANI-FreeRTOS_full.hex`. Flash that merged image using the same successful `nrfutil device program` command already used for QANI.
6. In nRF Connect, the device should now appear as `DWM3001-TRACKER`.

The original full QANI tree must remain installed on the PC: the uploaded ZIP did not include `Projects\Libs` (including NIQ headers/static libraries) or `Projects\SDK_BSP` (Nordic SDK/SoftDevice sources and headers). This patch is designed to be applied to the complete local tree, not to the incomplete ZIP extraction.
