# Roadmap

## 1. 方針

`rtktrace`は、共通のデータ処理、解析およびplot componentを先に構築し、最初のapplication targetとして`rtktrace light`を実装する。

lightの機能、性能およびcomponent境界を検証した後、同じshared componentを使用して`rtktrace full`を実装する。

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

## 6. Phase 4: rtktrace light

### 目的

最初の利用可能なapplicationとしてrtktrace lightを完成させる。

### 内容

- fixed window layout
- File/Slots sidebar
- NormalおよびReferenceのtab
- Trajectory、Time SeriesおよびBoth
- Both splitter
- file open
- visibilityおよびquality filter
- common time range dialog
- ENU基準toolbar pull-downおよび`User specified`の`Edit...`操作
- Fit ratio、default point size、center-fixed zoomおよびwindow resize modifierのOptions
- tick以外のabsolute GPST固定format
- Hz override
- Recorded/Expected status
- diagnostic history
- modal confirmation
- modal dialogの`Esc`による`Cancel`

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
Recorded/Expected summary、notification履歴とcaution indicator、quality filter、描画設定、auto-fit、および
trajectory・時系列の数値range/scale入力を実装済みである。数値入力はEnter確定、
非編集時3桁・編集中高精度表示、無効値の赤表示とfocus離脱時の有効値復帰、位置および
scale単位切替に対応し、時系列縦軸rangeの一回適用とtrajectoryの矢印キーpanも提供する。
summaryは5 slot分の行を確保し、6 slot以上をscroll表示する。Expected値を算出できない行は
その状態を明示する。時系列はEast、Northおよび鉛直成分を個別に表示選択できる。
時系列のwheel zoomではhover中の縦軸または最下部の共有時刻軸だけを変更する。
追加読み込み、visibility、並べ替えおよび削除ではplot rangeを
維持する。4 file × 10,000 sampleのapplication-level headless testでは、全共有pipelineと
light固定layoutの1 frameを継続的に計測する。window/panel resize時のtrajectory scale維持と
Options指定modifierによるホイールwindow寸法変更も実装済みである。各軸のmin/max/描画px長をplot上部へ
表示し、min/max上のホイール操作を対象軸別range APIへ接続した。水平軌跡は表示縮尺優先と
軸優先を切り替え、scale入力時は描画領域または軸rangeを固定対象として選択できる。window
寸法が制約へ達した場合は選択された固定対象を保ち、指定rangeを維持できないことを通知する。
仕様更新後のFit ratio、default point sizeとcurrent session値の分離、quality filter buttonの
状態色、pointer固定および中心固定zoom、重複しないwindow resize modifier、tick以外の
absolute GPST固定format、toolbar pull-downによるENU reference method選択、ならびにmodal
dialogの`Esc`処理を実装し、headless testで再検証済みである。slot間およびquality間の描画順も
Optionsから切り替え可能とした。これによりPhase 4は現行仕様に対して完了とする。

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

Phase 5の最初の境界整理として、light配下に置かれていたGUI非依存のfile workflow、
処理設定、ENU/relative cacheおよびdiagnostic ownershipを共有`PlotSessionState`と
`rtktrace-session` targetへ移した。light GUIはこの共有sessionをcompositionし、将来の
fullもlight namespaceへ依存せず同じsessionとdata viewを利用できる。

data/cacheと共有表示設定のownership、instance固有view state、monotonic `PlotWindowId`と
4種類の`PlotType`を使用するfull application API、session/filter revisionによるprepare条件、
非表示instanceの処理抑止、およびworker thread再評価条件を`architecture.md`へ確定した。
4 file × 10,000 sampleの継続計測では共有pipelineより描画が支配的であり、full実装前に
light固有layoutをshared sessionまたはplot componentから除去できている。上記の完了条件を
満たしているため、Phase 5は完了とする。

## 8. Phase 6: rtktrace full

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

Phase 6のfoundationとして、backend非依存の`FullApplicationState`と
`rtktrace-full-state` targetを追加した。4種類の`PlotType`、再利用しないmonotonic
`PlotWindowId`、instance作成、検索、title変更、表示・非表示、削除、共有quality filter
revision、および単一`PlotSessionState` ownershipをunit testで検証する。GUI compositionと
floating plot lifecycleはこのstate API上へ実装する。

次のcomposition境界として、`PlotWindowId`ごとに独立した`ImPlotComponent`を所有する
full GUI runtimeを追加した。visibleなinstanceだけをsession、quality filterおよびOptionsの
revision差分時にprepareし、非表示中はprepareとrenderを抑止する。再表示時は保持したview
stateを再fitせず最新の共有dataへ遅延更新し、削除時は対応するruntimeを破棄する。

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

## 10. Phase未定の将来タスク

### configurationおよびapplication stateの永続化

configurationおよびapplication stateの永続化は、`rtktrace light`初期実装の完了条件には
含めず、将来タスクとして追跡する。

### 内容

- configurationおよびapplication stateをTOML formatで保存する。
- TOML fileをcurrent working directoryではなく、実行中のexecutable fileと同一directoryへ自動生成する。
- fileが存在しない場合はbuilt-in defaultを使用し、必要な時点で自動生成する。
- 保存対象には、少なくともOptionsで設定する値および最後に使用したfile open directoryを含める予定とする。
- RTKLIBの設定fileとのformat互換性は要件としない。

TOML file名、lightとfullでfileを共有するか、schema version、atomic write、unknown keyの
扱い、保存失敗時の詳細動作、保存タイミング、同時起動時の競合処理および
session/workspace stateの保存範囲は未確定とする。executable directoryへ書き込めない場合の
詳細なerror処理も、このphaseでは新たに決定しない。
