# Helm / CCE 部署问题记录与后续操作说明

## 1. 文档目的

本文档用于记录 `DockerRoboshopServer` 在华为云 CCE 环境中通过 Helm 部署时遇到的问题、排查过程、解决办法、最终配置以及后续待办事项。

适用对象：

- 后续继续维护 `sep-rbs-server` Helm chart 的开发或运维人员
- 需要在 `roboshop` 命名空间重新部署该服务的同事

## 2. 当前部署结果

当前服务已经成功部署并验证可访问，部署状态如下：

- Helm Release：`sep-rbs-server-v1`
- Namespace：`roboshop`
- 域名：`https://sep-rbs-server.cloud-data-dev.seer-group.com/`
- 镜像：`swr.cn-north-4.myhuaweicloud.com/seer_develop/sep-rbs-server:1.0.0`
- Service 类型：`NodePort`
- Service 名称：`sep-rbs-server-v1`
- Ingress 类型：`cce`
- Ingress 绑定 ELB：`32bb8857-86a4-475b-b80a-f650ab200a8a`
- Ingress 内网地址：`192.168.0.154`
- 域名当前解析到的公网 IPv4：`124.71.231.189`
- Ingress TLS Secret：`sep-rbs-server-ingress-tls`
- Image Pull Secret：`default-secret`

实际验证命令：

```powershell
curl.exe -vk https://sep-rbs-server.cloud-data-dev.seer-group.com/health
```

实际返回结果：

```json
{"service":"DockerRoboshopServer","status":"ok","timeUtc":"2026-03-20T06:59:44Z"}
```

通过公网 IP + Host 头测试的命令：

```powershell
curl.exe -vk https://124.71.231.189:443/health -H "Host: sep-rbs-server.cloud-data-dev.seer-group.com"
```

测试结果：

- 返回 `200 OK`
- 说明共享 ELB 可以通过 Host 规则将请求转发到当前服务

## 3. 本次部署中遇到的问题与解决办法

### 3.1 `kubectl` 无法连接集群

现象：

```text
The connection to the server localhost:8080 was refused
```

原因：

- 当前机器没有可用的 `kubeconfig`
- `kubectl config get-contexts` 为空
- `kubectl` 回退去访问本地默认地址 `localhost:8080`

解决办法：

1. 从华为云 CCE 控制台下载目标集群的 `kubeconfig`
2. 配置到本机后，确认当前 context 可用

常用排查命令：

```powershell
kubectl config get-contexts
kubectl config current-context
kubectl cluster-info
kubectl get nodes
```

最终确认结果：

- 当前 context：`external`
- 控制平面地址：`https://122.9.34.242:5443`

### 3.2 访问 `http://sep-rbs-server.cloud-data-dev.seer-group.com/` 返回 404

现象：

- 域名访问返回 `404`
- 但源码中 `GET /` 与 `GET /health` 都会返回 `200`

原因：

- 原始 Helm 配置里，Ingress 前端还是 `HTTP/80`
- 后端 Service / Pod 实际提供的是 `HTTPS/8443`
- CCE Ingress 没有按 HTTPS backend 正确代理

解决办法：

将 Helm 默认值调整为适合 CCE 的 HTTPS 配置：

- `ingress.publicUrl` 改为 `https://sep-rbs-server.cloud-data-dev.seer-group.com/`
- `kubernetes.io/elb.port` 改为 `443`
- 增加 `kubernetes.io/elb.pool-protocol: https`
- `service.type` 改为 `NodePort`
- 路径匹配改为：
  - `pathType: ImplementationSpecific`
  - `ingress.beta.kubernetes.io/url-match-mode: STARTS_WITH`

### 3.3 `only https listener support https pool protocol`

现象：

执行 Helm 升级时报错：

```text
admission webhook "validate.crd.ingress" denied the request:
only https listener support https pool protocol
```

原因：

- 旧的 Ingress 已经按 `HTTP listener` 方式创建
- 新配置尝试直接通过 `helm upgrade` 将旧 Ingress patch 为 `HTTPS listener + HTTPS backend`
- CCE webhook 不允许这样直接变更

解决办法：

不要继续对旧资源做 `upgrade patch`，而是：

1. 卸载旧 release
2. 清理残留的 Ingress / Service / Deployment
3. 重新执行 `helm install`

实际处理命令：

```powershell
helm uninstall sep-rbs-server -n roboshop
helm uninstall sep-rbs-server-v1 -n roboshop
kubectl delete svc sep-rbs-server -n roboshop --ignore-not-found
kubectl delete svc sep-rbs-server-v1 -n roboshop --ignore-not-found
kubectl delete deploy sep-rbs-server -n roboshop --ignore-not-found
kubectl delete deploy sep-rbs-server-v1 -n roboshop --ignore-not-found
kubectl delete ingress sep-rbs-server -n roboshop --ignore-not-found
kubectl delete ingress sep-rbs-server-v1 -n roboshop --ignore-not-found
```

### 3.4 Ingress 后端 Service 不存在

现象：

`kubectl describe ingress sep-rbs-server-v1 -n roboshop` 显示：

```text
services "sep-rbs-server-v1" not found
```

原因：

- 旧 release 状态异常
- Ingress 仍然存在，但对应 Service 已经被删掉或没有正确创建

解决办法：

- 清理旧 release
- 重新安装最新 chart

重新安装后确认：

```powershell
kubectl get svc -n roboshop
kubectl describe ingress sep-rbs-server-v1 -n roboshop
```

结果应满足：

- `svc/sep-rbs-server-v1` 存在
- Ingress backend 指向 `sep-rbs-server-v1:8443`

### 3.5 Ingress 没有分配地址

现象：

```text
There has no elb ip and elb id been setted
```

表现：

- Ingress `ADDRESS` 为空
- 无法从公网访问

原因：

- 当前集群的 `cce` Ingress 实际依赖共享 ELB
- 正常工作的 Ingress 都显式带有 `kubernetes.io/elb.id`
- 新建的 `sep-rbs-server-v1` 没有配置该注解

解决办法：

复用当前集群里正在使用的共享 ELB：

- ELB ID：`32bb8857-86a4-475b-b80a-f650ab200a8a`

将该值写入 Helm values：

```yaml
ingress:
  annotations:
    kubernetes.io/elb.id: "32bb8857-86a4-475b-b80a-f650ab200a8a"
```

并同步写入 chart 默认值，避免下次升级时丢失。

最终结果：

- Ingress `ADDRESS` 成功变为 `192.168.0.154`

### 3.6 缺少前端 HTTPS 证书 Secret

现象：

执行：

```powershell
kubectl get secret -n roboshop | findstr sep-rbs-server-ingress-tls
```

返回为空。

原因：

- Helm values 默认已经引用了 `sep-rbs-server-ingress-tls`
- 但 `roboshop` 命名空间中并不存在该 TLS Secret

解决办法：

手工创建前端证书 Secret：

```powershell
kubectl create secret tls sep-rbs-server-ingress-tls `
  --cert=.\tls.crt `
  --key=.\tls.key `
  -n roboshop
```

说明：

- 这里使用的是本地通过 `openssl` 生成的自签名证书
- 这可以让功能打通，但浏览器会提示证书不受信任

### 3.7 镜像拉取 Secret 如何确认

现象：

需要确认是否已有私有 SWR 拉镜像凭证。

排查方式：

```powershell
kubectl get secret -n roboshop -o custom-columns=NAME:.metadata.name,TYPE:.type
kubectl get secret -n roboshop | findstr default-secret
```

结果：

- `default-secret` 已存在
- 类型为 `kubernetes.io/dockerconfigjson`

解决办法：

将 Helm 默认值设置为：

```yaml
imagePullSecrets:
  - name: default-secret
```

## 4. 最终落地的 Helm 关键配置

关键文件：

- [helm/values.yaml](/D:/docker-images/DockerRoboshopServer/DockerRoboshopServer/helm/values.yaml)
- [helm/values-production.yaml](/D:/docker-images/DockerRoboshopServer/DockerRoboshopServer/helm/values-production.yaml)
- [helm/templates/ingress.yaml](/D:/docker-images/DockerRoboshopServer/DockerRoboshopServer/helm/templates/ingress.yaml)

当前关键配置如下：

```yaml
imagePullSecrets:
  - name: default-secret

service:
  type: NodePort
  port: 8443
  targetPort: 8443

ingress:
  enabled: true
  className: cce
  publicUrl: https://sep-rbs-server.cloud-data-dev.seer-group.com/
  annotations:
    kubernetes.io/ingress.class: cce
    kubernetes.io/elb.class: performance
    kubernetes.io/elb.id: "32bb8857-86a4-475b-b80a-f650ab200a8a"
    kubernetes.io/elb.port: "443"
    kubernetes.io/elb.pool-protocol: "https"
    kubernetes.io/elb.lb-algorithm: ROUND_ROBIN
    kubernetes.io/elb.client_timeout: "300"
    kubernetes.io/elb.member_timeout: "300"
    kubernetes.io/elb.keepalive_timeout: "300"
  paths:
    - path: /
      property:
        ingress.beta.kubernetes.io/url-match-mode: STARTS_WITH
      pathType: ImplementationSpecific
  tls:
    - secretName: sep-rbs-server-ingress-tls
      hosts:
        - sep-rbs-server.cloud-data-dev.seer-group.com
```

## 5. 当前推荐部署步骤

### 5.1 确认集群连接正常

```powershell
kubectl cluster-info
kubectl get nodes
```

### 5.2 确认命名空间中的依赖 Secret

```powershell
kubectl get secret -n roboshop | findstr default-secret
kubectl get secret -n roboshop | findstr sep-rbs-server-ingress-tls
```

### 5.3 使用 Helm 包安装或升级

当前打包产物：

- [sep-rbs-server-1.0.0.tgz](/D:/docker-images/DockerRoboshopServer/DockerRoboshopServer/sep-rbs-server-1.0.0.tgz)

部署命令：

```powershell
helm upgrade --install sep-rbs-server-v1 D:\docker-images\DockerRoboshopServer\DockerRoboshopServer\sep-rbs-server-1.0.0.tgz `
  -n roboshop `
  --create-namespace
```

### 5.4 部署后检查

```powershell
helm list -n roboshop
kubectl get svc -n roboshop
kubectl get ingress -n roboshop
kubectl describe ingress sep-rbs-server-v1 -n roboshop
kubectl get pods -n roboshop
```

### 5.5 连通性验证

```powershell
curl.exe -vk https://sep-rbs-server.cloud-data-dev.seer-group.com/health
curl.exe -vk https://sep-rbs-server.cloud-data-dev.seer-group.com/
```

如果需要绕过 DNS，直接验证公网 ELB IP 是否可转发到服务，可使用：

```powershell
curl.exe -vk https://124.71.231.189:443/health -H "Host: sep-rbs-server.cloud-data-dev.seer-group.com"
curl.exe -vk https://124.71.231.189:443/api/version -H "Host: sep-rbs-server.cloud-data-dev.seer-group.com"
```

说明：

- 由于 Ingress 规则是按 `host` 匹配的，因此直接裸访问 `https://124.71.231.189:443` 并不可靠。
- 如果使用公网 IP 进行测试，应显式带上 `Host: sep-rbs-server.cloud-data-dev.seer-group.com` 请求头。
- 对外正式使用时，仍然建议统一通过域名访问，而不是通过公网 IP 访问。

### 5.6 代码修改后的更新与重新部署流程

后续日常发版建议固定按下面流程执行，重点是：

- 镜像标签不要复用，建议每次发版都递增，例如：`1.0.1`、`1.0.2`
- 如果只改了业务代码，没有改 Helm 模板，通常只需要更新镜像并执行 `helm upgrade`
- 如果改了 `helm/` 下的模板或默认值，除了升级，还应重新打包 chart

#### 5.6.1 只修改代码，未修改 Helm 配置

1. 本地完成代码修改并构建
2. 构建新镜像
3. 推送到华为云 SWR
4. 使用新镜像标签执行 Helm 升级
5. 检查 Pod 滚动状态并做接口验证

示例命令：

```powershell
$tag = "1.0.1"

docker build -t sep-rbs-server:$tag .
docker tag sep-rbs-server:$tag swr.cn-north-4.myhuaweicloud.com/seer_develop/sep-rbs-server:$tag
docker push swr.cn-north-4.myhuaweicloud.com/seer_develop/sep-rbs-server:$tag

helm upgrade --install sep-rbs-server-v1 .\helm `
  -n roboshop `
  --create-namespace `
  --set image.repository=swr.cn-north-4.myhuaweicloud.com/seer_develop/sep-rbs-server `
  --set image.tag=$tag

kubectl rollout status deployment/sep-rbs-server-v1 -n roboshop
curl.exe -vk https://sep-rbs-server.cloud-data-dev.seer-group.com/health
curl.exe -vk https://sep-rbs-server.cloud-data-dev.seer-group.com/api/version
```

#### 5.6.2 同时修改了 Helm 配置

如果修改了下面这些内容，就属于 Helm 配置变更：

- `helm/values.yaml`
- `helm/values-production.yaml`
- `helm/templates/*.yaml`
- `helm/Chart.yaml`

建议流程：

1. 先本地渲染或检查 chart
2. 重新打包新的 chart
3. 再执行升级
4. 如果要上传华为云模板市场，使用新的 `.tgz` 文件

示例命令：

```powershell
$tag = "1.0.1"
$chartVersion = "1.0.1"

helm lint .\helm
helm template sep-rbs-server-v1 .\helm -n roboshop > .\helm-rendered.yaml

docker build -t sep-rbs-server:$tag .
docker tag sep-rbs-server:$tag swr.cn-north-4.myhuaweicloud.com/seer_develop/sep-rbs-server:$tag
docker push swr.cn-north-4.myhuaweicloud.com/seer_develop/sep-rbs-server:$tag

helm package .\helm `
  --version $chartVersion `
  --app-version $tag `
  -d .

helm upgrade --install sep-rbs-server-v1 .\sep-rbs-server-$chartVersion.tgz `
  -n roboshop `
  --create-namespace `
  --set image.repository=swr.cn-north-4.myhuaweicloud.com/seer_develop/sep-rbs-server `
  --set image.tag=$tag
```

说明：

- `image.tag` 用于控制容器镜像版本
- `Chart.yaml` 的 `version` 用于 chart 包版本
- 如果只是代码更新，不一定要同步修改 chart 版本
- 如果要重新上传 `.tgz` 到平台，建议同步提升 chart 版本，避免覆盖混淆

#### 5.6.3 部署后的最小检查项

```powershell
helm list -n roboshop
helm history sep-rbs-server-v1 -n roboshop
kubectl get pods -n roboshop -o wide
kubectl get svc -n roboshop
kubectl get ingress -n roboshop
kubectl rollout status deployment/sep-rbs-server-v1 -n roboshop
curl.exe -vk https://sep-rbs-server.cloud-data-dev.seer-group.com/health
curl.exe -vk https://sep-rbs-server.cloud-data-dev.seer-group.com/api/version
```

建议至少确认：

- 新 Pod 已经替换旧 Pod
- `READY` 正常
- Ingress 规则没有被改坏
- `/health` 返回 `200`
- `/api/version` 返回的版本信息符合预期

#### 5.6.4 回滚流程

如果升级后发现异常，可以直接回滚到上一版：

```powershell
helm history sep-rbs-server-v1 -n roboshop
helm rollback sep-rbs-server-v1 1 -n roboshop
kubectl rollout status deployment/sep-rbs-server-v1 -n roboshop
```

说明：

- `1` 只是示例 revision，实际回滚时请先看 `helm history`
- 如果只是镜像版本有问题，回滚 Helm release 通常是最快的恢复方式
- 回滚后仍建议再次执行 `/health` 与 `/api/version` 验证

### 5.7 证书 / 密钥更换流程

当前部署里有两套 TLS 配置，替换时不要混淆：

- Ingress 前端证书：浏览器访问 `https://sep-rbs-server.cloud-data-dev.seer-group.com/` 时看到的证书
- 后端服务证书：Pod 内部应用监听 `8443` 时使用的 `server.crt` / `server.key`

当前默认情况：

- Ingress 前端证书 Secret：`sep-rbs-server-ingress-tls`
- 后端服务证书 Secret：默认未启用 `tls.existingSecret`

#### 5.7.1 更换 Ingress 前端证书，但 Secret 名不变

这是最简单、最推荐的做法。

如果仍然使用 `sep-rbs-server-ingress-tls` 这个名字，通常不需要修改 Helm 配置，只需要覆盖 Secret 内容：

```powershell
kubectl create secret tls sep-rbs-server-ingress-tls `
  --cert=.\new\tls.crt `
  --key=.\new\tls.key `
  -n roboshop `
  --dry-run=client -o yaml | kubectl apply -f -
```

说明：

- `tls.crt` 和 `tls.key` 必须是一对
- 证书里的域名应包含 `sep-rbs-server.cloud-data-dev.seer-group.com`
- 这种方式不改 Secret 名，因此 Helm values 一般不用修改

更换后建议验证：

```powershell
kubectl get secret sep-rbs-server-ingress-tls -n roboshop
kubectl describe ingress sep-rbs-server-v1 -n roboshop
curl.exe -vk https://sep-rbs-server.cloud-data-dev.seer-group.com/health
```

#### 5.7.2 更换 Ingress 前端证书，并改成新的 Secret 名

如果你想把 Secret 名从 `sep-rbs-server-ingress-tls` 改成其他名字，例如 `sep-rbs-server-ingress-tls-v2`，则需要两步：

1. 先创建新的 TLS Secret
2. 再修改 Helm 配置中的 `ingress.tls[].secretName`

创建新 Secret：

```powershell
kubectl create secret tls sep-rbs-server-ingress-tls-v2 `
  --cert=.\new\tls.crt `
  --key=.\new\tls.key `
  -n roboshop
```

临时修改方式：

```powershell
helm upgrade --install sep-rbs-server-v1 .\sep-rbs-server-1.0.0.tgz `
  -n roboshop `
  --set ingress.tls[0].secretName=sep-rbs-server-ingress-tls-v2
```

长期修改方式：

- 修改 [helm/values.yaml](/D:/docker-images/DockerRoboshopServer/DockerRoboshopServer/helm/values.yaml)
- 修改 [helm/values-production.yaml](/D:/docker-images/DockerRoboshopServer/DockerRoboshopServer/helm/values-production.yaml)
- 如果你是通过 `.tgz` 包部署，还需要重新执行 `helm package`

#### 5.7.3 更换后端服务证书

如果要替换 Pod 内部应用使用的 `server.crt` / `server.key`，需要启用 `tls.existingSecret`。

先创建或更新后端证书 Secret：

```powershell
kubectl create secret generic sep-rbs-server-backend-tls `
  --from-file=server.crt=.\server.crt `
  --from-file=server.key=.\server.key `
  -n roboshop `
  --dry-run=client -o yaml | kubectl apply -f -
```

然后执行 Helm 升级：

```powershell
helm upgrade --install sep-rbs-server-v1 .\sep-rbs-server-1.0.0.tgz `
  -n roboshop `
  --set tls.existingSecret=sep-rbs-server-backend-tls
```

如果 Secret 中的键名不是 `server.crt` 和 `server.key`，还要补充：

```powershell
helm upgrade --install sep-rbs-server-v1 .\sep-rbs-server-1.0.0.tgz `
  -n roboshop `
  --set tls.existingSecret=sep-rbs-server-backend-tls `
  --set tls.secretKeys.cert=<你的crt键名> `
  --set tls.secretKeys.key=<你的key键名>
```

说明：

- 当前模板会把该 Secret 挂载到容器内 `/app/certs`
- 对应逻辑见 [helm/templates/deployment.yaml](/D:/docker-images/DockerRoboshopServer/DockerRoboshopServer/helm/templates/deployment.yaml)
- 容器环境变量中的证书路径默认是 `/app/certs/server.crt` 和 `/app/certs/server.key`

#### 5.7.4 更换完成后的建议检查项

```powershell
kubectl get secret -n roboshop
kubectl get pods -n roboshop
kubectl rollout status deployment/sep-rbs-server-v1 -n roboshop
kubectl describe ingress sep-rbs-server-v1 -n roboshop
curl.exe -vk https://sep-rbs-server.cloud-data-dev.seer-group.com/health
curl.exe -vk https://sep-rbs-server.cloud-data-dev.seer-group.com/api/version
```

建议重点确认：

- Ingress 使用的 Secret 名是否正确
- Pod 是否已经完成滚动更新
- 服务是否仍然返回 `200`
- 浏览器或 `curl` 返回的证书是否已经是新证书

## 6. 以后还需要做的事情

### 6.1 替换自签名证书

当前前端证书使用的是本地 `openssl` 自签名证书，只适合测试环境。

后续建议：

- 申请正式 CA 证书
- 重新创建 `sep-rbs-server-ingress-tls`
- 验证浏览器不再出现证书警告

### 6.2 将 ELB ID 参数化

当前 chart 中已经写死：

```yaml
kubernetes.io/elb.id: "32bb8857-86a4-475b-b80a-f650ab200a8a"
```

这能保证当前环境稳定工作，但会导致：

- 跨环境复用不方便
- 其他环境可能并不存在同一个 ELB

后续建议：

- 将 `elb.id` 提取为显式 values 参数
- 为不同环境维护独立的 values 文件

### 6.3 补充 CI 检查

当前已经可以在发布机器上执行 `helm upgrade --install`，但尚未把 `helm lint` 与 `helm template` 纳入自动化校验。

后续建议：

- 在 CI 中增加 `helm template` / `helm lint`
- 对 `values.yaml` 与 `values-production.yaml` 做静态校验

### 6.4 整理环境差异

建议将下面信息按环境区分：

- 证书 Secret 名称
- ELB ID
- 域名
- 镜像仓库
- 副本数与资源限制

可选做法：

- 保留通用 `values.yaml`
- 为 `dev` / `test` / `prod` 分别维护覆盖文件

### 6.5 补充清理与回滚说明

后续建议补充一个简单运维脚本或文档，至少包括：

- 卸载旧 release 的步骤
- 清理残留 Service / Ingress / Deployment 的步骤
- 快速回滚到上一版 chart 的方式

## 7. 参考命令速查

### 查看当前发布状态

```powershell
helm list -n roboshop
kubectl get all -n roboshop
kubectl get ingress -n roboshop
```

### 查看服务日志

```powershell
kubectl logs -n roboshop deploy/sep-rbs-server-v1
```

### 查看 Ingress 详情

```powershell
kubectl describe ingress sep-rbs-server-v1 -n roboshop
kubectl get ingress sep-rbs-server-v1 -n roboshop -o yaml
```

### 查看 Secret

```powershell
kubectl get secret -n roboshop
kubectl get secret -n roboshop | findstr tls
kubectl get secret -n roboshop | findstr default-secret
```
