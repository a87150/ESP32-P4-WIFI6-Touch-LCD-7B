# BLE Service Test App

The test app registers a deterministic in-memory `PeripheralIface`, so it exercises the complete service/helper path
without requiring a BLE controller, BlueZ daemon, or browser. The same test sources build with ESP-IDF/Unity and
PC/Boost.Test.

Coverage includes:

- helper schema names, described configuration round-trips, UUID/config validation, and JSON byte arrays;
- service and fake-HAL plugin discovery;
- lazy host initialization, idempotent advertising, stop/start reuse, and reverse-order cleanup;
- connection, MTU, subscription, and characteristic-write callback dispatch;
- directed notifications, unsubscribed/notifiable/MTU errors, and binary `0x00`/`0xff` payloads;
- disconnect-triggered advertising restart, startup-failure cleanup, and late-callback rejection.

## PC Build and Run

Run from the `esp-brookesia` repository root:

```bash
cmake -S service/network/brookesia_service_ble/test_apps \
      -B /tmp/service_ble_pc \
      -DBROOKESIA_SERVICE_BLE_TEST_APPS_FORCE_PC=ON
cmake --build /tmp/service_ble_pc -j4
ctest --test-dir /tmp/service_ble_pc --output-on-failure
```

If system Boost.Test is unavailable, add
`-DBROOKESIA_TEST_BOOST_ROOT=/path/to/esp-boost/src` to the configure command.

## ESP-IDF Build

Enable an ESP-IDF environment, then run:

```bash
idf.py -C service/network/brookesia_service_ble/test_apps set-target esp32s3
idf.py -C service/network/brookesia_service_ble/test_apps build
```

The deterministic HAL keeps these component tests independent of the target radio. Real NimBLE and Hosted HCI
controller validation belongs to the board build and hardware smoke-test matrix.

After flashing a CI-compatible board, the Unity menu can also be driven through pytest:

```bash
pytest service/network/brookesia_service_ble/test_apps --target esp32s3 --env "generic,octal-psram"
```
