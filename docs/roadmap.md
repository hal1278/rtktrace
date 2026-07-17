# Roadmap

## 1. 方針

`plotcore`は、共通のデータ処理、解析およびplot componentを先に構築し、最初のapplication targetとして`plotcore light`を実装する。

lightの機能、性能およびcomponent境界を検証した後、同じshared componentを使用して`plotcore full`を実装する。

各phaseの詳細実装は、前phaseの完了条件を満たした後に確定する。性能最適化は測定結果に基づいて行う。

## 2. Phase 0: 仕様および技術選定

### 目的

実装開始に必要な要件、データ仕様、application構成および技術stackを確定する。

### 内容

- `requirements.md`の共通、lightおよびfull要件の整理
- `data-specification.md`の入力・時刻・座標・品質・matching・統計仕様
- `architecture.md`のapplication targetと共有境界
- C++20、Nix flake、MesonおよびNinjaによるbuild stackの確定
- Dear ImGui、SDL3、OpenGL3 renderer backendおよびImPlotによるGUI stackの確定
- SDL3によるLinux native window作成確認
- SDL3によるWindows x86-64 cross build確認
- Dear ImGui公式SDL3/OpenGL3 backendのbuild確認
- ImPlotのbuild確認
- Linux GUI smoke確認
- shared data-processing APIは同期APIとし、各処理は呼出元threadで完了する。
- 初期実装ではworker threadを使用しない。
- I/O、parse、normalization、ENUおよびrelative処理はGUI frameworkの型に依存させず、将来worker threadへdispatchできる境界を維持する。
- 非同期job/result用generation IDは非同期処理を実際に導入する時点で追加する。
- cache invalidationまたはcache version管理と、非同期job/resultのgeneration IDを混同しない。

### 完了条件

- lightの受入対象がrequirementsで識別できる。
- full固有要件がlightの初期scopeと分離されている。
- parserおよび正規化の実装に必要なdata specificationが定義されている。
- application固有UIがcoreへ依存逆流しない構成が定義されている。
- 初期実装stackが確定している。
- 初期threading modelが確定している。
- Linux native packageおよびWindows x86-64 cross packageが同じMeson projectからbuildできる。
- graphical sessionを必要としないflake checkが実行できる。

現在のGUI/build foundationは上記の完了条件を満たしているため、Phase 0は完了とする。

## 3. Phase 1: Shared data foundation

### 目的

GUIに依存しない共通データ処理を完成させる。

### 内容

- GPST整数nanosecond型
- UTC civil timeおよび閏秒table
- WGS 84 LLH、ECEFおよびsource lineを保持するnormalized sample
- loaded fileおよびslot model
- POS parser
- NMEA parser
- diagnostic
- duplicate epochおよびtime reversal
- WGS 84 LLH/ECEF変換
- optional start/endを持つcommon time range
- Hz推定およびoverride
- Recorded/Expected統計

### 完了条件

- 対応POSおよびNMEAをsample列へ変換できる。
- parserの部分読み込みとdiagnosticをtestできる。
- UTC/GPST、日跨ぎ、quality変換、duplicateおよびtime reversalのunit testが通る。
- GUIを起動せずに各fileの統計を算出できる。

現在のshared data foundationは、POS/NMEAの正規化、diagnostic、時刻・座標変換、
common time range、Hz推定およびRecorded/Expected統計とそのunit testを含み、上記の
完了条件を満たしているため、Phase 1は完了とする。

## 4. Phase 2: Shared coordinate and relative processing

### 目的

lightおよびfullで共有する座標処理と基準相対dataを完成させる。

### 内容

- ENU基準方式
- ECEF/ENU変換
- ENU cache
- common time range index
- 直前のslot 1 epochとのmatching
- tolerance checkの有効・無効および最大時刻差
- relative E/N/U/H
- ECEF normによる基準相対距離
- relative cache
- cache invalidationおよびcache revision管理

### 完了条件

- slot 1、common time rangeおよびENU基準変更に対する再計算結果がtestできる。
- 未来のreference epochを使用しない。
- tolerance checkが有効な場合は最大時刻差を超えるcomparison sampleを除外する。
- tolerance checkが無効な場合は最後のreference sampleを継続して使用する。
- Normal dataとRelative dataを共有modelから取得できる。

現在のshared coordinate and relative processingは、ENU基準とcache、common time range
index、reference matching、tolerance、relative E/N/U/H、ECEF三次元距離、slot対応の
relative cacheおよびrevision管理を含み、上記の完了条件を満たしているため、Phase 2は
完了とする。

## 5. Phase 3: Shared plotting components

### 目的

application layoutに依存しないtrajectoryおよびtime-series componentを完成させる。

### 内容

- tickおよびgrid
- Normal/Relative 2D trajectory
- Normal/Relative Time Series
- line、pointおよびline+point
- quality color
- slot marker
- drawing order
- m/px
- numeric rangeおよびaxis length
- Up/ellipsoidal height
- ImPlot baseline prototypeによるaxis behavior、auto-fit、equal scale、subplotおよびpan/zoomの実装・評価
- 数万点を持つ複数fileでの描画性能測定

### 完了条件

- plot componentがlightのtabおよびfullのwindow managerを参照しない。
- 同じcomponentへNormalまたはRelative data viewを渡して描画できる。
- 数万点を持つ複数fileで基本操作の性能を測定できる。
- RTKPLOT確認済みのaxisおよびquality規則が反映されている。

Normal/Relative共通plot data view、file visibility、quality filter、水平軌跡の
等縮尺auto-fit、時系列の位置・時刻axis auto-fit、ImPlot描画component、slot marker、
quality/slot drawing order、pan/zoom後のaxis metrics、およびRTKPLOT準拠tick/gridを
実装済みである。4 file × 10,000 sampleのheadless regression testは、描画batch準備と
trajectoryおよび3段time-seriesを含む1 frameの時間を継続的に測定して出力する。

## 6. Phase 4: plotcore light

### 目的

最初の利用可能なapplicationとしてplotcore lightを完成させる。

### 内容

- fixed window layout
- File/Slots sidebar
- NormalおよびReferenceのtab
- Trajectory、Time SeriesおよびBoth
- Both splitter
- file open
- visibilityおよびquality filter
- common time range dialog
- ENU基準dialog
- Hz override
- Recorded/Expected status
- diagnostic history
- modal confirmation

### 完了条件

- `requirements.md`でlight初期scopeに指定したworkflowを実行できる。
- POSおよびNMEAを複数同時に表示できる。
- NormalおよびRelativeの2D/time-seriesを操作できる。
- 数値range、axis length、scale、auto-fit、panおよびzoomが仕様どおり動作する。
- partial loadおよびwarningを利用者が確認できる。
- Linux上で想定データ量に対する性能を確認できる。

現在のlight applicationは、GUI framework非依存のapplication state、POS/NMEAのfile
workflow、100 MiB以上の確認、形式およびNMEA補完dialog、固定layout、slot sidebar、
6表示モード、Both splitter、共通時刻範囲、ENU基準、reference matching、Hz override、
Recorded/Expected summary、diagnostic履歴、quality filter、描画設定、auto-fit、および
trajectory・時系列の数値range/scale入力を実装済みである。数値入力はEnter確定、
無効値の赤表示、位置およびscale単位切替に対応し、時系列縦軸rangeの一回適用と
trajectoryの矢印キーpanも提供する。trajectoryと時系列は通常ホイールの中心固定zoom、
`Ctrl`ホイールのカーソル固定zoomに対応し、時系列ではhover中の縦軸または最下部の
共有時刻軸だけを変更する。追加読み込み、visibility、並べ替えおよび削除ではplot rangeを
維持する。残る描画領域寸法変更を含む詳細なrange interactionとLinux上の想定データ量での
application-level性能確認を完了した時点でPhase 4完了とする。

## 7. Phase 5: Light validation and shared-boundary cleanup

### 目的

full実装前に、light固有処理とshared componentの境界を検証する。

### 内容

- light固有tab/layout依存の抽出
- data ownershipの確認
- immutable dataとcacheの整理
- view state型の整理
- renderer bottleneck測定
- worker thread要否の再評価
- full用instance APIの確定
- regression test拡充

### 完了条件

- shared plot componentがfixed layoutへ依存しない。
- fullで再利用するためにlightを大幅改修する必要がない。
- plot instanceごとに独立保持すべきstateが定義されている。
- fullの実装開始条件と性能上限が明確になっている。

## 8. Phase 6: plotcore full

### 目的

任意個のfloating plot areaを扱うworkspace型applicationを実装する。

### 内容

- File/Slots area
- PlotWindowIdおよびPlotType
- Normal 2D
- Normal Time Series
- Relative 2D
- Relative Time Series
- 任意個のplot instance
- instance生成、表示、非表示、再表示および削除
- floating areaの移動およびresize
- instance固有view state
- window一覧
- 非表示windowの描画抑止
- shared ENUおよびrelative cache

### 完了条件

- 4種類のplot instanceを任意個生成できる。
- 同種の複数instanceを識別し、独立して操作できる。
- instance追加によってdata normalizationおよびreference matchingを重複実行しない。
- 非表示instanceのstateを保持し、再表示できる。
- 複数instance表示時の性能を測定できる。

## 9. Phase 7: Full extensions

以下はfullの基本機能完了後に必要性を評価する。

- docking
- multi-viewport
- layout persistence
- project/workspace persistence
- linked axes
- linked sample highlighting
- downsamplingおよびLOD
- custom GPU renderer
- Windows配布
- 追加plot種別

本phaseの項目は、基本full applicationの完了条件には含めない。docking、multi-viewportおよびcustom GPU rendererを採用するかは未確定であり、custom rendererはbaselineの測定結果に基づいてのみ検討する。
