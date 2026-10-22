# Tab5 おうちパネル デモ

M5Stack Tab5 で、家電のお手入れと消耗品の予定を期日の近い順に並べ、タップで記録する「おうちパネル」のデモ版です。Wi-Fi もサーバーも不要で、Tab5 本体だけで操作を体験できます。データは架空で、記録は保存しません。

**ブラウザから書き込む: https://sin1.studio/Tab5HomePanelDemo/**（PC の Chrome / Edge）

解説ブログ: [AI秘書xangiで日用品の在庫管理をしてみる（予定）](https://sin1.studio/blog/2026-10-22-xangi-home-stock/)

- チップの版（ESP32-P4 の初期ロット v1.x と v3.x）を書き込みページが読み取り、合う方のファームウェアを自動で選びます。
- 書き込みには [esptool-js](https://github.com/espressif/esptool-js) を使っています。

## できること

| 操作 | 内容 |
|---|---|
| やった | お手入れを今日やったことにする（次の期日が延びる。消耗品を使う作業は在庫も減る） |
| 開封 | 消耗品を開けた（在庫 −1。開封の間隔から次に買う日を計算し直す） |
| 届いた | 消耗品が届いた（数を選んで在庫 +） |
| +1日 / +7日 | 日付を進める。期日を迎えた品は「お知らせ」が出る |
| 最初から | デモのデータを最初の状態に戻す |
| ？ 説明 | しくみの説明と、このページの QR コード |

見積もり方（本番版と同じ）:

- ペース = 直近3回の開封間隔の中央値（履歴が2件未満なら目安の日数）
- 補充が必要な日 = 最後に開封した日 + ペース ×（在庫 − 最低在庫 + 1）
- お知らせの日 = 補充が必要な日 − 届くまでの日数

本番版は Wi-Fi で自宅PCの台帳とつながります。記録すると、AI アシスタント [xangi](https://github.com/karaage0703/xangi) が Discord へのお知らせの予約を入れ直します。

## ビルド

ESP32-P4 には [pioarduino](https://github.com/pioarduino/platform-espressif32)（55.03.311）が必要です。55 系は Python 3.10 以上の PlatformIO で動きます。

```powershell
# PowerShell で（Git Bash からだと ESP-IDF のツール導入が "MSys/Mingw is not supported" で止まる）
uvx --python 3.13 --from platformio==6.1.19 pio run -e tab5        # 初期ロット（v1.x）
uvx --python 3.13 --from platformio==6.1.19 pio run -e tab5_rev3   # v3.x
powershell -ExecutionPolicy Bypass -File scripts\build-web.ps1     # 両方をビルドして docs\firmware に置く
```

- `core_dir = C:/pio` は Windows の 260 文字制限への対策です。
- パーティションは `default_16MB.csv` です（日本語フォント込みで約2.8MB）。
- 書き込みページは `docs/`（GitHub Pages）です。ファームウェアは結合イメージ `firmware.factory.bin` を、アドレス 0x0 から書きます。

## ライセンス

MIT License © 2026 sin1's studio
