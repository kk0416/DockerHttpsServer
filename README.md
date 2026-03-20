# DockerRoboshopServer

Qt 6.7.x + MSVC2019 compatible HTTPS server project.

## Features

- HTTPS server based on `QTcpServer + QSslSocket`
- Endpoints:
  - `GET /`
  - `GET /health`
  - `GET /api/version`
  - `POST /api/echo`
- Keep-Alive + connection reuse for lower TLS handshake overhead
- High-concurrency controls: pending queue, listen backlog, active connection cap
- CMake project that can be built with Visual Studio 2019
- Docker packaging and Huawei Cloud SWR push helper script

## Project Structure

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

## Local Build (Qt 6.7.3 + VS2019)

Run in PowerShell:

```powershell
cd D:\QtDemo\2026\3\DockerRoboshopServer
D:/Qt/6.7.3/Tools/CMake_64/bin/cmake.exe -S . -B build-vs2019 -G "Visual Studio 16 2019" -A x64 -DCMAKE_PREFIX_PATH="D:/Qt/6.7.3/6.7.3/msvc2019_64"
cmake --build build-vs2019 --config Release
```

### Build with your vcvars + Ninja toolchain

```powershell
cmd.exe /c "\"D:\vs\2019\IDE\VC\Auxiliary\Build\vcvars64.bat\" && \"D:\Qt\6.7.3\Tools\CMake_64\bin\cmake.exe\" -S D:\QtDemo\2026\3\DockerRoboshopServer -B D:\QtDemo\2026\3\DockerRoboshopServer\build-msvc -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=D:/Qt/6.7.3/6.7.3/msvc2019_64 -DCMAKE_MAKE_PROGRAM=D:/Qt/6.7.3/Tools/Ninja/ninja.exe"
cmd.exe /c "\"D:\vs\2019\IDE\VC\Auxiliary\Build\vcvars64.bat\" && \"D:\Qt\6.7.3\Tools\CMake_64\bin\cmake.exe\" --build D:\QtDemo\2026\3\DockerRoboshopServer\build-msvc -j 8"
```

### Create local certificate

```powershell
cd D:\QtDemo\2026\3\DockerRoboshopServer
mkdir certs -Force
$env:OPENSSL_CONF="D:\Qt\6.7.3\Tools\OpenSSLv3\Win_x64\openssl.cnf"
D:/Qt/6.7.3/Tools/OpenSSLv3/Win_x64/bin/openssl.exe req -x509 -nodes -newkey rsa:2048 -keyout certs/server.key -out certs/server.crt -days 365 -subj "/CN=localhost"
```

### Run local executable

```powershell
.\build-vs2019\Release\DockerRoboshopServer.exe --host 0.0.0.0 --port 8443 --cert certs/server.crt --key certs/server.key --max-pending 4096 --listen-backlog 8192 --max-active 12000
```

### High-concurrency parameters

- CLI:
  - `--max-pending` (default: `1024`)
  - `--listen-backlog` (default: `2048`)
  - `--max-active` (default: `4096`)
- Environment:
  - `SERVER_MAX_PENDING`
  - `SERVER_LISTEN_BACKLOG`
  - `SERVER_MAX_ACTIVE`

### Test

```powershell
curl.exe -k https://127.0.0.1:8443/health
curl.exe -k -X POST https://127.0.0.1:8443/api/echo -H "Content-Type: application/json" -d "{\"hello\":\"qt6\"}"
```

## Docker Build and Run

```powershell
cd D:\QtDemo\2026\3\DockerRoboshopServer
docker build -t docker-roboshop-server:1.0.0 .
docker run --rm -p 8443:8443 docker-roboshop-server:1.0.0
```

If no cert is mounted, `scripts/entrypoint.sh` generates a self-signed cert automatically.

### Mount production certificate

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

## Deploy to Huawei Cloud (SWR + ECS)

### 1) Push image to SWR

Use helper script:

```bash
cd /path/to/DockerRoboshopServer
chmod +x scripts/push_to_huaweicloud.sh
./scripts/push_to_huaweicloud.sh cn-north-4 <namespace> <username> <password_or_token> docker-roboshop-server 1.0.0
```

Or manual:

```bash
docker login -u <username> -p <password_or_token> swr.cn-north-4.myhuaweicloud.com
docker build -t docker-roboshop-server:1.0.0 .
docker tag docker-roboshop-server:1.0.0 swr.cn-north-4.myhuaweicloud.com/<namespace>/docker-roboshop-server:1.0.0
docker push swr.cn-north-4.myhuaweicloud.com/<namespace>/docker-roboshop-server:1.0.0
```

### 2) Run on ECS

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

## Deploy to Kubernetes With Helm

The repository now contains a Helm chart in `helm/`.

### 1) Default chart behavior

- Default external URL: `https://sep-rbs-server.cloud-data-dev.seer-group.com/`
- `values.yaml` uses `ingress.publicUrl` as a full URL.
- The template automatically extracts the hostname part and writes it into `Ingress.spec.rules.host`.
- Container probes use `HTTPS` and check `/health`.
- The application listens on container port `8443`.

### 2) Install with default values

```bash
helm install roboshop-server ./helm
```

### 3) Install with SWR image

```bash
helm install roboshop-server ./helm \
  --set image.repository=swr.cn-north-4.myhuaweicloud.com/<namespace>/sep-rbs-server \
  --set image.tag=1.0.0
```

### 4) Mount backend server certificate from Kubernetes Secret

The server itself is an HTTPS service, so in Kubernetes you should provide a Secret containing:

- `server.crt`
- `server.key`

Example:

```bash
kubectl create secret generic roboshop-server-tls \
  --from-file=server.crt=/path/to/server.crt \
  --from-file=server.key=/path/to/server.key
```

Then install with:

```bash
helm install roboshop-server ./helm \
  --set image.repository=swr.cn-north-4.myhuaweicloud.com/<namespace>/sep-rbs-server \
  --set image.tag=1.0.0 \
  --set tls.existingSecret=roboshop-server-tls
```

### 5) Production values

```bash
helm install roboshop-server ./helm -f helm/values-production.yaml \
  --set image.repository=swr.cn-north-4.myhuaweicloud.com/<namespace>/sep-rbs-server \
  --set image.tag=1.0.0
```

Or upgrade:

```bash
helm upgrade --install roboshop-server ./helm -f helm/values-production.yaml \
  --set image.repository=swr.cn-north-4.myhuaweicloud.com/<namespace>/sep-rbs-server \
  --set image.tag=1.0.0
```

### 6) Ingress notes

- The chart now defaults to CCE HTTPS ingress on port `443` and forwards to the backend using `kubernetes.io/elb.pool-protocol: https`.
- The chart now defaults `service.type` to `NodePort`, which matches the common CCE LoadBalancer Ingress requirement.
- Access the public entry with `https://sep-rbs-server.cloud-data-dev.seer-group.com/`, not `http://...`.
- If you expose HTTPS at the ingress layer, configure `ingress.tls` or the corresponding ELB certificate settings before expecting external access to work.
- If your ingress controller talks to the backend over HTTPS, keep the Service port as `8443`.
- If you are in a CCE Turbo passthrough scenario that requires `ClusterIP`, override `service.type` explicitly.
- If your ingress controller requires an explicit backend protocol annotation, add the controller-specific annotation in `helm/values.yaml`.
- The chart default keeps the public access URL as `https://sep-rbs-server.cloud-data-dev.seer-group.com/`, and the Pod still serves HTTPS internally on port `8443`.

## Notes

- Use real CA certificate in production.
- Keep security group / firewall rule open for `8443/TCP`.
- This server is a compact demo and can be extended with auth, routing, and metrics.
