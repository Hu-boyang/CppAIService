FROM ubuntu:26.04

ENV DEBIAN_FRONTEND=noninteractive \
    HTTP_PORT=8116 \
    CHAT_RESOURCE_DIR=/app/AIApps/ChatServer/resource \
    MYSQL_HOST=mysql \
    MYSQL_PORT=3306 \
    MYSQL_USER=root \
    MYSQL_PASSWORD=123456 \
    MYSQL_DATABASE=ChatHttpServer \
    RABBITMQ_HOST=rabbitmq \
    RABBITMQ_PORT=5672 \
    RABBITMQ_USER=guest \
    RABBITMQ_PASSWORD=guest

RUN apt-get update \
    && apt-get install -y --no-install-recommends ca-certificates libstdc++6 python3 \
    && rm -rf /var/lib/apt/lists/* \
    && printf 'precedence ::ffff:0:0/96  100\n' >> /etc/gai.conf

ARG BINARY_MTIME=0
RUN echo "http_server mtime ${BINARY_MTIME}"

WORKDIR /app/build

COPY build/Release/http_server /app/build/http_server
COPY AIApps/ChatServer/resource /app/AIApps/ChatServer/resource
COPY scripts/entrypoint.py /entrypoint.py

RUN chmod +x /app/build/http_server /entrypoint.py

EXPOSE 8116

ENTRYPOINT ["python3", "/entrypoint.py"]
