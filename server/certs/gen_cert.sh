#!/usr/bin/env bash
set -e
mkdir -p certs
openssl req -x509 -newkey rsa:4096 -keyout certs/server.key -out certs/server.crt \
    -sha256 -days 365 -nodes \
    -subj "/C=US/ST=Dev/L=Local/O=Loomic/CN=localhost" \
    -addext "subjectAltName=DNS:localhost,IP:127.0.0.1"
chmod 600 certs/server.key
echo "Dev cert written to certs/server.crt and certs/server.key"
