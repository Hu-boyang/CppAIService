# CppAIService

用 C++ 搭的 AI 应用服务：网页里登录、多轮对话、知识库问答、识图、语音合成。底层是 muduo HTTP 服务，对话和视觉走阿里云百炼，语音合成走百度 TTS，消息异步进 MySQL。

代码从 [youngyangyang04/CppAIService](https://github.com/youngyangyang04/CppAIService) 来，许可证同样是 GPL-3.0。原项目介绍在 [这里](https://programmercarl.com/other/project_http_ai2.html)。这个仓库能直接用 Docker 跑起来，并加了本地知识库和更顺手的多会话。

## 功能

- 通义千问对话；模型需要时会调工具：查本地知识库，或联网搜索
- 上传文档后做分块和 BM25 检索，带引用回答
- 一个账号多条会话，思考中途可以开新对话，侧边栏能看到进行中的会话
- 识图默认走百炼视觉模型；本机 OpenCV + ONNX 是可选编译项
- 百度 TTS
- RabbitMQ 把聊天记录异步写入 MySQL

## 怎么跑

先把密钥模板拷出来（填好的 `docker/app.env` 不要提交）：

```bash
cp docker/app.env.example docker/app.env
```

对话和识图需要 `DASHSCOPE_API_KEY`，语音合成还要百度的 `BAIDU_CLIENT_ID` / `BAIDU_CLIENT_SECRET`。

配环境和依赖（Docker 镜像、Conan、CMake preset）：

```bash
python3 scripts/setup_env.py
```

编译并拉起 MySQL、RabbitMQ 和 HTTP 服务（默认 `8116`）。仓库里带了 `CMakePresets.json`；第一次编译若还没有 Conan 工具链，会先跑环境配置：

```bash
python3 scripts/build.py
python3 scripts/restart.py ensure
```

浏览器打开 http://127.0.0.1:8116 。在 VS Code / Cursor 里直接跑「编译并启动容器」即可；首次打开仓库缺少依赖时会自动配置。也可以先单独跑「配置开发环境」。

如果要本机 ONNX 识图：

```bash
conan install . --build=missing -s build_type=Release -s compiler.cppstd=17 \
  -o with_opencv=True -o with_onnxruntime=True
cmake --preset conan-release
cmake --build --preset conan-release --target http_server
```

## 和原仓库的差异

原仓库按本机 apt / 固定 `.so` 路径编译，演示工具是查天气和当前时间。这边改成 Conan 管依赖，`docker-compose` 起 MySQL 8、RabbitMQ 3.13 和编译好的 `http_server`；工具换成知识库检索和联网搜索；识图默认走云端视觉接口。HTTP SSL 那一套和示例客户端没有带过来，服务目前只走明文 HTTP。密钥放环境变量，不写进源码。

## 许可证

[GPL-3.0](LICENSE)。
