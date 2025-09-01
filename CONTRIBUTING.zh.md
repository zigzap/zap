# 🙌 贡献指南（中文）

非常感谢你对本项目的兴趣和支持！无论你是想提交代码、修复 Bug、完善文档，还是提出建议，我们都非常欢迎。

以下是参与贡献的详细指南。

---

## 💡 如何参与

你可以通过以下几种方式为本项目做出贡献：

- 报告 Bug 或建议（通过 Issues）
- 优化或修复已有功能
- 添加新功能
- 优化 UI/UX 或交互体验
- 编写或改进文档
- 提交测试用例、样例项目等

---

## 🔁 提交流程（从 Fork 到 PR）

请按以下步骤进行贡献代码的提交：

### 1. Fork 本项目

点击本项目右上角的 `Fork` 按钮，将仓库复制到你自己的 GitHub 账户下。

### 2. 克隆你的仓库副本

```bash
git clone https://github.com/<你的 GitHub 用户名>/<项目名>.git
cd <项目名>
```
### 3. 创建新分支
请不要直接在 master 分支上开发，建议使用具有描述性的分支名：

```bash
git checkout -b feature/xxx           # 新功能
git checkout -b fix/xxx-bug           # 修复 Bug
git checkout -b docs/update-readme    # 修改文档
```
### 4. 提交代码变更
```bash=
git add .
git commit -m "feat: 添加 xxx 功能"
git push origin feature/xxx
```

### 5. 提交 Pull Request（PR）
- 在 GitHub 网页端打开你的 Fork 仓库，点击 Compare & pull request；
- 填写 PR 标题和描述，说明你做了哪些修改和为什么；
- 目标分支请选择 main 或维护者指定的开发分支；
- 提交后等待维护者审核。

## ✅ Commit 提交规范（建议）
我们推荐使用 Conventional Commits 规范，便于统一日志风格和自动生成 changelog。

### 📋 格式：

| 类型       | 说明                                      |
|------------|-------------------------------------------|
| `feat`     | ✨ 添加新功能                              |
| `fix`      | 🐛 修复 Bug                                |
| `docs`     | 📝 仅修改文档内容                          |
| `style`    | 💅 代码格式修改（如空格、缩进、换行），不影响功能 |
| `refactor` | 🔨 重构代码（非新增功能或修复 Bug）        |
| `test`     | ✅ 添加或修改测试代码                      |
| `chore`    | 🔧 构建流程、工具配置或依赖更新            |
| `perf`     | ⚡ 性能优化相关改动                        |
| `revert`   | ⏪ 回滚历史提交                            |



示例：
```bash
git commit -m "feat: request"
git commit -m "fix: 修复 xxx 问题"
git commit -m "docs: 更新 CONTRIBUTING.md "
```
## 🛠️ 开发环境建议
- Zig 版本： 0.14.0 或以上
- 推荐 IDE：
    - CLion
    - VS Code + zls 插件

## 🐞 提交 Issue 指南
- 当你提交 Bug 或功能建议时，请尽量提供以下信息：
- 当前使用的系统、Zig 版本等环境信息
- 复现问题的操作步骤
- 问题的截图（如适用）
- 期望的行为和实际的表现差异

## 🤝 贡献建议
- 每次 Pull Request 尽量只修改一个功能或一个 Bug
- PR 提交前请确保可以成功构建并通过基本测试
- 尊重他人劳动成果，提交前可先通过 Issue 简要讨论以避免重复劳动

## 📄 License
本项目采用 MIT License。

感谢你对开源社区的支持和贡献 🙏