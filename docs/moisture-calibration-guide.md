# Home Assistantからの土壌水分校正

AtomS3 Lite firmware 0.3.8以降は、端末ごとにDry/Wetの2点校正値をNVSへ保存する。保存した値は再起動およびOTA更新後も維持される。

## 安全上の注意

- 校正中はポンプを強制的に動作禁止とする。
- Atomの長押し緊急停止は校正より常に優先される。
- Unit Watering全体を水へ沈めない。Wet基準には、実運用と同じ土を十分に湿らせた状態を使用する。
- 校正中はOTAを開始しない。

## 操作手順

1. センサー測定部を乾燥した基準土へ通常と同じ深さで挿入する。
2. Home Assistantで対象端末の「① 乾燥土で校正開始」を押す。
3. 「次に行う校正操作」に乾燥側の案内が表示されたら、値が安定するまで待ち「② 表示された基準値を記録」を押す。
4. 「次に行う校正操作」に湿潤側の案内が表示されたことを確認する。詳細属性の `state` は `AWAITING_WET`、`pendingDryRaw` は取得済みの乾燥値を示す。
5. センサーを、実運用と同じ十分に湿った基準土壌へ通常と同じ深さで挿入する。
6. 値が安定してから、もう一度「② 表示された基準値を記録」を押す。
7. 「次に行う校正操作」に校正完了と表示されたことを確認する。詳細属性の `state` は `SUCCEEDED`、`dryRaw` と `wetRaw` は保存された校正値を示す。

手順3と6では、Home Assistantの記録ボタンの代わりにAtom本体ボタンを短押ししても同じ操作になる。

Dry/Wetの差が100 ADC count未満、またはDry値がWet値以下の場合、校正は `FAILED` となり、以前の値を維持する。開始から5分経過するとタイムアウトする。「校正を中止」でいつでも中止できる。

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
