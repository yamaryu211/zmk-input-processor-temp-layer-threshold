# ZMK Input Processor: Temp Layer with Threshold

A ZMK module that provides a temporary layer input processor with movement threshold support. The layer only activates after cumulative mouse movement exceeds a specified threshold.

ZMKモジュール：移動量の閾値をサポートした一時レイヤー入力プロセッサ。累積マウス移動量が指定した閾値を超えたときにのみレイヤーが有効になります。

## Features / 機能

- **Movement Threshold**: Layer activates only after cumulative movement exceeds threshold  
  **移動量閾値**: 累積移動量が閾値を超えたときにのみレイヤーが有効化
- **Cursor Always Works**: Mouse cursor moves immediately, threshold only affects layer activation  
  **カーソルは常に動作**: マウスカーソルは即座に動き、閾値はレイヤー有効化にのみ影響
- **Auto Reset**: Accumulated movement resets after configurable timeout  
  **自動リセット**: 設定可能なタイムアウト後に累積移動量がリセット
- **Compatible with AML**: Drop-in replacement for `zmk,input-processor-temp-layer`  
  **AML互換**: `zmk,input-processor-temp-layer` の代替として使用可能

## Installation / インストール

Add to your `west.yml`:  
`west.yml` に以下を追加：

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

Add to your `.conf` file:  
`.conf` ファイルに以下を追加：

```conf
CONFIG_ZMK_INPUT_PROCESSOR_TEMP_LAYER_THRESHOLD=y
```

## Usage / 使用方法

In your keymap (`.keymap` or `.dtsi`):  
キーマップファイル内で：

```dts
#define AML 8  // Your AML layer index / AMLレイヤーのインデックス

/ {
    zip_temp_layer_threshold: zip_temp_layer_threshold {
        compatible = "zmk,input-processor-temp-layer-threshold";
        #input-processor-cells = <2>;
        
        // Cumulative movement required before activation
        // AML発動に必要な累積移動量
        threshold = <15>;
        
        // Key positions that won't deactivate the layer
        // 以下で指定したキー以外を押すとAML解除
        excluded-positions = <18 19 20>;
        
        // Don't activate if key was pressed within this time
        // キー入力から300ms間はAMLを発動しない
        require-prior-idle-ms = <300>;
        
        // Reset accumulated movement after this timeout
        // 蓄積された移動量をリセットするまでの時間
        reset-timeout-ms = <200>;
    };
};
```

Then use it in your trackball listener:  
トラックボールリスナーで使用：

```dts
trackball_listener {
    compatible = "zmk,input-listener";
    device = <&trackball>;
    
    // <layer_index timeout_ms>
    input-processors = <&zip_temp_layer_threshold AML 30000>;
};
```

## Parameters / パラメータ

### Input Processor Cells

| Parameter | Description |
|-----------|-------------|
| `layer_index` | Layer to activate (e.g., 8 for AML) / 有効化するレイヤー |
| `timeout_ms` | Deactivate layer after this many ms of no movement / 動きがなくなってからレイヤーを解除するまでの時間 |

### Properties / プロパティ

| Property | Default | Description |
|----------|---------|-------------|
| `threshold` | 0 | Cumulative movement required before activation (0 = immediate) / AML発動に必要な累積移動量（0=即時発動） |
| `require-prior-idle-ms` | 0 | Milliseconds of no key presses before activation is allowed / キー入力後、AML発動を許可するまでの時間 |
| `excluded-positions` | [] | Key positions that won't deactivate the layer / レイヤー解除をトリガーしないキー位置 |
| `reset-timeout-ms` | 200 | Milliseconds without movement before resetting accumulated movement / 累積移動量をリセットするまでの無動作時間 |

## How It Works / 動作原理

1. Trackball movement is accumulated / トラックボールの動きが蓄積される
2. When accumulated movement exceeds `threshold`, AML layer activates / 累積移動量が `threshold` を超えるとAMLレイヤーが有効化
3. If no movement for `reset-timeout-ms`, accumulated movement resets to 0 / `reset-timeout-ms` の間動きがないと累積移動量が0にリセット
4. If no movement for `timeout_ms`, AML layer deactivates / `timeout_ms` の間動きがないとAMLレイヤーが解除

## License

MIT
