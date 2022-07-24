
# QuaStation-Kernel

KDDI テクノロジーが [GPL/LGPL に基づき公開している](https://kddi-tech.com/kts31/list/opensource.html) BSP (Board Support Package) 版 Linux カーネル ([ミラー](https://github.com/Haruroid/linux-kernel-kts31)) をベースに、Qua Station 向けの様々な改良を行った Linux カーネルです。

Linux カーネルのバージョンは 4.1.17 ですが、mainline のカーネルと比較すると、Realtek SoC 向けの大幅なカスタマイズ（ Realtek SoC 固有のドライバやデバイスツリーの追加など）が行われています。  

> 基本的には [QuaStation-Kernel-BPi](https://github.com/tsukumijima/QuaStation-Kernel-BPi) (Linux 4.9.119 ベース) の方を使うことをおすすめします。  
> Linux 4.1.17 は古いですし、4.9.119 ベースのカーネルの方ができることが多いです。

## 既知の問題

- **2つある PCIe 接続の Wi-FI IC (RTL8192EE: 2.4GHz, RTL8812AE: 5GHz) のうち、RTL8812AE が接続されている方の PCIe バス（PCIE2）が認識できず、起動中に `pci_bus 0001:01: pcie2 dllp error occur = 0xdeadbeef` のような大量のエラーログが出力される。**
  - Qua Station の eMMC に書き込まれている純正のカーネルでは問題なく PCIE2 を認識できているため、KDDI テクノロジーから配布された Linux カーネルに何らかのコーディングミスがある可能性が高いです。
  - そもそもデバイスツリーが eMMC から抽出した実機の DTB のデコンパイル結果と一致しておらず、（完成品に書き込まれているカーネルよりも前の）開発段階のカーネルのソースコードが配布されているのだろうと推測しています。
  - 原因になりうる箇所を探したが、今のところ原因は特定できていません。なお、[QuaStation-Kernel-BPi](https://github.com/tsukumijima/QuaStation-Kernel-BPi) (Linux 4.9.119 ベース) では正常に PCIE2 と RTL8812AE を認識できています。
- **シャットダウンすると、一見完全に電源が切れたように見えるものの、数十秒後にカーネルパニックが発生する。**
  - シャットダウンプロセスは完了したものの、ボードの電源を切れていないことが考えられます。
  - Android 実機の Qua Station では、eMMC 内の U-Boot の環境変数領域にある PowerStatus フラグを off に書き換えた後に「再起動」する挙動になっているあたり、Qua Station はボード自体の電源オフには対応していないのかもしれません。
  - Qua Station の eMMC に内蔵されている U-Boot は Qua Station 向けにカスタマイズされており、環境変数の PowerStatus フラグ (環境変数は `env print` で確認できる) が off になっている場合はブート処理を行わず、PowerStatus が on になるまで待機するようになっています。
  - [QuaStation-Ubuntu](https://github.com/tsukumijima/QuaStation-Ubuntu) で生成できる Ubuntu の rootfs では、起動時に U-Boot の環境変数データが格納されている eMMC 上のアドレスを割り出して `/etc/fw_env.config` に書き出すように調整しています。
    - POWER ボタンを3秒以上長押しすると、[fw_setenv](https://manpages.ubuntu.com/manpages/focal/man8/fw_setenv.8.html) コマンドで PowerStatus フラグを off に書き換えてから再起動されるように設定しています。
    - こうすることで、実機同様に「シャットダウン」することができるようになりました（なお、この 4.1.17 カーネルで動作するかは検証していません）。

## カーネルのビルド

事前に Docker が使える環境であることが前提です。

> これらのコード群は GCC 9 系以下向けに書かれているため、GCC 10 以降がインストールされている環境では一部のビルドに失敗します。  
> こうした環境依存の問題を回避するため、Docker コンテナ上でビルドを行っています。  
> なお、Docker コンテナには GCC 4.9 系がインストールされています。

```bash
git clone https://github.com/tsukumijima/QuaStation-Kernel.git
cd QuaStation-Kernel
```

QuaStation-Kernel を clone します。

```bash
make docker-image
```

事前に、ビルドで使う Docker イメージをビルドしておきます。

```bash
make build
```

あとは `make build` を実行するだけで、全自動でカーネルのビルドが行われます。  

PC のスペックにもよりますが、カーネルのビルドには少し時間がかかります。  
`Kernel build is completed.` と表示されたら完了です！

`make config` で、カーネル構成 (.config) を調整できます。  
既定で Qua Station 向けに最適化された defconfig が適用されています。基本的に調整する必要はありません。

`make clean` で、ビルドされた成果物（ビルドキャッシュなど）をすべて削除できます。  
最初からすべてビルドし直したい際などに実行します。

## 成果物

ビルドが終わると、`usbflash/` ディレクトリ以下に

- Linux カーネル (`bootfs/uImage`)
- Device Tree Blob (`bootfs/QuaStation.dtb`)
- カーネルモジュール (`rootfs/usr/lib/modules/4.1.17-quastation/`)
- カーネルヘッダー (`rootfs/usr/src/linux-headers-4.1.17-quastation/`)

がそれぞれ生成されています。  

`usbflash/rootfs/` 以下には、他にも Bluetooth のファームウェア (rtlbt) や OpenMAX ライブラリなどのビルド済みバイナリが配置されています。  
各種 Linux ディストリビューションと一緒に USB メモリにコピーしてください。

その後、適切に U-Boot のコマンドを実行すれば、Qua Station 上で Linux が起動できるはずです。
