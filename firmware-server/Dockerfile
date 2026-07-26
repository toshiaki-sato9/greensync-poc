FROM golang:1.24-alpine AS build

WORKDIR /src
COPY go.mod ./
COPY cmd ./cmd
COPY internal ./internal
RUN CGO_ENABLED=0 GOOS=linux go build -trimpath -ldflags="-s -w" -o /out/firmware-server ./cmd/server

FROM alpine:3.21

RUN apk add --no-cache ca-certificates \
    && addgroup -S firmware \
    && adduser -S -G firmware -h /app firmware \
    && install -d -o firmware -g firmware -m 0750 /data

COPY --from=build /out/firmware-server /usr/local/bin/firmware-server

USER firmware
EXPOSE 8443
VOLUME ["/data"]
ENTRYPOINT ["/usr/local/bin/firmware-server"]
