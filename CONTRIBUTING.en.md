# 🙌 Contribution Guide (English)

Thank you very much for your interest and support in this project! Whether you want to submit code, fix bugs, improve documentation, or propose suggestions, you are very welcome.

Here is a detailed guide on how to contribute.

---

## 💡 How to Contribute

You can contribute to this project in the following ways:

- Report bugs or suggestions (via Issues)
- Improve or fix existing features
- Add new features
- Enhance UI/UX or user interaction
- Write or improve documentation
- Submit test cases, sample projects, etc

---

## 🔁 Contribution Process (From Fork to PR)

Please follow these steps to submit your code contributions:

### 1. Fork this repository

Click the Fork button at the top right of this project page to copy the repository to your own GitHub account.

### 2. Clone your fork

```bash
git clone https://github.com/<your-github-username>/<repository-name>.git
cd <repository-name>
```
### 3. Create a new branch
Please avoid working directly on the main branch. Use descriptive branch names like:：

```bash
git checkout -b feature/xxx          # new feature
git checkout -b fix/xxx-bug          # bug fix
git checkout -b docs/update-readme   # documentation update
```
### 4. Commit your changes
```bash
git add .
git commit -m "feat: add xxx feature"
git push origin feature/xxx
```

### 5. Create a Pull Request (PR)
- On GitHub, open your fork repository, and click Compare & pull request;
- Fill in the PR title and description explaining what you changed and why;
- Choose the target branch as main or the development branch designated by the maintainers;
- Submit and wait for the maintainers to review.

## ✅ Commit Message Guidelines (Recommended)
We recommend following the Conventional Commits specification to keep commit logs consistent and enable automatic changelog generation.

### 📋 Format:

| Type      | 	Description                                      |
|------------|-------------------------------------------|
| `feat`     | ✨ Introduce a new feature                             |
| `fix`      | 🐛 Fix a bug                                |
| `docs`     | 📝 Documentation only changes                          |
| `style`    | 💅 Code formatting (white-space, indentation, etc.) no functional changes |
| `refactor` | 🔨 Code refactoring (neither a fix nor a new feature)        |
| `test`     | ✅ Adding or modifying tests                      |
| `chore`    | 🔧 Changes to build process, tooling or dependencies            |
| `perf`     | ⚡ Performance improvements                        |
| `revert`   | ⏪ Revert previous commits                            |



Example commits:
```bash
git commit -m "feat: request"
git commit -m "fix: resolve xxx"
git commit -m "docs: update CONTRIBUTING.md"
```
## 🛠️ Recommended Development Environment
- Zig version: 0.14.0 or above
- Dependency management: Go Modules
- Recommended IDEs:
    - CLion
    - VS Code + official Zls plugin

## 🐞 Issue Submission Guide
- When submitting a bug report or feature request, please try to provide:
- Your operating system, Go version, and environment details
- Steps to reproduce the issue
- Screenshots if applicable
- Expected behavior and actual behavior differences

## 🤝 Contribution Suggestions
- Try to keep each Pull Request focused on a single feature or bug fix
- Make sure your code builds successfully and passes basic tests before submitting PR
- Respect others' work; it’s encouraged to discuss ideas via Issues before coding to avoid duplicated efforts

## 📄 License
This project is licensed under the MIT.

Thank you for your support and contributions to the open source community! 🙏