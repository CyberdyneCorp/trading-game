# Build context: repository root (docker build -f services/api/Dockerfile .)
FROM debian:trixie-slim AS engine
RUN apt-get update && apt-get install -y --no-install-recommends cmake ninja-build clang && rm -rf /var/lib/apt/lists/*
WORKDIR /src
COPY engine engine
RUN CC=clang CXX=clang++ cmake -S engine -B engine/build/release -G Ninja -DCMAKE_BUILD_TYPE=Release -DTRADELAB_BUILD_TESTS=OFF \
 && cmake --build engine/build/release --target tradelab_capi

FROM python:3.12-slim-trixie
COPY --from=ghcr.io/astral-sh/uv:0.5.11 /uv /usr/local/bin/uv
WORKDIR /app
COPY bindings/python /app/bindings/python
COPY services/api /app/services/api
COPY --from=engine /src/engine/build/release/libtradelab.so /usr/local/lib/libtradelab.so
ENV TRADELAB_LIB=/usr/local/lib/libtradelab.so UV_COMPILE_BYTECODE=1
WORKDIR /app/services/api
RUN uv sync --frozen --no-dev --python 3.12
EXPOSE 8000
USER nobody
CMD ["/app/services/api/.venv/bin/uvicorn", "tradelab_api.main:app", "--host", "0.0.0.0", "--port", "8000", "--proxy-headers"]
