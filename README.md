使用 Datalog 这样的语言进行指针分析等静态分析将会比较简单，这个 toy 项目尝试将 [differential-datalog](https://github.com/vmware/differential-datalog) 整合进一个 out-of-tree llvm pass 以进行分析

安装好 DDlog 之后执行
```
make
```

即可

能查到的文档看起来比较少，不保证实现的正确性。

指针分析还远远没有完成，不过框架“算是”搭起来了。不得不承认虽然算是较系统地学过了基础的静态分析，但是真的到实际使用的时候，还是差的很远，很多细节还是考虑不清楚555

这个 pass 是在 mem2reg pass 之前执行的，并不算真正的 SSA，为了更好的分析，也许放在 mem2reg 之后会更好。不过由于只学过对非 SSA 的分析，还没完成触类旁通，暂时针对 mem2reg 前的 IR 分析。

由于参考文档比较少，我比较菜，碰到很多奇奇怪怪的问题，这里在写一点我碰到的坑点:(

- 因为 LLVM_ENABLE_ABI_BREAKING_CHECKS 引起的符号缺失

  表现为 load pass 的时候，比如

  ```
  clang++ -fpass-plugin=XXXPass.so ...
  ```

  时出现 `__ZN4llvm23EnableABIBreakingChecksE` 或 `__ZN4llvm24DisableABIBreakingChecksE` 符号 Not Found 的情况。

  原因：

  编译 pass 的时候开启了 LLVM_ENABLE_ABI_BREAKING_CHECKS，而作为加载器的 clang/opt 在编译的时候没开启（或者反一下，pass 没开启但是 clang 开启了）。

  解决方法：

  编译时加上的 `-DLLVM_DISABLE_ABI_BREAKING_CHECKS_ENFORCING=1` 参数，强制和 clang 统一。（根据具体情况设置成 1 或 0）

- 对于 llvm IR 中临时“变量”没有名字，不便于分析

  注明一下：由于 llvm IR 是 SSA（static single assignment）的，所以实际上不存在变量的说法，在编程语言中的标识符，如变量常量等 IR 中实际上就变成了一个个 Value。

  既然是 SSA，那么自然会出现大量的临时值，他们默认不会被赋予名字，由于我不熟悉 llvm pass 一般是怎么写的，所以我不知道应该怎么处理这些临时值，我的做法是通过 Value 类的 setName 方法直接给他赋予 "tmp" 的名字，比较方便的是，这样之后 getName 的时候，获得是 tmpXX 的形式，不用担心变量重名的问题。

  再注明：这个做法本质就是做了一次 -instnamer pass 的操作，参考 [InstructionNamer.cpp](https://llvm.org/doxygen/InstructionNamer_8cpp_source.html)

- 获取 Type 的 std::string 形式

  会发现，直接 errs() << Type 是可以的，但是并没有直接转成 std::string 的方法，当然我觉得这种操作一般只在调试的时候需要使用，所以不提供这个接口也很正常，但是这里要传递到 ddlog 中，所以需要转成 string，解决的方法是使用 print 方法

  ```cpp
    std::string TypeStr;
    llvm::raw_string_ostream RSO(TypeStr);
    AllocatedType->print(RSO);
    return RSO.str();
  ```

一些其他问题

- 由于 DDlog 提供的实际上是一个 C 库，所以要给整个头文件包上 extren "C"

  ```cpp
  #ifdef __cplusplus
  extern "C" {
  #endif
  #include "ddlog.h"
  #ifdef __cplusplus
  }
  #endif
  ```

还想说点别的

- 之前学习的是 Java 的静态分析，基于 soot 和 tai-e 的 ir，直接套过来，看来不太可行。比如 llvm ir 创建对象并不是使用 new 关键字，而是通过 new 函数，分配对象大小的空间，然后进行初始化。这个过程是先调用 new，然后 bitcast 到对应的类型，这样

  ```
  ; alloc the memory
  %call = call noalias noundef nonnull i8* @_Znwm(i64 noundef 24) #7
  %0 = bitcast i8* %call to %class.B*
  ; default construct
  %1 = bitcast %class.B* %0 to i8*
  call void @llvm.memset.p0i8.i64(i8* align 16 %1, i8 0, i64 24, i1 false)
  ``` 
  
  这样的实现，对于 obj 的设计，和 soot 或者说 Java 这样 new 时带类型信息相比，就会麻烦一些了。还需要学习更多
