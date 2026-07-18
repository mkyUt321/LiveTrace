# Phase 0 サマリ

- **日付**: 2026-07-18
- **目的**: LiveTrace の土台構築。ランダム中継メッシュ・移動起点の周期的攻撃者・踏み台連鎖・正常背景トラフィック・
  オラクルロガー(真値分離)・NetAnim 出力を実装し、相関 OFF の状態で健全性を確認する。

## 環境/バージョン
- ns-3-dev 3.48(debug, runtime asserts/logging ON)、既存チェックアウトを再利用(`~/ns-3-dev`)し、
  `contrib/livetrace` と `scratch/livetrace-sim.cc` をこのリポジトリからシンボリックリンク。
- WSL Ubuntu 24.04 / g++ 13.3.0 / cmake 3.28.3 / Python 3.12.3(numpy 2.5.1, scipy 1.18.0, matplotlib 3.11.0)。
- GitHub リポジトリ: https://github.com/mkyUt321/LiveTrace(手動作成、当方から push)。

## トポロジ設定
- **モデル: Erdős–Rényi G(n,p)** に1つに固定。理由: 密度 p をノード数 n と独立に制御でき、
  N を第一級の実験軸にしやすい。生成手順は `RandomMeshTopology`:
  1. G(n,p) を生成し連結性を BFS で確認(非連結なら再抽選)。
  2. Tarjan 法で articulation point を検出し、各カット点を除去した際の「2番目に大きい成分」のサイズが
     `0.15 * n` を超える場合は「隘路」とみなして再抽選(最大 `max_regen_attempts` 回、既定 50 回)。
     小さな末端成分(ペンダント)は許容 — 全トラフィックが通る隘路ではないため。
  3. 既定パラメータ: n=50, p=0.12(config/default.yaml で変更可)。
- 全ノードに ns-3 InternetStack をインストールし、辺ごとに Point-to-Point リンク(5Mbps, 2ms)+ /30 サブネットを
  割当て、`Ipv4GlobalRoutingHelper::PopulateRoutingTables()` で多ホップ経路を解決。

## 攻撃者・踏み台・背景トラフィック
- **攻撃者(単一 actor)**: 周期 420 秒・バースト 5 秒(既定値、config で変更可)。毎周期、
  ランダムな起点ノードとランダムな中継連鎖(長さ 2〜5)を新規に選択し、`origin→relay1→…→relayk→victim` の
  アプリケーション層オーバレイを構築。中継は StepstoneRelayApp を都度動的インストールし、受信パケットを
  小さな遅延(1〜15ms)後にそのまま転送(ON/OFF タイミング構造を保存)。
- **踏み台連鎖はオーバレイ**(物理 IP ホップとは別レイヤ)。中継間の物理経路はグローバルルーティングによる
  複数 IP ホップを取りうる。これは古典的な Zhang–Paxson の踏み台モデルに対応。
- **背景トラフィック**: ポアソン到着(既定 4 pps)でランダムなノード対が 1〜5 パケットの短いセッションを交換。
  真値は一切持たず、観測ログにのみ現れるノイズとして機能。
- **フローキー**はローカルに見える `srcIP:port->dstIP:port` のみで構成し、バースト/actor/連鎖の識別子を
  一切埋め込まない(相関エンジンに近道を与えないための設計)。

## オラクル分離(不変条件)
- `OracleLogger` はシミュレーションドライバ内でのみ生成し、`AttackerCampaign` にのみ渡す。
  `StepstoneRelayApp`・`BackgroundTraffic`・`ObservationLog` はオラクルへの参照を一切持たない
  (クラス設計上、コンストラクタにポインタを渡していないため物理的に不可能)。
- 真値は `results/oracle_*.jsonl` に burst 単位で記録: `burst_id, true_actor_id, start_time_s, true_chain`(ノードID配列、起点が先頭・victim が末尾)。

## 遭遇した不具合と修正
- **StartTime/StopTime のバグ(重要)**: ns-3 の `Application::DoInitialize()` は
  `Simulator::Schedule(m_startTime, ...)` を「Initialize() 実行時点からの相対遅延」として扱う。
  `Node::AddApplication()` はミッドシミュレーション追加時も Initialize を "+0" でスケジュールするため、
  シミュレーション開始前に設置するアプリでは `SetStartTime(Seconds(x))` が絶対時刻と一致し問題にならないが、
  攻撃キャンペーンのように **実行中に** `AddApplication` する場合、`SetStartTime(Simulator::Now())` は
  「今からさらに Now() 秒後」に開始してしまうバグを生む。`SetStartTime(Seconds(0.0))` /
  `SetStopTime(Seconds(duration))` (相対値のみ)に修正して解決。小 N (n=12) のサニティランで
  origin→relay→relay→relay→victim の全ホップに観測イベントが記録されることを確認済み。

## 健全性確認(相関 OFF)
- 小 N (n=12, p=0.3, 周期 40 秒・バースト 5 秒に短縮した試験設定) で 90 秒シミュレーションを実行。
- `oracle_seed1_n12.jsonl`: 2 バースト、周期通り t≈10.44s, 50.44s に発火、真連鎖(例: `[2,5,6,3,0]`)を記録。
- `observed_seed1_n12.jsonl`: 1000+ イベント。攻撃バーストの全ホップ(origin送信→各中継のrx/tx→victim着信ポート9999への到達)
  および背景トラフィック(ポート8000)の両方が記録され、オラクル情報(burst_id/actor_id)は一切含まれない。
- NetAnim 用 XML (`netanim_*.xml`) を出力(ノード配置は円環レイアウト)。
- ns-3 単体テスト `./test.py`/test-runner `--suite=livetrace`: 3件全て PASS
  (トポロジ連結性、config パーサ、ObservationLog の時間窓クエリ)。

## 考察
- Erdős–Rényi + articulation point チェックは小規模では実用上ほぼ即座に条件を満たす(n=12, p=0.3 で
  1回の抽選で連結・隘路なしを達成)。大規模 N でのリトライ頻度は Phase 4 のスイープで観察する。
- Application の Start/Stop 相対時刻の罠は ns-3 でミッドシミュレーション動的アプリ生成を行う際の
  典型的な落とし穴であり、今後 Phase 1 以降で同様のパターン(相関確定後の追加処理等)を実装する際は
  同じ注意が必要。

## 次アクション
- Phase 1: Zhang–Paxson 型 ON/OFF タイミング相関で単一ホップの上流を確定する相関エンジンを実装。
- config/default.yaml の規定値(n=50, p=0.12, 周期420s/バースト5s)でのフル規模健全性確認は
  Phase 1 の相関エンジンと合わせて再実施予定(観測ログの量が増えるため、相関実装と一緒に検証するのが効率的)。
