# SFINAE

“代换失败不是错误” (Substitution Failure Is Not An Error, SFINAE)

在**函数模板的重载决议**[^1]中会应用此规则：当模板形参在替换成显式指定的类型或推导出的类型失败时，从重载集中丢弃这个特化，*而非导致编译失败*。

此特性被用于模板元编程。

注：本章需要先看[09待决名](09待决名.md)

## 模板形参的两步代换（Two-phase substitution）

对模板形参进行两步代换（由模板实参所替代）：

- 在模板实参推导前，对显式指定的模板实参进行代换【就是特化/偏特化的参数】
- 在模板实参推导后，对隐式推导出的实参和默认实参进行替换

> ”对显式指定的模板实参进行代换“这里的显式指定，就比如 `f<int>()` 就是显式指明了。我知道你肯定有疑问：我都显式指明了，那下面还推导啥？对，如果模板函数 `f` 只有一个模板形参，而你显式指明了，的确第二次代换没用，因为根本没啥好推导的。

> 两次代换都有作用，是在于有多个模板形参，显式指定一些，又根据传入参数推导一些。

代换的实参写出时非良构[^2]（并带有必要的诊断）的任何场合，都是*代换失败*。

## 代换失败与硬错误，以及SFINAE

> https://en.cppreference.com/w/cpp/language/sfinae.html

### 几个概念

**立即上下文（*immediate context*）**：SFINAE 语境中用来判断类型代换失败是否会被当作“静默丢弃重载”处理的那部分代码。包括会形成函数模板“类型/签名”的代码：函数的参数类型、返回类型、异常/`noexcept` 规格、**以及依赖于该模板的模板参数的非类型/类型模板形参**等。

- 这里解释一下“依赖于该函数模板的模板参数的非类型/类型模板形参”：

  ```cpp
  // In class ArrayProxy<T>
  template <typename B = T, typename std::enable_if<std::is_const<B>::value, int>::type = 0>
  ArrayProxy( std::initializer_list<typename std::remove_const<T>::type> const & list );
  ```

  这里 `B` 就是依赖外层模板参数 `T` 的类型模板形参。这里 `B` 如果发生了代换错误，即 `enable_if` 没过，则属于立即上下文中的错误，不会被视作硬错误。
  
- 为啥要被叫做“立即上下文”？<u>这个术语想表达的是“仅限”当前实例化的模板的一个范围</u>。如果超出这个范围，必然引发其他模板元的实例化，就不仅仅是“当前实例化的模板了”，即有副作用了。

**代换失败**：出现在 函数类型 或其 模板形参类型 或其 `explicit` 说明符 (C++20 起) 的**立即上下文**中失败。

**硬错误（*hard error*）**：在 模板形参代换后的类型/表达式 的*求值导致副作用（例如实例化某模板、生成某隐式定义的成员函数等）*，在这些副作用中如果产生了错误，则称作硬错误。（就是立即上下文之外出现的代换失败）。

综上所述，SFINAE（Substitution Failure Is Not An Error）语境就是规定了立即上下文中的类型代换失败叫做代换失败，不会让编译器立即报错，而是继续尝试决议其他模板重载来代换。而发生在立即上下文之外的类型代换失败则称之为硬错误，会直接让编译就地失败报错。所以立即上下文就是可以使用 SFINAE 技法的语境！

>回顾刚才的示例，如果我换成下面这个，则就不是 SFINAE 了。因为 `T` 如果在 `enable_if` 中代换失败，则是硬错误（因为 `T` 是整个当前类模板 `ArrayProxy<T>` 的模板参数，会实例化 `ArrayProxy<T>` ，代换失败影响的范围超过了当前实例化的成员函数模板 `ArrayProxy` ，所以不是在立即上下文内）：
>
>```cpp
>// In class ArrayProxy<T>
>template <typename std::enable_if<std::is_const<T>::value, int>::type = 0>
>ArrayProxy( std::initializer_list<typename std::remove_const<T>::type> const & list );
>```

### SFINAE 示例

```cpp
template<typename A>
struct B { using type = typename A::type; }; // 待决名，C++20 之前必须使用 typename 消除歧义

template<
    class T,
    class U = typename T::type,              // 如果 T 没有成员 type 那么就是 SFINAE 失败（代换失败）
    class V = typename B<T>::type>           // 如果 T 没有成员 type 那么就是硬错误 不过标准保证这里不会发生硬错误，因为到 U 的默认模板实参中的代换会首先失败【SFINAE的处理是按照你写的顺序来的】
void foo(int) { std::puts("SFINAE T::type B<T>::type"); }

template<typename T>
void foo(double) { std::puts("SFINAE T"); }

int main(){
    struct C { using type = int; };

    foo<B<C>>(1);       // void foo(int)    输出: SFINAE T::type B<T>::type
    foo<B<C>>(1.1);     // void foo(double) 输出: SFINAE T
    foo<void>(1);       // void foo(double) 输出: SFINAE T
}
```

以上的示例很好的向我们展示了 SFINAE 的作用，可以影响重载决议。

`foo<B<C>>(1)`、`foo<void>(1)` 如果根据一般直觉，它们都会选择到 `void foo(int)`，然而实际却不是如此；

这是因为 `foo<void>(1);` 去尝试匹配 `void foo(int)` 的时候，模板实参类型 `void` 进行替换，就会变成：

```cpp
template<
    class void,
    class U = typename void::type,         // SFINAE 失败
    class V = typename B<void>::type>      // 不会发生硬错误，因为 U 的代换已经失败
```

**`void::type`** 这一看就是非良构[^2]，根据前面提到的：

> 代换的实参写出时非良构（并带有必要的诊断）的任何场合，都是代换失败。

所以这是一个代换失败，但是因为“*代换失败不是错误*”，只是从“*重载集中丢弃（discard）这个特化，而不会导致编译失败*”，然后就就去尝试匹配下一个版本 `void foo(double)` 了，`1` 是 int 类型，隐式转换到 double，没什么问题。

至于其中提到的*硬错误*？为啥它是硬错误？其实最开始的概念已经说了：

> 如果对代换后的类型/表达式的求值导致副作用，例如实例化某模板特化、生成某隐式定义的成员函数等，那么这些副作用中的错误都被当做硬错误。

`B<T>` 显然是对代换后的类型求值导致了副作用，实例化了模板，实例化失败自然被当做硬错误。

> 注意，你应当关注 `B<T>` 而非 `B<T>::type`，因为是直接在实例化模板 B 的时候就失败了，被当成硬错误；如果 `B<T>` 实例化成功，而没有 `::type`，则被当成**代换失败**（不过这里是不可能）。

## 示例

### 示例1

我需要写一个函数模板 `add`，想要要求传入的对象必须是支持 `operator+` 的，应该怎么写？

利用 SFINAE 我们能轻松完成这个需求。

```cpp
template<typename T>
auto add(const T& t1, const T& t2) -> decltype(t1 + t2){   // C++11 后置返回类型，在返回类型（属于立即上下文）中运用 SFINAE
    std::puts("SFINAE +");
    return t1 + t2;
}
```

我知道你一定会有疑问

> 1. 这样有啥好处吗？使用了 SFINAE 看起来还变复杂了。我就算不用这写法，如果对象没有 `operator+` 不是一样会编译错误吗？
> 2. 虽然前面说了 SFINAE 可以影响重载决议，我知道这个很有用，但是我这个函数根本没有别的重载，这样写还是有必要的吗？

这两个问题其实是一个问题，本质上就是还是不够懂 SFINAE 或者说模板：

- 如果就是简单写一个 `add` 函数模板不使用 SFINAE，那么编译器在编译的时候，会尝试模板实例化来生成函数定义，结果发现你这类型根本没有 `operator+`，于是实例化模板错误。

- 如果按照我们上面的写法使用 SFINAE，根据“*代换失败不是错误*”的规则，从重载集中丢弃这个特化 `add`，然而又没有其他的 `add` 重载，所以这里的错误是“**未找到匹配的重载函数**”。

这里的重点是**能不要实例化就不要实例化，因为模板实例化会拖慢编译时间**。

我们当前的示例的 `add` 函数模板非常的简单，虽然编译器可以用到 `operator+()` 的 `return t1 + t2` 这句报错告诉你是因为没有 `operator+` ，但是很多模板是非常复杂的，编译器实例化模板经常会产生一些完全不可读的报错；<u>如果我们使用 SFINAE，编译器就是直接告诉我：“未找到匹配的重载函数”，而根本没有进行模板实例化，那我们就看 SFINAE 中哪些 Traits 没有满足即可，通常只需要按顺序一个个排查就行</u>。

总而言之：
**即使不为了处理重载，使用 SFINAE 约束函数模板的传入类型，也是有很大好处的：报错、编译速度**。

但是令人诟病的是 SFINAE 的写法在很多时候非常麻烦，目前各位可能还是没有感觉，后面的需求，写出的示例，慢慢的你就会感觉到了。这些问题会在下一章的[约束与概念](/md/第一部分-基础知识/11约束与概念.md)解决。

### 示例2

我现在想要检测一个类中是否有 `init` 成员函数：

```cpp
// (1) 错误版本
typedef struct invalid_t {} invalid_t;
template<typename T>
struct HasInit {
    static auto check(int) -> decltype(std::declval<T>().init(nullptr)); // 编译报错：B没有init函数
    static invalid_t check(...);
    static constexpr bool value = !std::is_same_v<decltype(check(int{0})), invalid_t>;
};

struct B {};
int main() {
    HasInit<B>::value; // 实例化失败
}
```

这个版本，`check(int)` 和 `check()` 不是函数模板，从而在选择 `HasInit::check(int)` 后发现 `B` 没有 `init` 函数后由于不是函数模板根本就没有实参代换，无法发生SFINAE，于是直接硬错误。

```cpp
// (2) 正确版本
template<typename T>
struct HasInit {
    template<typename U>
    static auto check(int) -> decltype(std::declval<U>().init(nullptr));
    template<typename U>
    static invalid_t check(...);
    static constexpr bool value = !std::is_same_v<decltype(check<T>(int{0})), invalid_t>;
};
```

这个版本，`check(int)` 和 `check()` 都是函数模板，名字查找时发现 `B` 确实没有 `init` 函数后发生SFINAE，选择到 `check()` 。

**注意**：上例中 `decltype` 中的 `check(int)` 和 `check<T>()` 都不是依赖名，因为在当前类中能直接看到形参匹配的 `check` 符号，所以名字查找在模板定义阶段就完成了。何况对于(1)来说 `check(int)` 连模板都不是。

### 示例3

```cpp
// (1) 错误版本
template<typename T>
class Singleton {
    //...
    template <typename...Args>
static typename std::enable_if<HasInit<T>::value, T&>::type Init(Args&&... args) {}
    //...
};
struct C : public Singleton<C> {
    void init(void *p) { std::cout << "C::init()" << std::endl; }
};
int main() {
    // 我都不需要创建C对象，也不需要使用C::Init就能报错，因为C的继承列表中实例化了Singleton<C>
}
/*编译期报错：
/home/insights/insights.cpp:25:36: error: failed requirement 'HasInit<C>::value'; 'enable_if' cannot be used to disable this declaration
   25 |     static typename std::enable_if<HasInit<T>::value, T&>::type Init(Args&&... args) // Failed requirement 'HasInit<C>::value'; 'enable_if' cannot be used to disable this declaration
      |                                    ^~~~~~~~~~~~~~~~~
/home/insights/insights.cpp:43:19: note: in instantiation of template class 'Singleton<C>' requested here
   43 | struct C : public Singleton<C> {
      |                   ^
*/
```

为啥 `HasInit<T>::value` 结果是false？因为这里 `HasInit<T>` 不是当前实例化的成员，当前报错是在 `struct C : public Singleton<C>` 这里实例化 `Singleton<C>` ，而 `Init` 函数中的 `HasInit<T>::value` 根据 `T` 已经能代换进去算出值了，不过这里按道理不应该出现模板替换失败，C确实有init函数，因此 `std::enable_if_t<HasInit<C>::value, T&>` 就是 `std::enable_if_t<true, T&>` ，Copilot说这个是bug😵。

```cpp
// (2) 正确版本
template<typename T>
class Singleton {
    //...
    template <typename...Args, typename U = T>
static typename std::enable_if_t<HasInit<U>::value, T&> Init(Args&&... args) {}
    //...
};
```

这里由于是 `HasInit<U>::value` 了，实例化 `Singleton` 时 `U` 未知，所以需要调用 `Init` 函数时才知道 `U` ，发生模板形参代换，从而能利用上SFINAE。

总之就是要确保你想使用SFINAE的场景必须是要发生模板代换的场景，而不能是这种副作用场景。

> 问的Copilot：
>
> Q：是不是因为在struct C : public Singleton<C>这里触发了Singleton的实例化,导致HasInit<T>::value变成HasInit<C>::value而被迫实例化;而注释掉的写法是HasInit<U>::value,而U只有在使用到init函数时才会被实例化,从而HasInit没有实例化
>
>
> A：你的理解**非常正确**！
>
> ------
>
> ### 详细解释
>
> #### 1. **未注释写法（HasInit<T>::value）**
>
> *struct* C : *public* Singleton<C> { ... };
>
> - 这行代码会**实例化 Singleton**。
> - 在实例化 Singleton 时，类体内的所有静态成员函数模板都会被声明（不是定义），但**所有静态成员的类型都要被检查**，包括 Init 的签名。
> - Init 的签名里有 `std::enable_if<HasInit<T>::value, T&>::type`，此时 T 已经是 C，所以会**立即实例化 HasInit::value**。
> - 这会导致 HasInit 的所有相关模板代码都被实例化、检查，这不符合SFINAE的预期使用方式。如果 C 没有 `init`，这里就会报错（不是SFINAE）。
>
> #### 2. **注释掉的写法（HasInit<U>::value）**
>
> // template <typename...Args, typename U = T>
>
> // static typename std::enable_if_t<HasInit<U>::value, T&> Init(Args&&... args)
>
> - 这里 U 是**函数模板的默认模板参数**，只有在你**调用 Init** 时，U 才会被推断/替换。
> - 只有在你真正用到 Init 时，HasInit::value 才会被实例化和检查。
> - 如果你没用到 Init，或者用到时 U 没有 `init`，SFINAE 会让这个重载被丢弃，不会报硬错误。
>
> ------
>
> ### 总结
>
> - **HasInit<T>::value**：在实例化 Singleton 时就会被实例化和检查，导致报错。
> - **HasInit<U>::value**：只有在用到 Init 时才会被实例化和检查，SFINAE生效，不会报错。

## 标准库支持

标准库提供了一些设施帮助我们更好的使用 SFINAE。

### `std::enable_if`

可能的实现：

```cpp
template<bool B, class T = void>
struct enable_if {};

template<class T> // 类模板偏特化
struct enable_if<true, T> { typedef T type; };     // 只有 B 为 true，才有 type，即 ::type 才合法

template< bool B, class T = void >
using enable_if_t = typename enable_if<B,T>::type; // C++14 引入
```

这是一个模板类，在 C++11 引入，它的用法很简单，就是第一个模板参数为 true，此模板类就有 `type = T`，不然就没有，以此进行 SFINAE。

```cpp
template<typename T,typename SFINAE = std::enable_if_t<std::is_same_v<T,int>>>
void f(T){}
```

函数 `f` 要求 `T` 类型必须是 `int` 类型；我们一步一步分析

`std::enable_if_t<std::is_same_v<T,int>>>` 如果 T 不是 int，那么 [std::is_same_v](https://zh.cppreference.com/w/cpp/types/is_same) 就会返回一个 false，也就是说 `std::enable_if_t<false>` ，带入：

```cpp
using enable_if_t = typename enable_if<false,void>::type; // void 是默认模板实参
```

但是问题在于：

- **enable_if 如果第一个模板参数为 `false`，它根本没有 `type` 成员**。

所以这里是个**代换失败**，但是因为“*代换失败不是错误*”，所以不会导致编译错误，只是不选择函数模板 `f` 而继续选择其他的模板。

---

再谈，`std::enable_if` 的默认模板实参是 **`void`**，如果我们不在乎 `std::enable_if` 得到的类型，就让它默认就行，比如我们先前的示例 `f` 根本不在乎第二个模板形参 `SFINAE` 是啥类型。

此外，`std::enable_if` 还有一种常见的用法，即利用其第二个模板参数而不声明额外的模板类型参数。例如：

```cpp
template<typename T,
    std::enable_if_t<std::is_same_v<T,int>,int> = 0>
void f(T){}
```

它的作用和之前的写法是一样的，但这个写法的原理是什么呢？我们可以逐步解析：

```cpp
std::enable_if_t<std::is_same_v<T,int>,int> = 0
```

这里的 `= 0` 实际上是对前面 `enable_if_t` 表达式的默认实参，它起到的是无名默认实参的作用。也就是说，如果 `std::is_same_v<T,int>` 为 true，那么 `std::enable_if_t<true,int>` 变为：

```cpp
using enable_if_t = typename enable_if<true,int>::type;
```

`true` 会选择 enable_if 的偏特化，从而**有 `type` 别名**，它的类型就是是我们传入的第二个参数 `int`。因此，**`std::enable_if_t<true,int>` 实际上就是 int**。

当然，如果 `std::is_same_v<T,int>` 为 `false`，则 `std::enable_if_t<false,int>` 会导致**代换失败**。而由于当前立即上下文是在实例化函数模板 `f` ，所以“代换失败不是错误”，所以只是不选择函数模板 `f`，而不会导致硬错误。（当然了，如果没有一个符合条件的重载，那还是会报编译错误的：“*未找到匹配的重载函数*”）。

---

```cpp
template <class Type, class... Args>
array(Type, Args...) -> array<std::enable_if_t<(std::is_same_v<Type, Args> && ...), Type>, sizeof...(Args) + 1>;
```

以上示例，是显式指明了 `std::enable_if` 的第二个模板实参，为 `Type`。

它是我们[类模板](02类模板.md)推导指引那一节的示例的**改进版本**，我们使用 `std::enable_if_t` 与 C++17 折叠表达式，为它增加了约束，这几乎和 [libstdc++](https://github.com/gcc-mirror/gcc/blob/7a01cc711f33530436712a5bfd18f8457a68ea1f/libstdc%2B%2B-v3/include/std/array#L292-L295) 中的代码一样。

`(std::is_same_v<Type, Args> && ...)` 做 std::enable_if 的第一个模板实参，这里是一个一元右折叠，使用了 **`&&`** 运算符，也就是必须 std::is_same_v 全部为 true，才会是 true。简单的说就是要求类型形参包 Args 中的每一个类型全部都是一样的，不然就是替换失败。

这样做有很多好处，如果不使用 SFINAE 约束，那么 array 的类型完全取决于第一个参数的类型，很容易导致其他问题：

```cpp
template<class Ty, std::size_t size>
struct array {
    Ty arr[size];
};

template<typename T, typename ...Args>
array(T t, Args...) -> array<T, sizeof...(Args) + 1>;

::array arr{1.4, 2, 3, 4, 5};        // 被推导为 array<double,5>
::array arr2{1, 2.3, 3.4, 4.5, 5.6}; // 被推导为 array<int,5>    有数据截断
```

### `std::void_t`

```cpp
template< class... >
using void_t = void; // C++17起
```

如你所见，它的实现非常非常的简单，就是一个别名，接受任意个数的类型参数，但自身始终是 `void` 类型。

- 将任意类型的序列映射到类型 void 的工具元函数。

- 模板元编程中，用此元函数检测 SFINAE 语境中的非良构类型[^2]。

---

*我要写一个函数模板 `add`，我要求传入的对象需要支持 `+` 以及它需要有别名 `type` ，成员 `value`、`f`*。

```cpp
template<typename T,
    typename SFINAE = std::void_t<
    decltype(T{} + T{}), // 要求有T::operator+(T)
    typename T::type,    // 要求有T::type
    decltype(&T::value), // 要求有T::value
    decltype(&T::f) >>   // 要求有T::f
auto add(const T& t1, const T& t2) {
    std::puts("SFINAE + | typename T::type | T::value");
    return t1 + t2;
}

struct Test {
    int operator+(const Test& t)const {
        return this->value + t.value;
    }
    void f()const{}
    using type = void;
    int value;
};

Test t{ 1 }, t2{ 2 };
add(t, t2);  // OK
//add(1, 2); // 未找到匹配的重载函数
```

- `decltype(T{} + T{})` 用 decltype 套起来只是为了获得类型符合语法罢了，`std::void_t` 只接受类型参数。如果类型没有 `operator+`，自然是*代换失败*。

- `typename T::type` 使用 `typename` 是因为[待决名](09待决名.md)；type 本身是类型，不需要 decltype。如果 `add` 推导的类型没有 `type` 别名，自然是*代换失败*。

- `decltype(&T::value)` 用 decltype 套就不用说了，`&T::value` 是[成员指针](https://zh.cppreference.com/w/cpp/language/pointer#.E6.88.90.E5.91.98.E6.8C.87.E9.92.88)的语法，不区分是数据成员还是成员函数，如果有这个成员 `value`，`&类名::成员名字` 自然合法，要是没有，就是*代换失败*。

- `decltype(&T::f)` ，其实前面已经说了，成员函数是没区别的，没有成员 `f` 就是 *代换失败*。

总而言之，这是为了使用 SFINAE。

> 那么这里 `std::void_t` 的作用是？

其实也没啥，就是让我们能在一个模板形参中写多个SFINAE条件（用 `decltype` 也一样的）。在C++17前，实现起来的形式和原理都是一样的：

```cpp
template <typename T,
    typename _1 = decltype(T{} + T{}), // 要求有T::operator+(T)
    typename _2 = typename T::type,    // 要求有T::type
    typename _3 = decltype(&T::value), // 要求有T::value
    typename _4 = decltype(&T::f)>     // 要求有T::f
auto add(const T& t1, const T& t2) {
    std::puts("+ | typename T::type | T::value");
    return t1 + t2;
}

struct Test {
    void f()const{}
};

Test t;
f(t);  // OK
f(1);  // 未找到匹配的重载函数
```

### `std::declval` 配合 `decltype`

```cpp
template<class T>
typename std::add_rvalue_reference<T>::type declval() noexcept;
```

将任意类型 T 转换成引用类型，*使得在 decltype 说明符的操作数中不必经过构造函数就能使用成员函数*。

- **[std::declval](https://zh.cppreference.com/w/cpp/utility/declval) 只能用于 [不求值语境](https://zh.cppreference.com/w/cpp/language/expressions#.E6.BD.9C.E5.9C.A8.E6.B1.82.E5.80.BC.E8.A1.A8.E8.BE.BE.E5.BC.8F)，且不要求有定义（即允许不完全类型）。而 `decltype` 正好提供的就是不求值语境**，两者一起用有奇效。

- **它不能被实际调用，因此不会返回值，返回类型是 `T&&`**。

它常用于模板元编程 SFINAE，我们用一个示例展现它的必要性：

```cpp
template<typename T, typename SFINAE = std::void_t<decltype(T{} + T{})> >
auto add(const T& t1, const T& t2) {
    std::puts("SFINAE +");
    return t1 + t2;
}

struct X{
    int operator+(const X&)const{
        return 0;
    }
};

struct X2 {
    X2(int){}   // 有参构造，没有默认构造函数
    int operator+(const X2&)const {
        return 0;
    }
};

int main(){
    X x1, x2;
    add(x1, x2);          // OK

    X2 x3{ 0 }, x4{ 0 };
    add(x3,x4);           // 未找到匹配的重载函数
}
```

错误的原因很简单，`decltype(T{} + T{})` 这个表达式中，同时**要求了 `T` 类型支持默认构造**（虽然这不是我们的本意，但不属于副作用），然而我们的 `X2` 类型没有默认构造，自然而然 `T{}` 不是合法表达式，*代换失败*。其实我们之前也有类似的写法，我们在本节进行纠正，使用 `std::declval`：

```cpp
template<typename T, typename SFINAE = std::void_t<decltype(std::declval<T>() + std::declval<T>())> >
auto add(const T& t1, const T& t2) {
    std::puts("SFINAE +");
    return t1 + t2;
}
```

把 `T{}` 改成 `std::declval<T>()` 即可，decltype 是不求值语境，没有问题。

---

还不止如此，使用它得以让我们先前的 `SFINAE` 检查类型是否有某些成员的形式得以适用于函数重载的情况，而不是像之前一样的  `decltype(&T::value), decltype(&T::f)` 的利用成员指针的形式。

```cpp
template<typename T,typename SFINAE = decltype(std::declval<T>().f(1))>
void f(int) { std::puts("f int"); }

template<typename T, typename SFINAE = decltype(std::declval<T>().f())>
void f(double) { std::puts("f double"); }

struct X{
    void f()const{}
};
struct Y{
    void f(int)const{}
};

int main(){
    f<X>(1); // f double
    f<Y>(1.1); // f int
}
```

显而易见，虽然我们的 `f<X>(1)` 传递的参数是 int 类型，但实际匹配到了参数为 `f(double)` 的版本，这是因为我们的 `f(int)` 版本的 `SFINAE` 约束要求了类型必须是支持 `f(1)` 这种形式，`X` 的成员函数 `f` 是空参的，自然不满足。

**使用此种方式得以更加明确的约束函数签名，从而得以==支持函数重载==。因为不管成员函数 f 的形参是什么情况，其成员指针表示形式都是：`&类名::f`。**

数据成员同样可以使用 `declval` 进行约束：

```cpp
template<typename T, typename SFINAE = decltype(std::declval<T>().value)>
void f(int) { std::puts("f int"); }

template<typename T>
void f(double) { std::puts("f double"); }

struct X {
    int value{};
};
struct Y {};

int main() {
    f<X>(1); // f int
    f<Y>(1); // f double
}
```

`f<Y>(1)`  虽然传递的参数是 int 类型，但是因为 `Y` 不满足 `SFINAE` 的约束，即没有成员 `value`，所以只能选择到 `f(double)` 的版本。

## 部分（偏）特化中的 SFINAE

在确定一个类或变量 (C++14 起)模板的特化是由部分特化还是主模板生成的时候也会出现推导与替换。在这种确定期间，**部分特化的替换失败不会被当作硬错误，而是像函数模板一样*代换失败不是错误*，只是忽略这个部分特化**。

```cpp
#include <iostream>

template<typename T,typename T2 = void>
struct X{
    static void f() { std::puts("主模板"); }
};

template<typename T>
struct X<T, std::void_t<typename T::type>>{
    using type = typename T::type;
    static void f() { std::puts("偏特化 T::type"); }
};

struct Test { using type = int; };
struct Test2 { };

int main(){
    X<Test>::f();       // 偏特化 T::type
    X<Test2>::f();      // 主模板
}
```

## 书写 SFINAE 的套路

主要有2种套路：同一种目的，2种风格。套路总结就是3步：

1. 构造一个模板参数代换的语境（即立即上下文）；
2. 提供一个代换失败后要回退的基本版本（如果你希望代换失败就出错，那就不要提供这个基本版本）；
3. 提供一个包含你需要的条件（traits）的代换成功的版本。

另外，注意可以**利用 `decltype` 和 `std::void_t` 来构造一个类型代换语境**，**利用逗号表达式或 `std::void_t` 来把多个类型代换表达式拼起来**。其中逗号表达式用于需要一个值的场景如 `std::enable_if` ，而 `std::void_t` 用于需要一个类型的场景。

假设我现在想写一个判断一个类型 `T` 是否可以进行迭代：

### 风格1

```cpp
// 基本模板，默认为false_type
template <typename T, typename SFINAE = void>
struct is_iterable : public std::false_type {};

// 特化版本，检查类型T是否有begin()和end()方法
template <typename T>
struct is_iterable<T, typename SFINAE = std::enable_if_t<
    std::is_same_v<
        decltype(std::declval<T>().begin()),
        decltype(std::declval<T>().end())
    >,
void>> : public std::true_type {};

// 更简单的实现版本（使用std::void_t）
template <typename T>
struct is_iterable<T, typename SFINAE = std::void_t<
    decltype(std::declval<T>().begin()),
    decltype(std::declval<T>().end())
>> : public std::true_type {};
```

注意：
- 由于我们是通过两个类模板实现的，立即上下文就是当前这个类模板，所以可以直接使用 `T` 作为 SFINAE 类型代换中的检查条件；
- 上面用了 `std::void_t` ，会导致所有 `SFINAE` 模板参数都推断为 `void` 。那么为啥不会选择到继承自 `std::false_type` 的基本模板呢？因为当有更特化的那个时，选择更特化的那个。

### 风格2

```cpp
template <typename T> struct is_iterable {
private:
    // 基本模板，默认为false_type
    template <typename SFINAE>
    static std::false_type check(...);

    // 特化版本，检查类型T是否有begin()和end()方法
    template <typename SFINAE>
    static auto check(int) -> decltype(std::declval<SFINAE>().begin(),
                                       std::declval<SFINAE>().end(),
                                       std::true_type{});

public:
    // 显式先重载决议特化版本，这样代换失败就能回退到基本模板。而不是一上来就基本模板。
    static constexpr bool value = decltype(check<T>(int{0}))::value;
};
```

注意：
- 由于我们是通过以 `T` 作为模板参数的类模板的成员函数来实现的，所以这里 `T` 不在当前类型代换要实例化的成员函数的立即上下文中，不能用 `T` 进行代换，所以单独搞了一个位于当前立即上下文的模板参数 `SFINAE` 来实现 SFINAE；
- 这里利用了逗号表达式是从左往右计算，最终返回值是最后一个表达式的返回值的特性。

---

[^1]: 注：“[重载决议](https://zh.cppreference.com/w/cpp/language/overload_resolution)”，简单来说，一个函数被重载，编译器必须决定要调用哪个重载，我们决定调用的是各形参与各实参之间的匹配最紧密的重载。

[^2]: 注：[非良构（ill-formed）](https://zh.cppreference.com/w/cpp/language/ub)——程序拥有语法错误或可诊断的语义错误。遵从标准的 C++ 编译器必须为此给出诊断。
