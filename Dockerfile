FROM alpine:3.20

# Install GCC, standard tools, Python, and iverilog/verilator for real fallback compilation
RUN apk update && apk add --no-cache \
    build-base \
    g++ \
    python3 \
    py3-pip \
    bash \
    iverilog

WORKDIR /app

COPY requirements.txt .
RUN pip3 install --no-cache-dir --break-system-packages -r requirements.txt

COPY server.py .
COPY static ./static

EXPOSE 8080

CMD ["uvicorn", "server:app", "--host", "0.0.0.0", "--port", "8080"]