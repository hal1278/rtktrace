# Architecture

## 1. 目的

本書は、`plotcore light`および`plotcore full`が共有する実装境界、application target、依存方向、状態所有および実装上の決定事項を定義する。

利用者から見える動作は`requirements.md`、入力データの意味および計算規則は`data-specification.md`、実装順序と完了条件は`roadmap.md`で定義する。

## 2. アーキテクチャ目標

- POSおよびNMEAの解釈をUIから分離する。
- 正規化データ、座標変換、基準相対処理および統計をlightとfullで共有する。
- 2D軌跡および時系列のplot componentをlightとfullで共有する。
- light固有の固定layoutをcoreおよびplot componentへ混入させない。
- full固有のplot instance管理をlightおよびcoreへ混入させない。
- 数万点規模の複数ファイルを対話的に操作できる性能を確保する。
- Linuxを初期対象とし、Windows対応を妨げない。
- browser、ElectronおよびWebViewへ依存しない。

## 3. Application target

### 3.1 plotcore light

`plotcore light`は最初に実装するapplication targetである。

lightは固定的で簡潔なwindow layoutを構成し、固定数のplot view stateを保持する。通常表示、基準相対表示、水平軌跡、時系列およびBoth表示の切り替えをapplication layerで管理する。

lightは共有plot componentを固定layoutへ配置する。共有plot componentはtab、Both表示またはsplitterの存在を認識しない。

### 3.2 plotcore full

`plotcore full`はlightの後に実装するapplication targetである。

fullは以下の4種類のplot instanceを任意個保持する。

- Normal 2D
- Normal Time Series
- Relative 2D
- Relative Time Series

各instanceは独立したfloating area、view stateおよびlifecycleを持つ。File/Slots areaおよびplot instanceは共通データmodelを参照する。

### 3.3 別targetとしての実装

lightとfullは、同一実行ファイル内のlayout modeではなく、別のexecutable targetとして構成する。

共通処理を共有libraryへ置き、application固有のwindow compositionだけを各targetへ置く。

## 4. 論理component

### 4.1 I/O

責務:

- POS parser
- NMEA parser
- source record位置の追跡
- parse diagnosticの生成
- 入力形式ごとの一時表現生成

I/O componentはGUI、plot view stateおよびapplication targetへ依存しない。

### 4.2 Data model

責務:

- GPST時刻
- 正規化sample
- 読み込み済みfile
- slot順
- file visibility
- common time range
- ENU基準設定
- estimated Hzおよびoverride Hz
- diagnostic
- generation情報

data modelはapplication固有windowを保持しない。

### 4.3 Analysis

責務:

- UTCからGPSTへの変換
- WGS 84 LLH/ECEF変換
- ENU基準位置の決定
- ECEF/ENU変換
- duplicateおよびtime reversal処理
- Hz推定
- Recorded/Expected統計
- 直前のslot 1 epochとの対応付け
- relative sample生成

analysisはUI操作ではなく、明示された入力値および設定値を受け取る。

### 4.4 Plot

責務:

- axis layout
- tickおよびgrid
- 2D trajectory描画
- time-series描画
- markerおよびline描画
- auto-fit
- panおよびzoom
- 数値range、描画領域長およびscaleの相互変換
- world座標とscreen座標の変換

plot componentは、lightのtab構成およびfullのwindow instance管理を認識しない。

### 4.5 Shared UI

共有可能なUI componentの候補:

- File/Slots内容
- file open dialog
- common time range dialog
- ENU基準dialog
- Hz override操作
- diagnostic履歴
- NMEA日付およびジオイド高確認
- 数値入力control
- plot toolbarまたはplot control

具体的な共有範囲は、light実装時に過剰な抽象化を避けながら確定する。

### 4.6 Application composition

light application layerは、共有UIおよびplot componentを固定layoutへ配置する。

full application layerは、File/Slots area、plot window managerおよび任意個のplot instanceを構成する。

application layerはdata modelおよび共有componentを利用するが、共有componentからapplication layerを参照してはならない。

## 5. 依存方向

依存方向は概念上、以下とする。

```text
plotcore-light        plotcore-full
       \                 /
        shared UI / plot
               |
        model / analysis
               |
              I/O
```

循環依存を設けない。

I/O、modelおよびanalysisからGUI frameworkの型を参照しない。

plot componentがGUI frameworkまたはgraphics APIへ依存する場合も、data modelへその型を漏らさない。

## 6. データ所有

### 6.1 共有状態

以下はapplication単位で1組を保持し、全viewから共有する。

- 読み込み済みfile
- 正規化sample
- slot順
- file visibility
- quality filter
- common time range
- ENU基準
- reference match tolerance
- ENU cache
- relative cache
- Hz
- statistics
- diagnostic

plot instance数に応じてこれらを複製しない。

### 6.2 view固有状態

以下はplot viewごとに保持する。

- 軸range
- zoomおよびpan状態
- 描画領域のpixel長
- 表示縮尺
- auto-fit状態
- draw mode
- time-seriesの表示成分
- Upまたは楕円体高の選択

lightは固定数のview stateを持つ。

fullは各plot instanceが1個のview stateを持つ。

### 6.3 full固有状態

fullだけが以下を保持する。

- PlotWindowId
- PlotType
- plot instance list
- instance title
- visible state
- pending deletion
- floating areaの位置および寸法

これらを共有coreへ含めない。

## 7. データ処理pipeline

概念上の処理順序は以下とする。

```text
file input
  -> format-specific parse
  -> time and quality normalization
  -> LLH to ECEF
  -> record validation and normalization
  -> file model
  -> Hz estimation
  -> common-time-range indexing
  -> ENU cache
  -> relative cache
  -> statistics
  -> plot data view
  -> rendering
```

各段階は、可能な範囲で前段の結果を破壊せず、再計算対象を限定する。

## 8. 再計算とcache

### 8.1 不変データ

読み込み後の以下は原則として不変とする。

- GPST時刻
- ECEF
- 楕円体高
- quality
- source record情報
- continuity

### 8.2 ENU cache

ENU基準、slot 1またはcommon time rangeの変更時に、必要なENU cacheを再生成する。

ECEFを保持し、parserを再実行しない。

### 8.3 Relative cache

以下の変更時にrelative cacheを再生成する。

- slot 1
- slot順
- common time range
- reference match tolerance
- normalized data
- ENU基準に依存するrelative成分

Normal 2DおよびNormal Time Seriesはrelative cacheへ依存しない。

Relative 2DおよびRelative Time Seriesは同じrelative sampleを共有する。

### 8.4 Plot view

axis range、zoom、pan、draw modeその他のview state変更では、parser、座標正規化およびreference matchingを再実行しない。

## 9. plotcore lightのcomposition

lightは概念上、以下を保持する。

```text
LightApplicationState
  file/slot UI state
  normal trajectory view
  normal time-series view
  relative trajectory view
  relative time-series view
  selected tab
  Both splitter state
```

Both表示は、trajectory plot componentとtime-series plot componentを同一画面へ配置するapplication固有compositionとする。

## 10. plotcore fullのcomposition

fullは概念上、以下を保持する。

```text
FullApplicationState
  file/slot area state
  plot instances [0..n]
```

各plot instanceは概念上、以下を保持する。

```text
PlotInstance
  id
  type
  title
  visible
  view state
  floating position
  floating size
```

同じ種類のinstanceを複数生成できるよう、表示名とは別に安定した一意IDを持つ。

非表示instanceはview stateを保持し、描画対象から除外する。

## 11. Rendering backend

GUI framework、window/input backend、graphics APIおよびplot libraryは未確定である。

候補の比較とprototype検証後に、以下を確定する。

- GUI framework
- window/input backend
- graphics API
- Dear ImGuiを使用する場合のbranch
- ImPlotまたは独自plot renderer
- multi-viewportの初期対応有無

backend選定によって、I/O、model、analysisおよびdata specificationを変更しない構成とする。

## 12. Threading

初期実装でworker threadを使用する範囲は未確定である。

少なくとも、将来以下をUI thread外へ移動できる境界を維持する。

- file I/O
- parseおよびnormalization
- ENU cache生成
- relative cache生成
- statistics

非同期処理を導入する場合はgenerationを結果へ付与し、古い設定に基づく完了結果を破棄する。

## 13. Build target

想定するtarget構成は以下とする。具体的なtarget名はbuild system確定時に調整できる。

```text
shared libraries
  plotcore-io
  plotcore-model
  plotcore-analysis
  plotcore-plot
  plotcore-ui-common

executables
  plotcore-light
  plotcore-full
```

最初は`plotcore-light`だけをbuild可能としてよい。shared libraryは必要以上に細分化せず、依存境界が明確になる最小単位で構成する。

## 14. Testing

GUIを必要としない以下はunit test可能なcomponentとして実装する。

- POS parser
- NMEA parserおよびchecksum
- UTC/GPST変換と閏秒
- duplicateおよびtime reversal
- LLH/ECEF/ENU
- common time range
- reference matching
- Hz推定
- Expected count
- axis rangeおよびauto-fit計算

plot renderingは、計算部分とgraphics API呼び出しを可能な範囲で分離する。

## 15. 確定事項

- application targetは`plotcore light`および`plotcore full`とする。
- lightを先に実装し、その後にfullを拡張として実装する。
- lightとfullは共通のdata-processingおよびplot componentを使用する。
- lightとfullは別のapplication targetとする。
- fullは4種類のplot instanceを任意個保持できる。
- coreはapplication固有window layoutを参照しない。

## 16. 未確定事項

- 実装言語の最終確定
- GUI framework
- window/input backend
- graphics API
- plot libraryまたは独自renderer
- threading model
- layout persistence
- fullのdocking対応
- fullのmulti-viewport対応
- 共有libraryの具体的な分割
