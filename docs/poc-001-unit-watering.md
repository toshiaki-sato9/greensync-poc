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
