# Data Specification

## 1. 目的と対象範囲

本書は、`plotcore light`および`plotcore full`が共通して使用するデータ仕様を定義する。

対応ファイルを読み込み、入力形式に依存しない位置sampleへ正規化し、通常表示、基準相対表示および統計処理へ渡すまでの規則は、applicationのwindow layoutに依存しない。

本書では、以下を定義する。

- POSおよびNMEA入力の構文
- 時刻、位置および測位解品質の正規化
- 不正record、時刻逆行および重複epochの処理
- 共通ENU座標系
- 共通処理時刻範囲
- 基準epochとの対応付け
- 相対位置および距離
- ファイルrateの推定
- ステータス統計
- diagnosticに保持する情報

tab、floating area、window lifecycle、button配置その他のapplication固有UIは本書の対象外とし、`requirements.md`で定義する。

具体的なC++型、メモリ配置、探索アルゴリズム、並列化およびapplication target間の共有境界は`architecture.md`で定義する。

## 2. 用語

- **record**: 入力ファイル上の1件の記録。
- **position record**: POSのデータ行またはGGAのように、位置sampleを生成することを意図したrecord。
- **sample**: 解析および正規化に成功し、内部データとして保持される位置データ。
- **epoch**: sampleの時刻。複数の入力recordが同一epochと判定されることがある。
- **slot**: 読み込み済みファイルへ割り当てる`1`から`n`までの位置。
- **reference sample**: スロット1に属するsample。
- **comparison sample**: スロット2以降に属するsample。
- **matched pair**: 基準相対表示で対応付けられたreference sampleとcomparison sampleの組。
- **common time range**: 全ファイルの後段処理へ共通して適用する閉区間。
- **estimated Hz**: ファイルのsampleから推定したrate。
- **override Hz**: 利用者が手動指定したrate。
- **effective Hz**: override Hzが存在する場合はその値、存在しない場合はestimated Hz。

## 3. 正規化sample

各sampleは、概念上、少なくとも以下を保持する。

```text
time_gpst_ns
latitude_rad
longitude_rad
ellipsoidal_height_m
ecef_x_m
ecef_y_m
ecef_z_m
enu_e_m
enu_n_m
enu_u_m
quality
source_line_number
continuous_from_previous
```

`time_gpst_ns`はGPS epochからの整数nanosecondとする。

latitude、longitudeおよびellipsoidal heightはWGS 84のLLHとし、latitudeおよびlongitudeをradian、ellipsoidal heightをmeterで保持する。

ECEFを正規化後の基本計算表現とする。LLHおよびECEFは読み込み後に変更しない。ENUは選択された共通ENU基準位置に対して計算し、保持する。

`source_line_number`は、sampleの生成元となった入力ファイル上の1始まりの物理行番号とする。

- POSでは、採用したPOS data recordの行番号とする。
- NMEAでは、採用したGGAの行番号とする。
- duplicate epochまたはtalker優先順位によってrecordを置換した場合は、最終的に採用したrecordの行番号とする。

`source_line_number`はsample番号またはepoch番号ではない。

`quality`は`0`から`6`までの整数とする。

`continuous_from_previous`は、そのsampleが正規化後の直前sampleと入力上連続して得られたかを表す。初期実装では内部データに保持するだけとし、描画および統計には使用しない。

## 4. POS入力

### 4.1 行の分割

- 行頭の空白を除いた後、`%`から始まる行は読み飛ばす。
- headerの内容は解釈しない。
- fieldは1個以上のASCII spaceまたはtabで分割する。
- Qより後ろに存在するfieldは無視する。
- 空行は読み飛ばす。

### 4.2 field順序

fieldは左から以下の順とする。

```text
time fields, latitude, longitude, ellipsoidal height, Q, optional fields...
```

時刻を解析した直後の3 fieldを座標として扱い、その次のfieldをQとして扱う。

### 4.3 GPS week/TOW形式

次の形式を受理する。

```text
GPSW TOW latitude longitude ellipsoidal_height Q
```

- GPSWは小数点を含まない整数とする。
- TOWは整数または小数とする。
- GPSWおよびTOWをGPSTとして解釈する。

### 4.4 calendar形式

次の形式を受理する。

```text
YYYY/mm/dd hh:MM:SS.s latitude longitude ellipsoidal_height Q
```

- 小数秒は省略可能とし、桁数は任意とする。
- calendar時刻をGPSTとして直接解釈する。
- UTCからGPSTへの変換は行わない。

### 4.5 座標

- latitudeおよびlongitudeはdegreeとする。
- heightはmeter単位の楕円体高とする。
- 座標はWGS 84のLLHとして解釈し、ECEFへ変換する。
- 数値として解析できることだけを確認し、緯度、経度および高さの値域検査は行わない。
- 3座標fieldのいずれかを解析できないrecordは除外する。

### 4.6 品質

- Qは整数として解析する。
- `1`から`6`はそのまま正規化品質として使用する。
- その他の整数値は警告したうえで品質`0`として使用する。
- Qを整数として解析できないrecordは除外する。

## 5. NMEA入力

### 5.1 行の基本検査

NMEA行は以下を満たす場合に構文解析を行う。

- ASCII文字列である。
- `$`から始まる。
- `*HH`形式のchecksumを持つ。
- `$`と`*`の間のbyte XORがchecksumと一致する。

基本検査に失敗した行は読み飛ばし、diagnosticへ記録する。

### 5.2 対象sentenceおよびtalker

処理対象sentenceは以下とする。

- GGA
- RMC
- ZDA

その他のsentenceは読み飛ばし、警告しない。

GGA、RMCおよびZDAでは、talker IDとして`GP`および`GN`だけを受理する。それ以外のtalker IDは読み飛ばし、警告する。

1ファイル内に`GP`と`GN`の両方が存在する場合は警告する。

### 5.3 GGAによる位置sample

GGAから以下を取得する。

- UTC time of day
- latitude
- longitude
- altitude
- geoid separation
- fix quality

同一epochに`GNGGA`および`GPGGA`が存在する場合は、`GNGGA`を優先する。

同一talkerのGGAが同一epochに複数存在する場合は、入力上で後に現れたrecordを優先する。

latitude、longitudeまたはaltitudeのいずれかを解析できないGGAは使用しない。解析できた値の値域検査は行わない。

### 5.4 楕円体高

geoid separationを解析できる場合は、次の式で楕円体高を得る。

```text
ellipsoidal_height = altitude + geoid_separation
```

geoid separationが欠損している場合は、ファイル単位のmodal確認を行う。

- altitudeを楕円体高として読み込む
- ファイル読み込みを中止する

ジオイドモデルによる補完は行わない。

### 5.5 NMEA品質変換

GGA fix qualityを次の表で正規化する。

| GGA quality | POS quality | 意味 |
|---:|---:|---|
| 0 | 0 | invalid/unknown |
| 1 | 5 | single |
| 2 | 4 | DGPS |
| 3 | 6 | PPP |
| 4 | 1 | fixed |
| 5 | 2 | float |
| 6 | 0 | invalid/unknown |

GGA qualityが`0`または`6`でも座標を解析できる場合は、品質`0`のsampleとして保持する。

上記以外のfix qualityは警告したうえで品質`0`として扱う。

### 5.6 最初のRMCまたはZDA

ファイル上で最初に現れた有効なRMCまたはZDAを、GGAの日付確定に使用する。RMCとZDAの種類による優先順位は設けない。

最初のRMCまたはZDAより前に出現したGGAは、time of dayと位置の組として一時保持し、基準sentenceの日時から過去方向へ日付を付与する。

RMC statusが`V`である場合はinfo diagnosticを記録する。日付および時刻fieldを解析できる場合は、statusが`V`でも日付基準またはvalidationへ使用する。

### 5.7 RMC/ZDAが存在しない場合

ファイル読み込みの終了まで有効なRMCまたはZDAが存在しない場合は、日付指定dialogを表示する。

dialogには以下を設ける。

- 日付入力欄
- ファイル時刻から日付を入力欄へ反映するbutton
- `OK`

ファイル時刻には、filesystemのlast-write timeをアプリケーションのlocal timeへ変換して得た日付を使用する。

利用者が`OK`を実行した日付をGGAの日付基準として使用する。

### 5.8 日付の遡及付与

最初のRMC/ZDAまたは利用者指定日付より前のGGAは、後ろから前へ走査する。

後側のtime of dayを`next_tod`、現在のGGAを`current_tod`、日跨ぎ許容値を`rollover_tolerance`とする。

```text
current_tod - next_tod >= 86400 s - rollover_tolerance
```

を満たす場合は、現在のGGAの日付を後側の日付より1日前とする。

`rollover_tolerance`の既定値は300 sとする。

### 5.9 GGAによる日付更新

最初の日付確定後は、RMCまたはZDAによって日付を更新しない。GGAのtime of dayだけを使用する。

直前GGAのtime of dayを`previous_tod`、現在を`current_tod`とする。

```text
raw_delta = current_tod - previous_tod
```

次を満たす場合は日跨ぎとして日付を1日進める。

```text
raw_delta <= -(86400 s - rollover_tolerance)
```

日跨ぎ時の実時間隔は次とする。

```text
adjusted_delta = raw_delta + 86400 s
```

日跨ぎでない場合は次とする。

```text
adjusted_delta = raw_delta
```

### 5.10 GGA時刻jump

GGA時刻jumpの許容値は10 epoch分とする。

effective Hzを`f`とすると、許容時間は次とする。

```text
jump_tolerance = 10 / f
```

日付付与およびHz推定後、次を満たす隣接GGAを警告する。

```text
abs(adjusted_delta) > jump_tolerance
```

effective Hzを得られない場合はjump検査を実行せず、そのことをdiagnosticへ記録する。

### 5.11 2件目以降のRMC/ZDA

2件目以降の有効なRMCまたはZDAは日時validationだけに使用し、確定済みの日付を更新しない。

validationでは、sentenceのUTC日時に最も近いGGA由来UTC日時を比較対象とし、日付と時刻の両方を考慮する。

日時差の絶対値がvalidation toleranceを超える場合はpopup警告を表示する。

validation toleranceは設定可能とし、既定値は300 sとする。

### 5.12 UTCからGPSTへの変換

NMEAのGGA、RMCおよびZDAはUTCとして解釈する。

UTC civil timeは、閏秒時の`23:59:59`、`23:59:60`および翌日の`00:00:00`を区別できる一時表現で解析する。

UTCからGPSTへの変換には、コード内に保持する閏秒tableを使用する。固定offsetだけによる変換は行わない。

## 6. 内部時刻

### 6.1 表現

内部時刻はGPS epochからの整数nanosecondとする。

概念上は次の型に相当する。

```cpp
struct GpsTime {
    std::int64_t nanoseconds_since_gps_epoch;
};
```

### 6.2 小数秒の丸め

入力小数秒はnanosecond単位へroundする。

小数秒が非負であることを前提とし、nanosecond未満は次の規則で最も近いnanosecondへ丸める。

```text
rounded_ns = floor(exact_fractional_seconds * 1e9 + 0.5)
```

`rounded_ns`が`1e9`となった場合は、整数秒へcarryする。

### 6.3 同一epoch判定

同一epoch判定のtoleranceは設定可能とし、既定値を5 msとする。

2時刻の絶対差がtolerance以下の場合は、同一epochと判定する。

用途の異なる以下の設定は個別に保持する。

- `duplicate_epoch_tolerance`
- `reference_match_tolerance`
- `rate_min_interval`
- `nmea_datetime_validation_tolerance`

`reference_match_tolerance`の既定値は本版では未確定とする。

## 7. recordの検証と正規化

### 7.1 部分読み込み

一部recordの解析に失敗しても、解析に成功したsampleの読み込みを継続する。

有効sampleが1件も存在しない場合はファイル全体の読み込み失敗とする。

### 7.2 時刻逆行

正規化後の直前sampleより、`duplicate_epoch_tolerance`を超えて過去となるsampleは除外する。

時刻逆行を警告し、次に保持するsampleの`continuous_from_previous`を`false`とする。

入力sample列を時刻順へ自動sortしない。

### 7.3 重複epoch

正規化後の直前sampleとの絶対時刻差が`duplicate_epoch_tolerance`以下の場合は重複epochとする。

重複epochでは、入力上で後に現れたsampleを残す。残したsampleの時刻および座標をそのまま使用し、`continuous_from_previous`を`false`とする。

### 7.4 continuity

各ファイルの先頭sampleは`continuous_from_previous = false`とする。

以下に該当するposition recordの除外または置換が存在した場合は、境界となる保持sampleを`false`とする。

- 構文解析失敗
- 必須field欠損
- checksum不一致
- 時刻逆行
- 重複epoch
- talker優先によるGGA除外
- 非対応talkerのGGA除外
- その他、位置sampleを生成することを意図したrecordの除外

RMC、ZDAおよび未対応の非位置sentenceはcontinuityを切らない。

重複epochまたはtalker優先によってsampleを置換した場合は、残したsampleだけを`false`とし、その次のsampleまで連鎖して`false`にしない。

初期実装ではcontinuityを描画および統計に使用しない。

## 8. 座標系

### 8.1 基本座標

- 楕円体はWGS 84とする。
- 入力LLHをECEFへ変換し、ECEFを正規化後の基本位置表現とする。
- 内部距離単位はmeterとする。
- ジオイドモデルは使用しない。

### 8.2 ENU基準方式

共通ENU基準位置は、以下から選択する。

- `Slot1Start`: 共通時刻範囲内にあるスロット1の先頭sample
- `Slot1End`: 共通時刻範囲内にあるスロット1の末尾sample
- `Slot1EcefAverage`: 共通時刻範囲内にあるスロット1のECEF座標算術平均
- `UserSpecified`: 利用者指定位置

品質`0`のsampleも算出対象へ含める。

`Slot1EcefAverage`では、ECEF平均をENUの平行移動原点とし、そのECEFをLLHへ変換して得た緯度および経度をENU回転に使用する。

### 8.3 利用者指定位置

利用者指定位置はLLHまたはECEFで受け付ける。

LLH指定ではlatitudeおよびlongitudeをdegree、ellipsoidal heightをmeterとする。

ECEF指定ではx、yおよびzをmeterとする。

### 8.4 再計算

以下の変更時は共通ENU基準位置を再決定し、全読み込み済みsampleのENUを再計算する。

- スロット1の変更
- 共通時刻範囲の変更
- ENU基準方式の変更
- 利用者指定位置の変更

共通時刻範囲は基準位置の算出対象を選ぶために使用する。決定した基準位置によるENUは、共通時刻範囲外を含む全sampleについて保持してよい。

スロット1に共通時刻範囲内のsampleが存在せず基準位置を決定できない場合は警告する。直前の有効な基準位置が存在する場合はそれを維持し、存在しない場合はENUを利用不能とする。

## 9. 測位解品質

正規化品質の意味は以下とする。

| Quality | Meaning |
|---:|---|
| 0 | invalid/unknown |
| 1 | fixed |
| 2 | float |
| 3 | SBAS |
| 4 | DGPS |
| 5 | single |
| 6 | PPP |

POSおよびNMEAからの変換後は、すべてこの値を使用する。

品質分類間の「良品質から低品質」の厳密な順序、およびRTKPLOT File 1に準拠する既定色は、RTKPLOT実装確認後に確定する。

## 10. 共通処理時刻範囲

### 10.1 union

読み込み済み全ファイルのunionを次で定義する。

```text
union_start = min(file_start)
union_end   = max(file_end)
```

### 10.2 有効境界と実効時刻範囲

startおよびendは個別に有効状態を持つ。

```text
effective_start =
    entered_start, if start_enabled
    union_start,   otherwise

effective_end =
    entered_end, if end_enabled
    union_end,    otherwise
```

実効時刻範囲は次の閉区間とする。

```text
[effective_start, effective_end]
```

sampleは次を満たす場合に範囲内とする。

```text
effective_start <= sample.time <= effective_end
```

`start_enabled`および`end_enabled`の初期値はともに`false`とする。したがって初期の実効時刻範囲はunionとなる。

境界を無効にしても入力欄の値は保持し、実効境界の計算にだけunion境界を使用する。

### 10.3 intersection

intersectionは次とする。

```text
intersection_start = max(file_start)
intersection_end   = min(file_end)
```

`intersection_start > intersection_end`の場合はintersectionなしとする。`Intersection`操作では現在の入力値および有効状態を変更せず、そのことを通知する。

intersectionが存在する場合、`Intersection`操作は次を行う。

```text
entered_start = intersection_start
entered_end   = intersection_end
start_enabled = true
end_enabled   = true
```

dialog内の変更は`OK`で適用する。

### 10.4 後段処理

以下は共通時刻範囲内のsampleだけを対象とする。

- 通常表示
- 基準epochとの対応付け
- 基準相対表示
- 品質別epoch数および割合
- 表示用の開始・終了時刻

Hz推定は共通時刻範囲を適用する前のファイル全体を対象とする。

## 11. 基準epochとの対応付けと相対位置

### 11.1 対応する基準epoch

comparison sampleの時刻を`t_cmp`とする。

対応するreference sampleは、スロット1の共通時刻範囲内sampleのうち、次を満たす時刻が最大のsampleとする。

```text
t_ref = max({t | t <= t_cmp})
```

comparison sampleより未来のreference sampleは使用しない。

同一reference sampleを複数のcomparison sampleへ対応付けてよい。

`t_cmp`以前のreference sampleが存在しない場合はmatched pairを生成しない。

### 11.2 tolerance

tolerance checkは設定により有効または無効へ切り替えられる。

tolerance checkが有効な場合は次の時刻差を使用する。

```text
dt = t_cmp - t_ref
```

```text
dt > reference_match_tolerance
```

を満たす場合はmatched pairを生成しない。

tolerance checkが無効な場合は`dt`を検査しない。`t_cmp`以前のreference sampleが存在する限り、その最新sampleを使用する。

このため、comparison sampleがreference fileの最終epochより後にある場合も、その最終reference sampleを継続して使用する。

相対表示データの時刻には`t_cmp`を使用する。

### 11.3 相対成分

matched pairについて、共通ENU上の成分差を次で定義する。

```text
delta_e = comparison_e - reference_e
delta_n = comparison_n - reference_n
delta_u = comparison_u - reference_u
```

鉛直成分として楕円体高を選択した場合は次を使用する。

```text
delta_h = comparison_ellipsoidal_height
        - reference_ellipsoidal_height
```

relative sampleの品質にはcomparison sampleの品質を使用する。品質filterおよび
品質別描画ではreference sampleではなくこのcomparison品質を参照する。

### 11.4 基準相対距離

三次元基準相対距離はECEF差のEuclidean normとする。

```text
delta_ecef = comparison_ecef - reference_ecef
reference_relative_distance_3d = norm(delta_ecef)
```

これは各matched pairにおけるcomparison sampleとreference sampleの空間距離である。

連続するcomparison sample間の移動距離、軌跡に沿った道のりまたは累積距離ではない。

基準相対距離時系列の時刻にはcomparison sampleの`t_cmp`を使用する。

## 12. ファイルrate

### 12.1 推定対象

各ファイルは一定rateとみなす。

推定には、共通時刻範囲を適用する前の正規化済みsample列を使用する。

### 12.2 推定式

隣接sampleの正の時刻差`dt`のうち、`rate_min_interval`より大きい値の最小値を使用する。

```text
minimum_interval = min({dt | dt > rate_min_interval})
estimated_hz = 1 / minimum_interval
```

`rate_min_interval`は設定可能とし、既定値を5 msとする。

候補intervalが存在しない場合、またはsample数が2件未満の場合はestimated Hzを不明とする。

### 12.3 手動override

内部ではestimated Hzとoverride Hzを分離して保持する。

```text
effective_hz =
    override_hz,  if override_hz exists
    estimated_hz, otherwise
```

override Hzは正の有限値だけを受理する。

利用者はoverride Hzを解除してestimated Hzへ戻すことができる。

## 13. ステータス統計

### 13.1 共通値

品質別countは、共通時刻範囲内に実際に保持されているsampleを品質ごとに数える。

表示するファイル開始・終了時刻は、共通時刻範囲内に存在する最初および最後のsample時刻とする。

### 13.2 Recorded mode

Recorded modeの母集団は、共通時刻範囲内に実際に保持されている全sample数とする。

```text
recorded_denominator = sum(quality_count)
```

各品質の割合は次とする。

```text
quality_percentage = quality_count / recorded_denominator * 100
```

### 13.3 Expected mode

Expected modeでは、共通時刻範囲のstartおよびendとeffective Hzを使用する。

```text
duration_seconds = (end - start) / 1e9
expected_count =
    floor(duration_seconds * effective_hz + 0.5) + 1
```

これは非負値に対する最も近い整数へのroundとする。effective Hzが小数の場合も同じ式を使用する。

- startおよびendの両端を含む。
- ファイル自身の開始・終了時刻では切り詰めない。
- 共通startがファイル固有のepoch grid上に一致するかを考慮しない。
- `end == start`の場合は`expected_count = 1`とする。
- `end < start`は無効な時刻範囲とする。
- effective Hzを得られない場合は、そのファイルのExpected modeを算出不能とする。

各品質の割合は次とする。

```text
quality_percentage = quality_count / expected_count * 100
```

割合を100%へ正規化またはclampしない。品質別割合の合計は100%未満または100%を超える場合がある。

## 14. metadata

初期実装でファイルtooltipへ提供するmetadataはファイル名だけとする。

内部処理に必要な以下の値は、tooltip用metadataとは別に保持する。

- source path
- input format
- sample count
- diagnostic count
- estimated Hz
- override Hz
- first and last sample time

## 15. diagnostic

各diagnosticは、少なくとも以下を保持する。

```text
severity
code
file_name
source_line_number
time_optional
message
action
```

severityは以下とする。

- `Info`
- `Warning`
- `RequiresDecision`
- `Fatal`

actionは、例えば以下を表す。

- `Ignored`
- `SampleRemoved`
- `SampleReplaced`
- `LoadedAsQualityZero`
- `UserDecisionRequired`
- `LoadedAltitudeAsEllipsoidalHeight`
- `FileRejected`

少なくとも以下を区別できるcodeを設ける。

- parse error
- checksum error
- missing field
- invalid time
- time reversal
- duplicate epoch
- unsupported talker
- multiple talkers
- missing geoid separation
- missing date
- date validation mismatch
- unknown POS quality
- unknown NMEA quality
- void RMC status
- GGA time jump
- rate estimation failure
- empty ENU reference range
- no common intersection

表示文言はcodeとは分離する。

## 16. 未確定事項

以下は本版では未確定とし、実装前またはRTKPLOT実装確認後に確定する。

- `reference_match_tolerance`の既定値
- 品質分類間の厳密な描画順序
- 測位解品質ごとの既定表示色
- 軸、tick、GPST表示書式およびグリッド規則
