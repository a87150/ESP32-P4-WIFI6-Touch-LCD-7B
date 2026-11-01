# Bt service test app

This test app exercises the real `Bt` service lifecycle through `ServiceManager`. The PC build uses
the registered Linux Bluetooth provider; it does not construct a test backend or inject Bluetooth
events. ESP-IDF builds verify the explicit unavailable path on targets without Classic Bluetooth.

The smoke test configures, starts, stops, and cleans up the service only. It does not connect a peer,
inject PCM, or test AVRCP controls.
