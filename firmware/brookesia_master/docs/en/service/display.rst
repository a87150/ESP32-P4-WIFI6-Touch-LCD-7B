.. _service-display-sec-00:

Display Service
===============

:link_to_translation:`zh_CN:[中文]`

- Helper header: ``#include "brookesia/service_helper/media/display.hpp"``
- Helper class: ``esp_brookesia::service::helper::Display``

.. rubric:: Overview

``brookesia_service_display`` owns display outputs, sources, active-source arbitration, frame presentation, touch gestures, and backlight control.

.. rubric:: Core Responsibilities

- Registers display sources and routes frames to active physical outputs.
- Provides service functions for output/source discovery and active-source selection.
- Exposes touch gesture events and output-bound backlight operations.

.. rubric:: DataFlow Integration

The display service registers a visual DataFlow provider. Video and other consumers can discover outputs, select active sources, and present frames through the provider-neutral visual operation contract.

.. rubric:: Integration Position

This component is an independent ESP-Brookesia release component. It can be integrated through ESP-IDF component dependencies and combined with peer framework components as needed.

.. include-build-file:: contract_guides/service/display.inc
