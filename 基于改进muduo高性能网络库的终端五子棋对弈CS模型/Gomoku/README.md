# Gomoku（自研工区）

这份 `Gomoku/` 是从 [`../../forDsh/Gomoku/`](../../forDsh/Gomoku/README.md) **原样复制**来的，
业务代码一行没改：同一份服务端/客户端，换一份 muduo 就能跑，用来验证自研 muduo 与参考实现等价。

组件的职责划分、锁边界、房间状态机与胜负判定见参考实现那份 README：
[`forDsh/Gomoku/README.md`](../../forDsh/Gomoku/README.md)。协议、客户端命令、测试方法见
[`forDsh/README.md`](../../forDsh/README.md)。

本目录只额外保证一点：`#include "muduo/..."` 的路径不能变，否则拷过来的代码编译不过。
