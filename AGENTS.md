# Project Coding Rules (C++/Qt)

本文件用于约束本仓库的代码风格、工程约定与 agent 执行规范。  
对本仓库进行任何代码修改时，必须遵守本文件规则。

## 0. Project Context

### 0.1 Project Summary

- 项目名称：`DockerRoboshopServer`
- 技术栈：`Qt 6.7.x`、`QSslSocket`、`QTcpServer`、`CMake`、`Docker`、`Helm`
- 项目类型：轻量级 HTTPS 服务端程序
- 默认监听地址：`0.0.0.0:8443`

### 0.2 Runtime Endpoints

- `GET /`
- `GET /health`
- `GET /api/version`
- `POST /api/echo`

### 0.3 Key Directories

- `src/`：核心源码目录
- `scripts/`：镜像启动与发布辅助脚本
- `helm/`：Kubernetes / CCE 部署 Chart
- `certs/`：本地或容器挂载证书
- `deploy/`：部署产物目录，通常为打包输出
- `build/`、`build-msvc/`：本地构建输出目录

### 0.4 Build And Run Reference

- 优先使用 `CMakePresets.json` 中已有 preset。
- 常用配置命令：
  - `cmake --preset vs2019-release`
  - `cmake --build --preset build-vs2019-release`
  - `cmake --preset ninja-release`
  - `cmake --build --preset build-ninja-release`
- 本地运行示例：
  - `.\build\vs2019-release\Release\DockerRoboshopServer.exe --host 0.0.0.0 --port 8443 --cert certs/server.crt --key certs/server.key --max-pending 4096 --listen-backlog 8192 --max-active 12000`
- Docker 验证示例：
  - `docker build -t docker-roboshop-server:1.0.0 .`
  - `docker run --rm -p 8443:8443 docker-roboshop-server:1.0.0`
- Helm 部署示例：
  - `helm install roboshop-server ./helm`
  - `helm upgrade --install roboshop-server ./helm -f helm/values-production.yaml`
- 冒烟验证示例：
  - `curl.exe -k https://127.0.0.1:8443/health`
  - `curl.exe -k -X POST https://127.0.0.1:8443/api/echo -H "Content-Type: application/json" -d "{\"hello\":\"qt6\"}"`

### 0.5 Runtime Environment Variables

- `SERVER_HOST`
- `SERVER_PORT`
- `SERVER_MAX_PENDING`
- `SERVER_LISTEN_BACKLOG`
- `SERVER_MAX_ACTIVE`
- `SSL_CERT_FILE`
- `SSL_KEY_FILE`

### 0.6 Helm Defaults

- Helm chart 路径：`helm/`
- 默认公共访问地址：`https://sep-rbs-server.cloud-data-dev.seer-group.com/`
- `helm/values.yaml` 中的 `ingress.publicUrl` 是完整 URL，不是裸 host。
- Ingress 模板会自动从 `ingress.publicUrl` 提取 `Ingress.spec.rules.host` 所需的 hostname。
- Pod 内应用默认仍通过 `8443` 端口提供 `HTTPS` 服务。
- 若需要挂载服务端证书，优先使用 `tls.existingSecret`，并保证 Secret 内存在 `server.crt` 与 `server.key`。

### 0.7 Agent Execution Rules

- 修改逻辑前先以 `README.md`、`CMakeLists.txt`、`CMakePresets.json`、`src/`、`scripts/` 和 `helm/` 为准核对实际行为。
- 默认不要修改 `build/`、`build-msvc/` 下的生成文件，除非任务明确要求处理构建产物。
- 默认不要直接手改 `deploy/` 下二进制、Qt 库和插件，除非任务明确要求更新部署包。
- 涉及 Docker 启动逻辑时，必须同时检查 `Dockerfile`、`docker-compose.yml` 和 `scripts/entrypoint.sh`。
- 涉及 Kubernetes / CCE 部署逻辑时，必须同时检查 `helm/values.yaml`、`helm/values-production.yaml` 和相关 `templates/`。
- 涉及 HTTPS、证书或端口调整时，必须同步检查命令行参数、环境变量和 README 示例是否一致。
- 涉及 Helm Ingress 变更时，区分完整 URL 与 Ingress `host` 字段，不要直接把 `http://` 或路径写入 `spec.rules.host`。
- 修改接口行为时，应保持以下端点兼容：`/`、`/health`、`/api/version`、`/api/echo`。
- 若修改会影响部署流程，需补充最小验证说明，至少覆盖构建、启动或接口冒烟中的一项。
- 若修改会影响 Helm chart，需同步检查 `README.md` 中 Helm 用法、默认 URL、证书 Secret 示例和安装命令。

## 1. Naming

- 类名：`UpperCamelCase`  
  示例：`RobotStatusWidget`
- 结构体名：`UpperCamelCase`  
  示例：`RobotPose`
- 函数名：`lowerCamelCase`  
  示例：`loadRobotConfig`
- 普通变量名：`lowerCamelCase`  
  示例：`currentRobotId`
- 成员变量名：`_lowerCamelCase`  
  示例：`_currentRobotId`
- 静态成员变量名：`s_lowerCamelCase`  
  示例：`s_instance`
- 常量名：`kUpperCamelCase`  
  示例：`kDefaultTimeoutMs`
- 宏名：`UPPER_SNAKE_CASE`  
  示例：`MAX_BUFFER_SIZE`
- 枚举类型名：`UpperCamelCase`  
  示例：`ConnectionState`
- 枚举值：`E_` 前缀 + `UpperCamelCase`  
  示例：`E_Disconnected`
- Qt 信号函数名：`sigXxx`  
  示例：`sigConnectionChanged`
- Qt 槽函数名：`slotXxx`  
  示例：`slotReconnect`

## 2. File And Type Layout

- 一个主要类对应一对文件：`.h/.cpp`，文件名与类名一致。
- 头文件使用 include guard 或 `#pragma once`，同一子模块保持一致。
- 头文件中禁止 `using namespace xxx;`。
- include 顺序：
  1. 本文件对应头文件
  2. Qt 头文件
  3. 标准库头文件
  4. 项目内其他头文件
- 能前置声明就前置声明，减少头文件耦合。

## 3. Function Rules

- 函数职责单一，优先早返回，减少嵌套层级。
- 单个函数建议不超过 80 行；超过时拆分私有辅助函数。
- 参数超过 4 个时，优先封装为结构体或配置对象。
- 避免魔法数字，提取为具名常量。
- 任何可能失败的操作必须有返回值检查或错误日志。

## 4. Qt Rules

- 能用新式连接就用新式连接：
  `connect(a, &A::sigXxx, b, &B::slotXxx);`
- QObject 对象优先通过父子关系管理生命周期。
- 非 QObject 资源优先使用 RAII（智能指针或栈对象）。
- UI 线程不做长耗时阻塞操作，耗时任务放工作线程。

## 5. Comment And Doc

- 对外接口（public API）必须有 Doxygen 注释。
- 注释解释“为什么”，避免只写“做了什么”。
- 复杂算法必须补充输入、输出、边界条件说明。

## 6. Logging And Error Handling

- 错误路径必须记录可定位日志（包含关键参数）。
- 日志内容应可检索，禁止无意义日志（如只打印“failed”）。
- 不要吞异常或忽略错误码。

## 7. Safety

- 禁止在未确认线程安全的情况下跨线程共享可变对象。
- 禁止引入未使用代码、未使用 include、未使用变量。
- 修改旧逻辑时保持行为兼容，除非需求明确要求变更行为。

## 8. Commit Quality Checklist

- 命名符合本规范。
- 编译通过。
- 关键路径已做最小验证（启动、主流程、异常路径）。
- 无明显无用代码、无调试残留。
- 若改动影响容器启动，已检查 `Dockerfile` / `docker-compose.yml` / `scripts/entrypoint.sh` 的一致性。
- 若改动影响 HTTP 接口，已验证至少一个成功路径和一个错误路径。
- 若改动影响 Helm chart，已检查 `helm/values*.yaml`、`templates/` 与 `README.md` 的一致性。

## 9. Priority

- 若与历史代码风格冲突：新改动按本规范执行。
- 若与外部硬性规范冲突（编译器、框架、接口协议）：以硬性规范为准，并在注释中说明原因。
