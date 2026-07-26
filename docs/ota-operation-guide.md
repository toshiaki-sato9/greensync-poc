# AtomS3 Lite ビルド・OTA運用手順

この手順は、開発PCでAtomS3 Liteのファームウェアをビルドし、Firmware Serverへ登録して、対象デバイスへOTA配信するまでを扱う。コマンドは、特記がない限り開発PCのリポジトリ直下で実行する。

## スクリプトの役割

| スクリプト | 役割 |
|---|---|
| `build.sh` | ファームウェアをビルドする。AtomがUSB接続中なら書き込み後にシリアルモニターも開始する |
| `monitor.sh` | ビルドや書き込みをせず、シリアルモニターだけを開始する |
| `upload.sh` | ビルド済みの `firmware.bin` をFirmware Serverへ登録する。AtomへのOTA指示は送らない |
| `firmware-server/deploy.sh` | 登録済みバージョンを指定デバイスへOTAするようMQTTで指示する |

## 1. 初回準備

### 1.1 Firmware Serverの稼働確認

Home Assistantサーバーで確認する。

```bash
cd /opt/greensync/firmware-server
docker compose --env-file .env ps

curl --cacert certs/ca.crt \
  https://192.168.1.35:8443/greensync/ota/ready
```

`{"status":"ready"}` が返ればよい。

### 1.2 Deployment tokenを開発PCへ保存

Home Assistantサーバーで、一時的に取得可能なコピーを作る。

```bash
sudo install \
  -m 600 \
  -o sato \
  -g sato \
  /opt/greensync/firmware-server/secrets/deployment-token.txt \
  /home/sato/greensync-deployment-token.tmp
```

開発PCで取得する。

```bash
mkdir -p .secrets

scp \
  sato@192.168.1.35:/home/sato/greensync-deployment-token.tmp \
  .secrets/firmware-server-token.txt

chmod 600 .secrets/firmware-server-token.txt
wc -c .secrets/firmware-server-token.txt
```

`wc` が `0` でないことを確認する。`.secrets/` はGit管理対象外である。

取得後、開発PCからサーバー上の一時ファイルを削除する。

```bash
ssh sato@192.168.1.35 \
  rm /home/sato/greensync-deployment-token.tmp
```

## 2. リリースバージョンを設定

`firmware/atom-s3-lite/src/Config.h` の `FirmwareVersion` を、これから生成するバージョンへ変更する。

```cpp
constexpr char FirmwareVersion[] = "0.3.5";
```

同じhardware ID・versionで異なるバイナリを再登録することはできない。コードや設定を変更して再ビルドした場合は、新しいversionを使用する。

## 3. ビルドまたはUSB書き込み

```bash
./build.sh
```

- `/dev/cu.usbmodem101` が存在する場合: ビルド、USB書き込み、シリアルモニターを順に実行する。
- USBが未接続の場合: ビルドだけを実行する。

別ポートを使用する場合は引数で指定する。

```bash
./build.sh /dev/cu.usbmodem102
```

生成物は次のファイルである。

```text
firmware/atom-s3-lite/.pio/build/m5stack-atoms3/firmware.bin
```

初回導入、OTA用CA証明書を持たない旧版からの復旧、パーティション変更時はUSB書き込みが必要である。

## 4. シリアルログを確認

`build.sh` がモニターを開始していない場合、USB接続後に実行する。

```bash
./monitor.sh
```

別ポートを使用する場合:

```bash
./monitor.sh /dev/cu.usbmodem102
```

終了は `Ctrl+C`。起動ログのversion、Wi-Fi接続、MQTT接続、OTA command topicのsubscribeが成功していることを確認する。

## 5. Firmware Serverへアップロード

ビルド時と同じversionを指定する。

```bash
./upload.sh 0.3.5
```

スクリプトはバイナリのサイズとSHA-256を表示し、サーバーも受信後に再検証する。最後に公開されたmanifestが表示されれば登録成功である。

`401` はDeployment tokenが不正、`409` は同じversionに異なるバイナリがすでに存在することを示す。

## 6. OTA状態を購読

Home Assistantサーバーの別ターミナルで実行する。`-R` は過去のretainメッセージを表示しないため、今回の遷移だけを確認できる。

```bash
mosquitto_sub \
  -h 192.168.1.35 \
  -R \
  -v \
  -t 'greensync/atom-s3-4c1f980af6e8/ota/#'
```

## 7. 対象デバイスへOTA指示

開発PCで、登録したversionと対象device IDを指定する。

```bash
firmware-server/deploy.sh 0.3.5 4c1f980af6e8
```

期待する状態遷移は次のとおり。

```text
CHECKING
DOWNLOADING
VERIFYING
REBOOTING
SUCCEEDED
```

再起動後に次で実行中versionを確認する。

```bash
mosquitto_sub \
  -h 192.168.1.35 \
  -v \
  -C 1 \
  -W 10 \
  -t 'greensync/atom-s3-4c1f980af6e8/ota/version'
```

## 8. 問題発生時の確認

Atomのリアルタイム生存確認:

```bash
mosquitto_sub \
  -h 192.168.1.35 \
  -R \
  -v \
  -C 1 \
  -W 15 \
  -t 'greensync/atom-s3-4c1f980af6e8/state'
```

- `TLS_ERROR`: 実行中ファームウェアに正しいOTA CA証明書がない。修正版をUSBで書き込む。
- `DOWNLOAD_FAILED`: Firmware ServerへのHTTPS接続、証明書、manifest URLをシリアルログで確認する。
- OTA commandだけ見えて状態が出ない: Atomの生存、シリアルログ、MQTT subscribe結果を確認してから、新しいrequest IDで `deploy.sh` を再実行する。
- `REBOOTING`で止まる: 更新元ファームウェアが再起動後の最終通知保存に対応しているか確認する。

## 通常リリースの最短手順

初回準備とUSB導入が済んでいる場合は、次の順序だけでよい。

```bash
# Config.hのFirmwareVersionを更新後
./build.sh
./upload.sh 0.3.5

# 別ターミナルでOTA状態を購読してから
firmware-server/deploy.sh 0.3.5 4c1f980af6e8
```
