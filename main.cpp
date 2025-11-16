#include <iostream>
#include <thread>
 int main() {
    std::thread t1([]() { std::cout << "Hello World!" << std::endl; })
    ;
    t1.join();
    return 0;
}