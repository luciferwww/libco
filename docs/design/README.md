# libco 2.0 设计文档索引

欢迎阅读 libco 2.0 的设计文档。这些文档详细描述了项目的架构、API、实现细节、测试策略和开发计划。

## 📑 文档列表

### 核心设计文档

1. **[00-overview.md](./00-overview.md) - 项目概览**
   - 项目定位和核心特性
   - 与 C++20 协程的对比
   - 目标用户和设计目标
   - 技术栈选择
   - 成功标准

2. **[01-architecture.md](./01-architecture.md) - 架构设计**
   - 整体架构图
   - 核心模块详解
     - Context（上下文管理）
     - Scheduler（调度器）- 支持多种调度策略
     - Routine（协程）- 完整生命周期管理
     - Runtime（运行时）- Select、TLS 等高级特性
   - 平台抽象层
   - 线程安全设计
   - 性能考虑

3. **[02-api-design.md](./02-api-design.md) - API 设计**
   - API 设计原则
   - 完整的 C API 定义
   - 协程生命周期管理（join、cancel、detach）
   - Select 多路等待
   - 协程本地存储（TLS）
   - 调试和诊断 API
   - C++ 扩展 API
   - 丰富的使用示例

4. **[03-implementation.md](./03-implementation.md) - 实现细节**
   - 上下文切换实现（各平台）
   - 调度器核心算法
   - 定时器堆实现
   - I/O 多路复用实现
   - Channel 实现
   - 性能优化技巧

5. **[04-testing.md](./04-testing.md) - 测试策略**
   - 测试金字塔
   - 单元测试计划
   - 集成测试计划
   - 性能基准测试
   - 内存测试（Valgrind、ASan）
   - CI/CD 配置

6. **[05-build.md](./05-build.md) - 构建系统**
   - CMake 项目结构
   - 配置文件
   - 构建脚本
   - 包管理器支持（vcpkg、Conan）
   - 交叉编译
   - Docker 构建

7. **[06-roadmap.md](./06-roadmap.md) - 实施路线图**
   - 14 周开发计划
   - 6 个阶段里程碑
   - 每周详细任务
   - 风险评估和缓解
   - 成功标准检查清单

## 🎯 阅读建议

### 快速了解项目

如果你想快速了解项目，建议按顺序阅读：
1. [项目概览](./00-overview.md) - 了解项目是什么
2. [API 设计](./02-api-design.md) - 看看如何使用
3. [实施路线图](./06-roadmap.md) - 了解开发计划

### 深入技术细节

如果你想深入了解技术实现，建议阅读：
1. [架构设计](./01-architecture.md) - 整体架构
2. [实现细节](./03-implementation.md) - 核心算法
3. [测试策略](./04-testing.md) - 质量保证

### 参与开发

如果你想参与开发，建议阅读：
1. [实施路线图](./06-roadmap.md) - 当前进度和任务
2. [构建系统](./05-build.md) - 如何构建项目
3. [测试策略](./04-testing.md) - 如何编写测试

## 📊 设计决策记录

### 为什么选择 Stackful 而不是 Stackless？

**决策**：实现 stackful 协程

**理由**：
1. 可以在任意调用深度挂起，适合 Hook 系统调用
2. 对现有代码侵入性小，迁移成本低
3. 填补 C++20 协程的空白
4. 适合 C 语言生态

**权衡**：
- 优势：灵活性高，适用场景广
- 劣势：内存开销较大（每协程需要独立栈）

### 为什么使用 C17 而不是 C11？

**决策**：使用 C17 标准

**理由**：
1. C17 修复了 C11 的一些缺陷
2. 仍然保持广泛的编译器支持
3. 现代化但不过于激进
4. 2026 年 C17 已经很成熟

### 为什么 C++ 扩展使用 C++17？

**决策**：C++ 扩展使用 C++17

**理由**：
1. C++17 提供了足够的现代特性（结构化绑定、if constexpr 等）
2. 比 C++20 有更广泛的编译器支持
3. 不需要 C++20 的协程特性（我们自己实现）

### 为什么选择 Unity 作为 C 测试框架？

**决策**：使用 Unity 测试框架

**理由**：
1. 纯 C 实现，无依赖
2. 轻量级，易于集成
3. API 简单直观
4. 广泛使用，文档完善

**备选方案**：Check、Criterion

## 🔄 文档更新

这些设计文档会随着项目的演进而更新：

- **设计阶段**（当前）：文档详细描述计划
- **实施阶段**：根据实际情况调整设计
- **发布后**：补充实际经验和最佳实践

## 💡 反馈和建议

如果你对设计有任何意见或建议，欢迎通过以下方式反馈：

1. 创建 GitHub Issue
2. 提交 Pull Request
3. 发送邮件

我们非常重视社区的反馈！

## 📝 文档规范

本项目的文档遵循以下规范：

- **格式**：Markdown
- **命名**：`<编号>-<描述>.md`
- **结构**：清晰的标题层次，代码示例，图表说明
- **语言**：中文为主，代码和技术术语使用英文

## 🎓 相关资源

### 协程基础知识

- [Wikipedia - Coroutine](https://en.wikipedia.org/wiki/Coroutine)
- [Coroutines in C](https://www.chiark.greenend.org.uk/~sgtatham/coroutines.html)

### 上下文切换

- [ucontext man page](https://man7.org/linux/man-pages/man3/getcontext.3.html)
- [Windows Fibers](https://docs.microsoft.com/en-us/windows/win32/procthread/fibers)

### 参考实现

- [libaco](https://github.com/hnes/libaco)
- [libco (腾讯)](https://github.com/Tencent/libco)
- [Boost.Context](https://www.boost.org/doc/libs/release/libs/context/)

### 性能分析

- [perf](https://perf.wiki.kernel.org/)
- [Valgrind](https://valgrind.org/)
- [AddressSanitizer](https://github.com/google/sanitizers)

---

**最后更新**：2026-03-23
**状态**：✅ 设计完成，准备开始实施
