# OTAクライアント導入・検証手順

## 1. 初回導入

OTA用のA/Bパーティションテーブルは通常のアプリ更新では変更できないため、最初の1回はUSBで書き込む。

```bash
cd firmware/atom-s3-lite
cp include/Secrets.example.h include/Secrets.h
# Secrets.h のWi-Fi、MQTT、OTA_BASE_URL、OTA_CA_CERTを環境に合わせて編集
pio run --target upload
pio device monitor
```

`OTA_BASE_URL` はFirmware Serverの公開HTTPS URLである。Home Assistantと同じIPに置く場合もHTTPS証明書のSANに、デバイスが接続時に使うホスト名またはIPが含まれている必要がある。クラウド移行時は `Secrets.h` の `OTA_BASE_URL`、`OTA_MANIFEST_URL`、`OTA_CA_CERT` のみ差し替える。

## 2. 更新バイナリの作成と公開

`Config.h` の `FirmwareVersion` を現在の端末より新しいSemVerへ変更してビルドし、生成物をFirmware Serverへ公開する。

```bash
cd firmware/atom-s3-lite
pio run
cd ../..

# scripts/publish-firmware.sh 冒頭の VERSION などを環境に合わせて設定
scripts/publish-firmware.sh
```

## 3. MQTTからの更新確認

起動ログの `clientId=greensync-atom-s3-<hardware_id>` から端末IDを確認する。まず `check` を送信する。

```bash
mosquitto_pub -h <mqtt-host> \
  -t 'greensync/atom-s3-<hardware_id>/ota/command' \
  -m '{"action":"check","requestId":"ota-check-001","targetVersion":"0.4.0"}'
```

`greensync/atom-s3-<hardware_id>/ota/state` が `CHECKING`、`AVAILABLE` の順になることを確認する。Home Assistantでは同じ情報が `OTA Status` センサーに表示される。

## 4. OTA実行

```bash
mosquitto_pub -h <mqtt-host> \
  -t 'greensync/atom-s3-<hardware_id>/ota/command' \
  -m '{"action":"install","requestId":"ota-install-001","targetVersion":"0.4.0"}'
```

次を確認する。

- 散水中なら直ちにポンプが停止し、OTA中は再開しない。
- 状態が `CHECKING` → `DOWNLOADING` → `VERIFYING` → `REBOOTING` と遷移する。
- ダウンロードしたサイズとSHA-256がmanifestに一致した場合だけ次スロットを起動する。
- 再起動後にWi-FiとMQTTへ接続できると `SUCCEEDED` になり、`ota/version` が新バージョンになる。
- 再起動後120秒以内にWi-FiとMQTTへ接続できない場合は、bootloader rollbackで旧スロットへ戻る。

## 5. 現時点で実施できる検証

実機へのOTA公開前でも、PlatformIOビルド、A/Bパーティション生成、Home Assistant Discovery、MQTTコマンドの形式、manifestの機種・バージョン・URL・サイズ検証までは確認できる。実機とFirmware Serverを接続すると、以下の異常系も確認できる。

| ケース | 期待結果 |
|---|---|
| 異なるhardwareのmanifest | `HARDWARE_MISMATCH` |
| 現在以下または指定と異なるversion | `VERSION_NOT_ALLOWED` |
| 許可prefix外のmanifest/bin URL | `URL_NOT_ALLOWED` または `MANIFEST_INVALID` |
| binを1 byte変更 | `HASH_MISMATCH`、再起動しない |
| 同一requestIdを再送 | `DUPLICATE_REQUEST` |
| 緊急停止中 | `EMERGENCY_STOP_ACTIVE` |
| 更新後のネットワークを遮断 | 120秒後に旧firmwareへrollback |

SHA-256は転送破損と、信頼できるmanifestに対するbinの不一致を検出する。manifestとbinを同時に置換できる攻撃者に対する真正性は保証しないため、製品化時はデバイス内の公開鍵による署名検証を追加する。
