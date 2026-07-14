# Requirements

## 1. Purpose

`plotcore` provides fast, interactive visualization of GNSS positioning solution data for inspecting and comparing trajectories and solution quality.

The initial product supports loading positioning solution files and NMEA position logs, displaying multiple trajectories in a common local coordinate system, and examining horizontal position and East/North/Up time-series plots.

The project does not aim to perform positioning computations or reproduce the complete RTKPLOT feature set. It focuses on the plotting functions required for efficient inspection of recorded positioning results.

## 2. 対象範囲

### 2.1 初期実装の対象

初期実装では、以下の機能を対象とする。

- RTKLIBまたはMRTKLIB形式に準じる測位解ファイルの読み込み
- NMEA形式の位置ログの読み込み
- 複数ファイルの一括読み込み
- 複数ファイルの同時表示
- 共通の局所East/North座標系による水平軌跡の表示
- East、Northおよび鉛直位置成分の時系列表示
- 時系列表示の鉛直位置成分として、局所座標系のUpまたは楕円体高を選択する機能
- 測位解品質による表示の区別
- ファイル単位の表示・非表示の切り替え
- 測位解品質の分類単位での表示・非表示の切り替え
- マウス操作によるパンおよびズーム
- 各軸の表示範囲を数値で指定する機能
- 表示中のデータに対する表示範囲の自動調整
- 軸、単位、目盛りおよびグリッドの表示

対応する入力形式およびデータの解釈規則の詳細は、`data-specification.md`で定義する。

### 2.2 初期実装の対象外

初期実装では、以下の機能を対象外とする。

- GNSS測位計算
- 受信機からのリアルタイム入力
- シリアルポートからの入力
- TCP、UDPまたはNTRIPによるストリーム入力
- RINEX観測ファイルまたは航法ファイルの処理
- RTCMのデコード
- 衛星配置図
- 衛星観測値および追尾状態のプロット
- 残差、アンビギュイティまたはDOPのプロット
- 地図タイルまたは地理的背景地図の表示
- 三次元表示
- 軌跡データの編集
- 入力ファイルの変更
- RTKPLOTとの完全な機能互換性
- RTKPLOTとの設定互換性
- ブラウザ、ElectronまたはWebViewを利用した実行(恒久的に対象外)

### 2.3 将来検討する機能

以下の機能は、初期実装の表示機能および性能を検証した後に検討する。

- 任意の2ファイルを基準側および比較側として選択する機能
- 選択した2ファイルの最近傍epoch間における距離の時系列表示
- 選択した2ファイルの最近傍epoch間における相対水平位置の表示
- 三次元軌跡の表示
- 衛星数、衛星追尾状態、DOPおよびsky plotを含む観測状態の表示
- プロット画像の保存
- マウスオーバーしたsampleの時刻、座標および関連情報の表示
- 同一sampleを時系列、水平軌跡および観測状態等の複数のプロット上で連動して強調表示する機能
- 複数のプロットを配置するタイリングレイアウト
- Multiple Document Interface（MDI）または同等の複数ビュー管理
- プロット配置および表示設定の組合せをレイアウトセットとして保存する機能
- 地理的背景地図上での軌跡表示
- 緯度、経度および高さによる位置表示
- ドラッグ・アンド・ドロップによるファイル読み込み
- 入力ファイル変更時の自動再読み込み
- 処理済みデータの出力
- プロジェクトまたはワークスペースの保存
- リアルタイムデータ入力
- RTKPLOTが提供する追加のプロット種別
- Windows向け配布パッケージ

本節に将来検討項目として記載した機能は、初期実装の要件には含めない。
