# LiveTrace

送信元が毎回変わる周期的攻撃者に対する、オンライン逆探知とバースト間再同定を ns-3 上で
end-to-endにシミュレートするプロジェクト。

## 中心的な問い

生きた5秒間の追跡窓は、送信元が毎回変わる断続的攻撃通信に対して、その真の踏み台連鎖を起点まで
遡り切れるか——ランダムな中継メッシュ網の規模がノード数百まで拡大しても。本リポジトリの主要な数値は
すべて **シード間の平均±95%信頼区間、ネットワーク規模 N の関数として**報告される。
起点に到達**できなかった**ケースも、捨てずに負の観測として記録する。

オラクル(真の攻撃者ID・真の連鎖経路)は事後の評価にのみ記録され、逆探知・相関・再同定を行う
コードには一切渡していない。

**現在の主結果**(`docs/summaries/phase6.md`、N=20〜640、`config/scale_sweep.yaml`。
図付きの詳細は `docs/results.md` を参照): 追跡成功率は **N=20〜40 の 1.000 から N=640 の
0.325 [0.248, 0.413] まで**低下する、実データに基づく統計的に裏付けられた難易度曲線が得られた。
単ホップ再現率は全 N で 1.000 を維持しており、成功率の低下は相関の破綻ではなく、生きた追跡窓の
時間予算の問題であることが明確である(窓内到達段数・time-to-trace はいずれも N とともに単調に増加)。
これはメッシュ直径が N に連動して成長するようトポロジを構成し(固定エッジ確率ではなく固定平均次数)、
踏み台連鎖長をその直径に連動させたことで、N が実際に難易度を左右する軸になったためである。
これ以前のスイープ(`docs/summaries/phase4.md`)では全 N で成功率100%というフラットな結果しか
得られなかったが、これは「システムが規模に対して頑健である」ことの証拠ではなく、**実験設計上の
アーティファクト**(連鎖長が N に依存しない固定値だったこと)だったと判明している——詳しい監査と
修正内容は `docs/summaries/phase6.md` を参照。

## 実験環境(このプロジェクトで固定)

- ns-3-dev **3.48**(debugビルド、runtime asserts + logging 有効)。`~/ns-3-dev` に構築し、
  本リポジトリへは持ち込まずシンボリックリンクで再利用する(`tools/setup.sh` 参照)。
- WSL Ubuntu 24.04、g++ 13.3、cmake 3.28。
- Python 3.12、numpy 2.5、scipy 1.18、matplotlib 3.11(`analysis/`)。
- 可視化: NetAnim(定性的、確定した逆探知イベントで駆動)+ matplotlib(定量的な規模スイープ図)。
  自作の HTML/JS は使用しない。

## リポジトリ構成

```
config/default.yaml       単一actor・正準420秒周期のベースライン設定(仕様に忠実)。
                           単発実行のサンプル設定としても使用。
config/scale_sweep.yaml   主結果のスイープ設定: 2 actor・平均次数固定
                           (直径がNとともに成長)・統計的検出力のための周期圧縮。
                           詳細は docs/summaries/phase6.md を参照。
config/phase3_reid_test.yaml  再同定専用の2-actorテスト設定
                           (reid_precision を自明値にしないための構成)。
contrib/livetrace/        ns-3モジュール: トポロジ・攻撃者・中継・背景トラフィック・
                           オラクルロガー・観測ログ・相関エンジン・逆探知observer・再同定エンジン。
scratch/livetrace-sim.cc  シミュレーション本体(設定を読み込み1シードを実行)。
analysis/                 Python: ログ解析・指標算出・規模スイープハーネス・作図。
tests/                    analysis/ の統計処理に対するPython単体テスト。
tools/setup.sh            contrib/scratch を ns-3-dev へシンボリックリンクしビルド設定する。
tools/run_sweep.sh        単純な N×seed グリッドを実行する(全Nで同一seed数)。
tools/run_sweep_staged.sh 大きいNではseed数を減らす段階運用でメインスイープを実行する
                           (大きいNは1seedあたりの実行時間が大幅に長いため)。
results/                  生ログ・図(gitignore対象。下記コマンドで再現可能)。
docs/summaries/           各フェーズの設定・指標・結果・考察をまとめたMarkdown。
docs/results.md           パイプライン全体を図付きでまとめた結果レポート。
docs/figures/             上記レポートに埋め込む、コミット対象の図。
```

## 単発実行の再現方法

```bash
export NS3_DIR=~/ns-3-dev        # 手元の ns-3-dev チェックアウト先
./tools/setup.sh                  # シンボリックリンク+ビルド設定(冪等)
cd "$NS3_DIR"
./ns3 build livetrace scratch/livetrace-sim
./ns3 run "scratch/livetrace-sim --config=$OLDPWD/config/default.yaml --outdir=$OLDPWD/results --seed=1"
```

これにより `results/oracle_seed1_n50.jsonl`(真値、評価専用)、`results/observed_seed1_n50.jsonl`
(逆探知スタックが参照してよい情報のすべて)、`results/netanim_seed1_n50.xml`(NetAnimで開く)が出力される。

このモジュールの ns-3 単体テストを実行する:

```bash
cd "$NS3_DIR" && ./test.py -s livetrace
```

## 規模スイープ(主結果)の再現方法

```bash
tools/run_sweep_staged.sh config/scale_sweep.yaml results   # 約50分: N<=320は8seed、N=640は3seed
python3 analysis/evaluate_phase4.py --results-dir results \
    --n-values 20 40 80 160 320 --seeds 1 2 3 4 5 6 7 8 --out-csv /tmp/sweep_stage1.csv
python3 analysis/evaluate_phase4.py --results-dir results \
    --n-values 640 --seeds 1 2 3 --out-csv /tmp/sweep_stage2.csv
head -1 /tmp/sweep_stage1.csv > results/sweep_summary.csv
tail -n +2 /tmp/sweep_stage1.csv >> results/sweep_summary.csv
tail -n +2 /tmp/sweep_stage2.csv >> results/sweep_summary.csv
python3 analysis/plot_sweep.py --csv results/sweep_summary.csv --out-dir results
python3 analysis/plot_mechanism.py --results-dir results \
    --n-values 20 40 80 160 320 640 --out docs/figures/mechanism_vs_n.png
```

(`config/default.yaml` + `tools/run_sweep.sh` は、単一actor・正準420秒周期・固定pというよりシンプルな
スイープにも引き続き使える——これは Phase 6 の修正前、`docs/summaries/phase4.md` で使われていた構成。)

上記は `results/sweep_summary.csv` と `results/scale_sweep.png` を出力する(率指標はWilson 95%信頼区間、
連続指標はt分布95%信頼区間、4パネル、N軸は対数スケール、各点に標本数を注記)。`results/netanim_*.xml`
を NetAnim で開くと、逆探知observerが実行中に確定ホップをライブでハイライトする様子を見られる
——確定ホップは赤、victimは青。

Phase 3 の2-actor再同定テスト(単一actorでは自明値になる `reid_precision` を意味のある指標にするために必要)を再現する:

```bash
./ns3 run "scratch/livetrace-sim --config=$LIVETRACE_DIR/config/phase3_reid_test.yaml --outdir=$LIVETRACE_DIR/results --seed=1 --n=20"
```

パイプライン全体を図付きでまとめた結果レポートは **[docs/results.md](docs/results.md)** を参照。

## 設計判断とその理由

- **トポロジ: Erdős–Rényi G(n,p) をプロジェクト全体で唯一の生成モデルとして固定。** エッジ密度を
  ノード数と独立に制御できるため、N をスイープの第一級の規模軸にできる。生成は(上限付きで)
  再試行し、連結であること、かつネットワークの大部分を切り離してしまう単一の関節点(articulation
  point)が存在しないことを保証する——これは「全通信が通らざるを得ない隘路を置かない」という要件を、
  より強く扱いにくい k-連結性保証を要求せずに実現するもの。
- **踏み台はアプリケーション層のオーバレイ**であり、IPホップではない: 攻撃者はメッシュの中から
  k個の中継**ノード**を選び、それらを経由してUDPを中継する。オーバレイ上の任意の2ホップ間の物理経路は、
  通常のIPルーティングを介して ns-3 のポイントツーポイントリンクを複数またぐことがある。
  これは相関エンジンが実装している古典的な Zhang–Paxson の踏み台モデルに一致する。
- **フローキーはローカルに見えるアドレス:ポートの組のみから構成**し、バースト・actor・連鎖の
  識別子からは一切構成しない。そのため相関エンジンは近道ができず、中継における流入・流出フローを
  結びつけるには本物のタイミング相関を行う必要がある。
- **オラクルと観測の分離は規約ではなくアーキテクチャ上の制約**: `OracleLogger` はシミュレーション
  ドライバ内でのみ構築され、攻撃キャンペーンにのみ渡される。他のクラス(中継、背景トラフィック、
  相関エンジン、observer、再同定エンジン)にそのポインタが渡されることは一切ない。
- **`Ipv4GlobalRoutingHelper` ではなく Nix-Vector ルーティングを使用。** グローバルルーティングは
  全ノード対の経路を事前計算するためスケールしないことがよく知られており、N=160 ではセットアップだけで
  5分以上かかった。Nix-Vector はオンデマンドで経路計算するため、N=320 のスイープセル全体
  (900秒のシミュレーション全体を含めて約7.5分)を現実的な時間で実行可能にした。
- **背景トラフィックのレートは N にスケールする**(`background.rate_pps` はノードあたりの値で、
  ドライバ内で N 倍される)。これにより、ノードあたりの環境ノイズ密度——つまり相関エンジンの候補
  プールのサイズ——がスイープ全体を通じて同程度に保たれる。同じ固定の集計レートがノード数の増加とともに
  希薄化してしまうことを防ぐ。
- **トポロジは固定エッジ確率ではなく固定平均次数を使用**(`topology.avg_degree` から
  `p = avg_degree/(n-1)` を導出)。固定 p ではメッシュは N が増えるほど相対的に密になり、
  直径はほぼ一定(N=320 でも2〜3ホップ)に留まる。固定次数にすることで直径が N とともに
  (~log N で)成長するようになり、踏み台連鎖長(`attacker.chain_length_factor * diameter`)は
  この直径に連動している——現在の主結果を支える最も本質的な修正であり、詳細は
  `docs/summaries/phase6.md` を参照。
- **率指標(再現率・適合率・追跡成功率)は、シードごとの率の平均ではなく、全試行をプールした
  Wilson二項信頼区間で報告する。** 後者は、全シードがたまたま0%または100%になった場合に
  誤解を招くほど狭い(しばしば幅ゼロの)区間に潰れてしまう——実際、初期の不具合のあるスイープで
  まさにこれが起きていた。オンラインシステムが一致するトレースを一つも生成しなかったバーストは、
  分母から静かに除外されるのではなく、失敗として計上する。

各フェーズのパラメータ・指標・結果は `docs/summaries/` を参照。特に `phase6.md` は、
初期の Phase 4 スイープが不自然な100%フラット結果を出したことを受けて実施した、
上記の問題点全般の監査記録である。図付きの統合結果は **[docs/results.md](docs/results.md)** を参照。
