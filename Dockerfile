# 1. 使用 Ubuntu 22.04 (Jammy Jellyfish)
FROM ubuntu:22.04

# 2. 安装运行时依赖
# 注意：这里使用的是 libssl3 (22.04 特有)，而不是 libssl1.1
RUN apt-get update && DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends \
    # 字体 (防止中文乱码)
    fonts-wqy-zenhei fontconfig \
    # 图形界面基础库 (X11, OpenGL, DBus)
    libgl1 libxkbcommon-x11-0 \
    libxcb-icccm4 libxcb-image0 libxcb-keysyms1 libxcb-randr0 \
    libxcb-render-util0 libxcb-shape0 libxcb-xfixes0 \
    libxcb-xinerama0 libxcb-xkb1 libdbus-1-3 \
    libx11-xcb1 libxcb-cursor0 \
    # SSL 库 (22.04 必须是 libssl3) + 证书工具
    libssl3 ca-certificates openssl \
    # 其他常见依赖
    libxrender1 libxtst6 libegl1 \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app

# 3. 拷贝文件
# 编译好的程序、库、插件
COPY deploy/ ./
# 启动脚本
COPY scripts/ ./scripts/
# 证书文件夹 (如果存在)
COPY certs/ ./certs/

# 4. 赋予执行权限
RUN chmod +x ./DockerRoboshopServer ./AppRun ./scripts/*.sh

# 5. 环境变量配置
# 告诉 Qt 去哪里找平台插件 (非常关键，否则报 "could not load the Qt platform plugin 'xcb'")
ENV QT_QPA_PLATFORM_PLUGIN_PATH=/app/plugins
# 动态库路径
ENV LD_LIBRARY_PATH=/app/lib:$LD_LIBRARY_PATH

# 业务配置
ENV SERVER_HOST=0.0.0.0
ENV SERVER_PORT=8443
ENV SSL_CERT_FILE=/app/certs/server.crt
ENV SSL_KEY_FILE=/app/certs/server.key

# 6. 入口点
ENTRYPOINT ["/app/scripts/entrypoint.sh"]
