# DockerRoboshopServer

基于 Qt 6.7.x 与 MSVC2019 的 HTTPS 服务端项目。

## 功能特性

- 基于 `QTcpServer + QSslSocket` 的 HTTPS 服务
- 接口列表：
  - `GET /`
  - `GET /health`
  - `GET /api/version`
  - `POST /api/echo`
- 支持 Keep-Alive 与连接复用，降低 TLS 握手开销
- 提供高并发控制参数：等待队列、监听 backlog、活动连接上限
- 使用 CMake 管理，可通过 Visual Studio 2019 构建
- 提供 Docker 打包与华为云 SWR 推送辅助脚本

## 项目结构

```text
.
|-- CMakeLists.txt
|-- CMakePresets.json
|-- Dockerfile
|-- docker-compose.yml
|-- helm/
|   |-- Chart.yaml
|   |-- values.yaml
|   |-- values-production.yaml
|   `-- templates/
|-- src/
|   |-- main.cpp
|   |-- HttpsServer.h
|   `-- HttpsServer.cpp
|-- scripts/
|   |-- entrypoint.sh
|   `-- push_to_huaweicloud.sh
`-- certs/
```

## 本地构建（Qt 6.7.3 + VS2019）

在 PowerShell 中执行：

```powershell
cd D:\QtDemo\2026\3\DockerRoboshopServer
D:/Qt/6.7.3/Tools/CMake_64/bin/cmake.exe -S . -B build-vs2019 -G "Visual Studio 16 2019" -A x64 -DCMAKE_PREFIX_PATH="D:/Qt/6.7.3/6.7.3/msvc2019_64"
cmake --build build-vs2019 --config Release
```

### 使用 vcvars + Ninja 工具链构建

```powershell
cmd.exe /c "\"D:\vs\2019\IDE\VC\Auxiliary\Build\vcvars64.bat\" && \"D:\Qt\6.7.3\Tools\CMake_64\bin\cmake.exe\" -S D:\QtDemo\2026\3\DockerRoboshopServer -B D:\QtDemo\2026\3\DockerRoboshopServer\build-msvc -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=D:/Qt/6.7.3/6.7.3/msvc2019_64 -DCMAKE_MAKE_PROGRAM=D:/Qt/6.7.3/Tools/Ninja/ninja.exe"
cmd.exe /c "\"D:\vs\2019\IDE\VC\Auxiliary\Build\vcvars64.bat\" && \"D:\Qt\6.7.3\Tools\CMake_64\bin\cmake.exe\" --build D:\QtDemo\2026\3\DockerRoboshopServer\build-msvc -j 8"
```

### 生成本地证书

```powershell
cd D:\QtDemo\2026\3\DockerRoboshopServer
mkdir certs -Force
$env:OPENSSL_CONF="D:\Qt\6.7.3\Tools\OpenSSLv3\Win_x64\openssl.cnf"
D:/Qt/6.7.3/Tools/OpenSSLv3/Win_x64/bin/openssl.exe req -x509 -nodes -newkey rsa:2048 -keyout certs/server.key -out certs/server.crt -days 365 -subj "/CN=localhost"
```

### 运行本地可执行文件

```powershell
.\build-vs2019\Release\DockerRoboshopServer.exe --host 0.0.0.0 --port 8443 --cert certs/server.crt --key certs/server.key --max-pending 4096 --listen-backlog 8192 --max-active 12000
```

### 高并发参数

- 命令行参数：
  - `--max-pending` (default: `1024`)
  - `--listen-backlog` (default: `2048`)
  - `--max-active` (default: `4096`)
- 环境变量：
  - `SERVER_MAX_PENDING`
  - `SERVER_LISTEN_BACKLOG`
  - `SERVER_MAX_ACTIVE`

### 测试

```powershell
curl.exe -k https://127.0.0.1:8443/health
curl.exe -k -X POST https://127.0.0.1:8443/api/echo -H "Content-Type: application/json" -d "{\"hello\":\"qt6\"}"
```

## Docker 构建与运行

```powershell
cd D:\QtDemo\2026\3\DockerRoboshopServer
docker build -t docker-roboshop-server:1.0.0 .
docker run --rm -p 8443:8443 docker-roboshop-server:1.0.0
```

如果没有挂载证书，`scripts/entrypoint.sh` 会自动生成自签名证书。

### 挂载生产证书

```powershell
docker run --rm -p 8443:8443 `
  -e SSL_CERT_FILE=/app/certs/server.crt `
  -e SSL_KEY_FILE=/app/certs/server.key `
  -e SERVER_MAX_PENDING=4096 `
  -e SERVER_LISTEN_BACKLOG=8192 `
  -e SERVER_MAX_ACTIVE=12000 `
  -v D:\my-certs:/app/certs:ro `
  docker-roboshop-server:1.0.0
```

## 部署到华为云（SWR + ECS）

### 1）推送镜像到 SWR

使用辅助脚本：

```bash
cd /path/to/DockerRoboshopServer
chmod +x scripts/push_to_huaweicloud.sh
./scripts/push_to_huaweicloud.sh cn-north-4 <namespace> <username> <password_or_token> docker-roboshop-server 1.0.0
```

或手工执行：

```bash
docker login -u <username> -p <password_or_token> swr.cn-north-4.myhuaweicloud.com
docker build -t docker-roboshop-server:1.0.0 .
docker tag docker-roboshop-server:1.0.0 swr.cn-north-4.myhuaweicloud.com/<namespace>/docker-roboshop-server:1.0.0
docker push swr.cn-north-4.myhuaweicloud.com/<namespace>/docker-roboshop-server:1.0.0
```

### 2）在 ECS 上运行

```bash
docker login -u <username> -p <password_or_token> swr.cn-north-4.myhuaweicloud.com
docker pull swr.cn-north-4.myhuaweicloud.com/<namespace>/docker-roboshop-server:1.0.0
docker run -d --name docker-roboshop-server \
  -p 8443:8443 \
  -e SERVER_HOST=0.0.0.0 \
  -e SERVER_PORT=8443 \
  -e SERVER_MAX_PENDING=4096 \
  -e SERVER_LISTEN_BACKLOG=8192 \
  -e SERVER_MAX_ACTIVE=12000 \
  -e SSL_CERT_FILE=/app/certs/server.crt \
  -e SSL_KEY_FILE=/app/certs/server.key \
  -v /opt/certs:/app/certs:ro \
  swr.cn-north-4.myhuaweicloud.com/<namespace>/docker-roboshop-server:1.0.0
```

## 使用 Helm 部署到 Kubernetes

仓库中已经包含位于 `helm/` 目录下的 Helm Chart。

### 1）Chart 默认行为

- 默认外部访问地址：`https://sep-rbs-server.cloud-data-dev.seer-group.com/`
- `values.yaml` 中的 `ingress.publicUrl` 使用完整 URL。
- 模板会自动提取其中的 hostname，并写入 `Ingress.spec.rules.host`。
- 容器探针使用 `HTTPS` 协议访问 `/health`。
- 应用容器监听端口为 `8443`。

### 2）使用默认值安装

```bash
helm install roboshop-server ./helm
```

### 3）使用 SWR 镜像安装

```bash
helm install roboshop-server ./helm \
  --set image.repository=swr.cn-north-4.myhuaweicloud.com/<namespace>/sep-rbs-server \
  --set image.tag=1.0.0
```

### 4）通过 Kubernetes Secret 挂载后端服务证书

服务本身运行在 HTTPS 模式下，因此在 Kubernetes 中应提供一个包含以下文件的 Secret：

- `server.crt`
- `server.key`

示例：

```bash
kubectl create secret generic roboshop-server-tls \
  --from-file=server.crt=/path/to/server.crt \
  --from-file=server.key=/path/to/server.key
```

然后执行安装：

```bash
helm install roboshop-server ./helm \
  --set image.repository=swr.cn-north-4.myhuaweicloud.com/<namespace>/sep-rbs-server \
  --set image.tag=1.0.0 \
  --set tls.existingSecret=roboshop-server-tls
```

### 5）生产环境 values

```bash
helm install roboshop-server ./helm -f helm/values-production.yaml \
  --set image.repository=swr.cn-north-4.myhuaweicloud.com/<namespace>/sep-rbs-server \
  --set image.tag=1.0.0
```

或执行升级：

```bash
helm upgrade --install roboshop-server ./helm -f helm/values-production.yaml \
  --set image.repository=swr.cn-north-4.myhuaweicloud.com/<namespace>/sep-rbs-server \
  --set image.tag=1.0.0
```

### 6）Ingress 说明

- 当前 Chart 默认使用 CCE HTTPS Ingress，前端监听端口为 `443`，并通过 `kubernetes.io/elb.pool-protocol: https` 转发到后端。
- 当前 Chart 默认将 `service.type` 设置为 `NodePort`，这更符合 CCE LoadBalancer Ingress 的常见要求。
- 公网入口应访问 `https://sep-rbs-server.cloud-data-dev.seer-group.com/`，不要使用 `http://...`。
- 如果 Ingress 层对外暴露 HTTPS，请先配置 `ingress.tls` 或对应的 ELB 证书设置，再进行外部访问验证。
- 如果 Ingress 控制器通过 HTTPS 转发到后端，请保持 Service 端口为 `8443`。
- 如果你的场景属于 CCE Turbo 透传并且要求 `ClusterIP`，请显式覆盖 `service.type`。
- 如果使用的 Ingress 控制器需要额外的后端协议注解，请在 `helm/values.yaml` 中补充对应配置。
- 当前默认配置下，公网访问地址是 `https://sep-rbs-server.cloud-data-dev.seer-group.com/`，Pod 内部依然通过 `8443` 提供 HTTPS 服务。

### 7）代码更新后的重新部署

- 只改业务代码时，建议构建新镜像、推送 SWR，然后执行 `helm upgrade --install` 并通过 `--set image.tag=<新版本>` 更新镜像标签。
- 同时改了 `helm/` 模板时，建议先执行 `helm lint` / `helm template`，再重新 `helm package` 生成新的 `.tgz` 包。
- 如果需要更换 HTTPS 证书或私钥，请区分 Ingress 前端证书和 Pod 内服务端证书，具体步骤见 `docs/helm_cce_deployment_notes_zh.md` 中的“证书 / 密钥更换流程”。
- 完整流程、回滚示例和常见问题见 [docs/helm_cce_deployment_notes_zh.md](docs/helm_cce_deployment_notes_zh.md)。

## 注意事项

- 生产环境请使用真实 CA 签发的证书。
- 请确保安全组 / 防火墙已放通 `8443/TCP`。
- 当前服务是一个轻量级示例，可按需扩展认证、路由与监控能力。
- Helm / CCE 部署问题记录与排障说明见 [docs/helm_cce_deployment_notes_zh.md](docs/helm_cce_deployment_notes_zh.md)。
