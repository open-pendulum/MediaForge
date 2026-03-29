# 背景

当前的项目是一个多媒体处理程序，目前已经实现了转码和视频播放+视频分割，但是都是简单实现的，各种流程都是面向过程实现的。但是很多能力都可以复用，例如，文件解封装，编码、解码等节点，因此我想重构一下。

# 重构思路
1. 将各个音视频处理能力封装到单独的 node，例如 demux node、video encode node、video decode node、mux node。这些 node 可以抽象出一个基类。
声明一些公共接口。将各个 node 串联起来就可以形成一个音视频处理 pipeline，这里pipeline 是个虚拟的概念，可不必从代码上体现。
2. 实现一个 media engine 或者叫 node manager，负责各个 node 的创建、销毁、链接、断开等。node manager 要维护各个 node 的声明周期，可以给
各个node 发送 action 消息用于 start stop 等，也可以发送 property 消息，用于设置一些参数。manager 也要提供一个 observer 注册到各个 node，
各个node可以基于整个 observer 给 manager 回调 node 内部消息。
3. 业务层是各自实现，例如 transcoder，其内部持有整个 node manager，通过 node manager 来创建自己需要的node，根据自己的业务逻辑
串联其这些node，也可以向 node manager 注册 observer，来接受自己需要的某些node回调信息。也可以通过 node manager 提供的接口给指定的node发 action 或 property
消息。也就说业务曾可能要感知某些 node 的 id或者 name。
4. node 之间因为已经链接起来，所以可以做数据交互，运行向上有节点 pull，也允许往下游节点 push。节点之间的数据要抽象出一个基类，然后音视频数据都可以继承这个基类，节点之间也允许发送消息，也是继承这个基类。
5. 要有几个公共的线程，例如音频一个，视频一个，各个node 根据自己的类型可以使用对应的 node。
6. node manager 支持基于一个 json 来自动创建node链路，这个是高级功能可以优先级降低
7. 可以先实现上述框架，然后用 transcoder 场景为例子对其基于上述框架进程重构。可以在代码里写死一个测试文件，具体为 test_data/泰罗奥特曼01_test.mkv，然后不和ui交互，只是一个命令行程序，方便你自己测试
8. 我已经安装了 cmake ninja 等工具，你可以自行编译和运行

