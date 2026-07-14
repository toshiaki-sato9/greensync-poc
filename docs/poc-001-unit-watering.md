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
