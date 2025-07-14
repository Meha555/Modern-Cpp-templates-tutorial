# Head-only库的书写

Head-only库大都是模板库，因此需要保证模板的定义放在头文件中。

但毕竟是库，可能会有很多的类、函数，如果所有的函数实现都放在类内，会让这个类变得庞大，难以阅读。

因此，往往采用**声明头+内联实现头**的方式来书写。

以 node-addon-api 为例：

声明头：

```cpp
// napi.h
#pragma once

namespace Napi {

template<typename T>
class Reference {
public:
    static Reference New(const T &value, std::uint32_t initial);
    T value() const;
};

}

// 文件末尾
#include "napi-inl.h"
```

内联实现头：

```cpp
// napi-inl.h
// NOTE: DO NOT INCLUDE THIS FILE. INCLUDE napi.h INSTEAD.

#include "napi.h"

namespace Napi {

// 注意类外实现都是inline的
template <typename T> inline Reference<T> Reference<T>::New(const T &value, std::uint32_t initial) {
    // ...
}

template <typename T> inline T Reference<T>::value() const {
    // ...
}

}
```

这样一来，`napi.h` 就清爽多了。