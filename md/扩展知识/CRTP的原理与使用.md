# CRTP 奇特重现模板模式

## 背景：虚函数和动态绑定

C++ 通过类的继承与虚函数的动态绑定，实现了动态多态。这种特性，使得我们能够用基类的指针/引用，访问子类的实例。

```cpp
struct Base {
    virtual void func() {}
};
struct Derived : Base {
    virtual void func() override {}
};
Base *p = new Derived();
p->func();
```

在执行 `p->func()` 时，编译器生成的指令会检查 `p` 指向的实际类型（动态类型 `Derived` ），然后调用对应的 `func()` 。这个过程涉及到查询虚函数表，且 `p` 的动态类型是在运行时确定的，而非编译时确定，所以存在运行时开销。

## 使用 CRTP 实现静态多态

为了在编译时绑定，我们就需要放弃虚函数机制，而只是在基类和子类中实现同名的普通函数；同时，为了在编译时确定类型，我们就需要将子类的名字在编译时提前传给基类。因此，我们需要用到 C++ 的模板，这个实现的套路叫做**奇异递归模板式（Curiously Recurring Template Pattern, CRTP）**。

CRTP的范式基本上都是有一个模板父类和任意子类：

- 基类是一个模板类并以子类类型作为父类模板实参；
- 因此子类的继承列表会类似于 `class Derived : public Base<Derived>`；
- 基类定义要实现静态多态的函数，在其函数体中使用 `static_cast<>` 将基类的指针转为（模板）子类的指针，在编译期完成绑定。

此外，往往需要在子类中将基类设置为友元，从而能访问到未公开的函数。

```cpp
template <typename Derived>
class Base {
public:
    void func() {
        static_cast<Derived*>(this)->funcImpl(); // 强制转换为子类类型，然后调用子类的函数（因为我们明确this的静态类型就是Derived参数对应的类型）
        // 用引用也是可以的
        // static_cast<Derived&>(*this).funcImpl();
    }
};

class SubClaz : public Base<SubClaz> { // 显式实例化了Base<SubClaz>类并作为SubClaz的基类
friend class Base<SubClaz>; // 确保基类Base<SubClaz>可以看到这里私有的funcImpl()
private:
	void funcImpl() {}
    // 可以想想这里继承得到了一个public: void Base<SubClaz>::func();
};

SubClaz s;
s.func();
```

### 扩展实现

#### 额外提供一个抽象基类

由于模板实例化产生的是不相干的类，因此类似于使用 `std::vector<Base*>` 来存储CRTP子类的做法是行不通的，所以CRTP实际上是损失了一定的多态性的。

不过这种多态性的损失可以简单地通过继承一个普通的基类实现（相当于这个非模板的基类会有多个平级的不同的子类，从将多态压缩到1层，不会出现多层的虚函数表查找，所以性能仍然有保障）。注意这种做法也可以用于将CRTP模板的”虚方法“转换为真正的虚方法，使其可以提供默认实现。

```cpp
#include <iostream>
#include <vector>

using std::cout;
using std::vector;

class Animal {
public:
    virtual void say() const = 0; // 虚函数
    virtual ~Animal() = default;
};

template<typename T>
class Animal_CRTP : public Animal {
public:
    void say() const override { // CRTP模板基类重写虚函数（注意此时say()是一个虚函数）
        static_cast<const T *>(this)->say();
    }
};

class Cat : public Animal_CRTP<Cat> {
public:
    void say() const { // 注意此时say()是一个虚函数
        cout << "Meow~ I'm a cat." << '\n';
    }
};

class Dog : public Animal_CRTP<Dog> {
public:
    void say() const { // 注意此时say()是一个虚函数
        cout << "Wang~ I'm a dog." << '\n';
    }
};

int main() {
    vector<Animal *> zoo;
    zoo.push_back(new Cat());
    zoo.push_back(new Dog());
    for ( vector<Animal *>::const_iterator iter{ zoo.begin() }; iter != zoo.end(); ++iter ) {
        (*iter)->say();
    }
    for ( vector<Animal *>::iterator iter{ zoo.begin() }; iter != zoo.end(); ++iter ) {
        delete (*iter);
    }
    return 0;
}
```

#### 为CRTP”虚方法“提供默认实现

除了使用“额外提供一个抽象基类”的方式将CRTP”虚方法“转化为真正的虚方法来提供默认实现，还可以利用SFINAE来选择合适的实现版本：

```cpp
#include <type_traits>
#include <iostream>

// 定义辅助traits：检测Derived是否有funcImpl()方法
template <typename Derived>
struct has_func_impl {
private:
    // 尝试调用Derived::funcImpl()，若存在则匹配此重载（返回std::true_type）
    template <typename T>
    static auto check(int) -> decltype(std::declval<T>().funcImpl(), std::true_type{});
    // 若不存在，则匹配此重载（返回std::false_type）
    template <typename T>
    static std::false_type check(...);
public:
    // 最终结果：true表示Derived有funcImpl()，false则无
    static constexpr bool value = decltype(check<Derived>(int{0}))::value;
};

// CRTP基类：提供默认实现
template <typename Derived>
class Base {
public:
    // 对外接口：统一调用入口
    void func() {
        // 根据派生类是否有funcImpl()，选择不同实现
        funcImplDispatch(std::integral_constant<bool, has_func_impl<Derived>::value>{});
    }

private:
    void defaultFuncImpl() {
        std::cout << "Base::defaultFuncImpl (默认实现)\n";
    }

    void funcImplDispatch(std::true_type) {
        static_cast<Derived*>(this)->funcImpl();
    }
    void funcImplDispatch(std::false_type) {
        defaultFuncImpl();
    }
};

// 派生类A：实现了funcImpl()，使用自己的逻辑
class DerivedA : public Base<DerivedA> {
friend class Base<DerivedA>; // 允许基类访问private的funcImpl()
friend class has_func_impl<DerivedA>; // 允许has_func_impl访问private的funcImpl()
private:
    void funcImpl() {
        std::cout << "DerivedA::funcImpl (自定义实现)\n";
    }
};

// 派生类B：未实现funcImpl()，使用基类默认实现
class DerivedB : public Base<DerivedB> {
};

int main() {
    DerivedA a;
    a.func(); // 输出：DerivedA::funcImpl (自定义实现)

    DerivedB b;
    b.func(); // 输出：Base::defaultFuncImpl (默认实现)
    return 0;
}
```

## C++23 的改动-显式对象形参

C++23 引入了**显式对象形参**，让我们的 `CRTP` 的形式也出现了变化：

> [显式对象形参](https://zh.cppreference.com/w/cpp/language/member_functions#.E6.98.BE.E5.BC.8F.E5.AF.B9.E8.B1.A1.E6.88.90.E5.91.98.E5.87.BD.E6.95.B0)，顾名思义，就是将 C++23 之前，隐式的，由编译器自动将 `this` 指针传递给成员函数使用的，改成**允许用户显式写明**了，也就是：
>
> ```cpp
> struct X{
>  void f(this const X& self){}
> };
> ```
>
> 它也支持模板（可以直接 `auto` 而无需再 `template<typename>`），也支持各种修饰，如：`this X self`、`this X& self`、`this const X& self`、`this X&& self`、`this auto&& self`、`const auto& self` ... 等等。

```cpp
struct Base { void name(this auto&& self) { self.impl(); } };
struct D1 : Base { void impl() { std::puts("D1::impl()"); } };
struct D2 : Base { void impl() { std::puts("D2::impl()"); } };
```

不再需要使用 `static_cast` 进行转换，直接调用即可。且如你所见，我们的显式对象形参也可以写成模板的形式：`this auto&& self`。

使用上也与之前并无区别，创建子类对象，调用接口即可。

```cpp
D1 d;
d.name();
D2 d2;
d2.name();
```

`d.name` 也就是把 `d` 传入给父类模板成员函数 `name`，`auto&&` 被推导为 `D1&`，顾名思义”***显式***“对象形参，非常的简单直观。

> [运行](https://godbolt.org/z/WW59PqEd3)测试。
