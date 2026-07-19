# PoC-001 Unit Watering

## 目的
M5Stack Unit Wateringを使い、土壌水分のADC値をHome Assistantに蓄積し、散水前後の変化を確認する。

## 使用機材
- M5Stack Unit Watering
- M5Stack Atom Lite
- Home Assistant
- ESPHome

## 確認項目
- ADC値が取得できること
- 乾燥時に値が大きく、湿潤時に値が小さくなること
- Home Assistantに履歴が残ること
- 手動でポンプON/OFFできること
- 自動散水は最後に実施する

## 注意
初期段階では自動散水しない。水漏れ・過散水を避けるため、まず手動操作で確認する。

## Home Assistant から散水閾値を変更する
- MQTT discovery により `Watering Threshold` の `number` エンティティが作成される
- Home Assistant から閾値を変更すると、デバイスの散水判定に反映される
- 閾値はESP32の永続領域に保存され、再起動後も維持される

### 動作確認
- Home Assistant で `Watering Threshold` を開き、値を変更する
- デバイスのシリアルログで `Watering threshold updated via MQTT` が出ることを確認する
- 以後の判定で、新しい閾値が `threshold=` としてログに表示されることを確認する

## Emergency Stop 応答性

### 背景
- 現状の実装では `PumpController::waterForMs()` が `delay(durationMs)` を使っており、散水中はメインループが停止する
- メインループ末尾にも `delay(10000)` があり、待機中もボタン入力を処理できない
- このため、`BtnA` による emergency stop は散水中または待機中に即時反応できない

### 要件
- `BtnA` 長押しによる emergency stop を、待機中・散水中のどちらでも受け付ける
- emergency stop を受け付けたら、ポンプを即時OFFにする
- emergency stop 中は自動散水を再開しない
- MQTT 通信と Home Assistant 連携は、emergency stop 対応後も継続する

### 実装方針
- メインループから固定 `delay()` をなくし、`millis()` ベースの非ブロッキング制御に変更する
- 散水処理は `IDLE` / `WATERING` / `EMERGENCY_STOP` の状態で管理する
- 散水開始時に開始時刻を保持し、設定時間を超えたらポンプをOFFにして `IDLE` に戻す
- センサ読取・MQTT publish は一定周期で実行し、ループ自体は止めない
- `BtnA` は毎ループ監視し、emergency stop 検知時は状態に関係なくポンプをOFFにして `EMERGENCY_STOP` に遷移する

### 初期仕様
- emergency stop の解除方法は現行どおり自動解除しない
- 解除方法の追加は別issueで扱う
- 散水中断時は MQTT state に停止後の状態を反映する

### 完了条件
- 散水中に `BtnA` 長押しでポンプが停止する
- 待機中に `BtnA` 長押しで emergency stop に入る
- emergency stop 中は閾値を下回っても自動散水しない
- MQTT publish と Home Assistant 表示が継続する
