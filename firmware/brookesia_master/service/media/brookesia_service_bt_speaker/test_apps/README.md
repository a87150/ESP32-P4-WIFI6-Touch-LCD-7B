# Bluetooth speaker service test app

This test app starts the real `BtSpeaker` service and its real `Bt` dependency through `ServiceManager`.
The PC build uses the registered Linux Bluetooth provider; no fake service, fake audio playback service, or
test event injection is registered.

The smoke test configures, starts, stops, and cleans up the A2DP profile without connecting a peer. ESP-IDF
targets without Classic Bluetooth verify the explicit unavailable and cleanup path; Classic-capable hardware
is validated separately with its board configuration.
