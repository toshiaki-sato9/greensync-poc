# Home Assistantからの土壌水分校正

AtomS3 Lite firmware 0.3.8以降は、端末ごとにDry/Wetの2点校正値をNVSへ保存する。保存した値は再起動およびOTA更新後も維持される。

## 安全上の注意

- 校正中はポンプを強制的に動作禁止とする。
- Atomの長押し緊急停止は校正より常に優先される。
- Unit Watering全体を水へ沈めない。Wet基準には、実運用と同じ土を十分に湿らせた状態を使用する。
- 校正中はOTAを開始しない。

## 操作手順

1. Home Assistantで対象端末の「Start Moisture Calibration」を押す。
2. 「Moisture Calibration Status」が `AWAITING_DRY` になったことを確認する。
3. センサー測定部を乾燥基準状態に置き、値が安定してからAtomボタンを短押しする。
4. 状態が `AWAITING_WET` になったことを確認する。
5. センサーを、実運用と同じ十分に湿った基準土壌へ通常と同じ深さで挿入する。
6. 値が安定してからAtomボタンを短押しする。
7. 状態が `SUCCEEDED` になり、`dryRaw`と`wetRaw`が更新されたことを確認する。

Dry/Wetの差が100 ADC count未満、またはDry値がWet値以下の場合、校正は `FAILED` となり、以前の値を維持する。開始から5分経過するとタイムアウトする。「Cancel Moisture Calibration」でいつでも中止できる。

## MQTTインターフェース

`<id>`は12桁のAtom端末ID。

```text
Command: greensync/atom-s3-<id>/calibration/command
State:   greensync/atom-s3-<id>/calibration/state
```

開始:

```bash
mosquitto_pub \
  -h 192.168.1.35 \
  -t 'greensync/atom-s3-<id>/calibration/command' \
  -m START
```

状態確認:

```bash
mosquitto_sub \
  -h 192.168.1.35 \
  -v \
  -t 'greensync/atom-s3-<id>/calibration/state'
```

代表的な状態は `IDLE`、`AWAITING_DRY`、`AWAITING_WET`、`SUCCEEDED`、`FAILED`、`CANCELLED`。
