
echo_server 是作为规范例子编写的。

包含 proactor 的三大正统用法：

- 协程法

    例子是 echo_server_stackless.cpp . 使用 c++20 协程构建。是三大示例里，代码阅读起来最轻松的

- 纤程法

    例子是 echo_server_stackfull.c 。 注意是 C 语言的版本。 因为 C++ 不用协程是脱裤子放屁。故而例子使用 C 语言。
    使用的是 Windows Fiber 或者 posix 平台的 ucontext 进行上下文切换。代码阅读起来还是比较轻松的。但是 C 语言的啰嗦会带来一定的干扰。

- 回调地狱法

    例子使用的是传统的 回调模式。展示了如何将 OVERLAPPED 结构和 回调进行结合。这个结合代码以 C with class 写成。如果是 正统 C++
    写法，会利用 类型擦除器 将 任意回调和 OVERLAPPED 结构绑定。而不是使用 C 函数指针。
    但是正统 C++ 写法，何必用回调，上协程会更轻松。拒绝协程的人一般也是 C with class 程序员，因此例子使用 C with class 写法。

关于 proactor 的三大正统用法，可以参考我的博客 [这篇文章](https://microcai.org/2024/12/14/use-iocp-in-the-right-way.html) 和他的[前置文章](https://microcai.org/2024/12/13/in-deepth-async.html) 还有文章内列出的 扩展阅读。


web_server 是一个非常不规范的例子，只是当时拿的一份简易demo，从 redit 上巴拉下来的。
然后对他进行了略微的协程化改写。总算代码有略微可读性了。这个主要是用来测试 iocp4linux 的。

zhihu.cpp 这个是从 知乎 上的一个回答里巴下来的。千万不要学习这种垃圾代码。
这个完全是作为 iocp4linux 的兼容性测试而放在这里的。