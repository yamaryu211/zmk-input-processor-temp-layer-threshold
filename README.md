# ZMK Input Processor: Temp Layer with Threshold

A ZMK module that provides a temporary layer input processor with movement threshold support. The layer only activates after cumulative mouse movement exceeds a specified threshold.

## Features

- **Movement Threshold**: Layer activates only after cumulative movement exceeds threshold
- **Cursor Always Works**: Mouse cursor moves immediately, threshold only affects layer activation
- **Auto Reset**: Accumulated movement resets after configurable timeout
- **Compatible with AML**: Drop-in replacement for `zmk,input-processor-temp-layer`

## Installation

Add to your `west.yml`:

```yaml
manifest:
  remotes:
    - name: yamaryu211
      url-base: https://github.com/yamaryu211
  projects:
    - name: zmk-input-processor-temp-layer-threshold
      remote: yamaryu211
      revision: main
```

## Usage

In your keymap (`.keymap` or `.dtsi`):

```dts
/ {
    zip_temp_layer_threshold: zip_temp_layer_threshold {
        compatible = "zmk,input-processor-temp-layer-threshold";
        #input-processor-cells = <2>;
        
        // Movement threshold before layer activation (e.g., 25 pixels)
        threshold = <25>;
        
        // Key positions that won't deactivate the layer
        excluded-positions = <18 19 20>;
        
        // Don't activate if key was pressed within this time
        require-prior-idle-ms = <300>;
        
        // Reset accumulated movement after this timeout
        reset-timeout-ms = <200>;
    };
};
```

Then use it in your trackball listener:

```dts
&trackball_listener {
    // <layer_index timeout_ms>
    input-processors = <&zip_temp_layer_threshold 8 30000>;
};
```

### Parameters (input-processor-cells)

| Parameter | Description |
|-----------|-------------|
| `layer_index` | Layer to activate (e.g., 8 for AML) |
| `timeout_ms` | Deactivate layer after this many ms of no movement |

### Properties

| Property | Default | Description |
|----------|---------|-------------|
| `threshold` | 0 | Cumulative movement required before activation (0 = immediate) |
| `require-prior-idle-ms` | 0 | Milliseconds of no key presses before activation is allowed |
| `excluded-positions` | [] | Key positions that won't deactivate the layer |
| `reset-timeout-ms` | 200 | Milliseconds without movement before resetting accumulated movement |

## License

MIT
