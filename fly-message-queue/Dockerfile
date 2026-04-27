# Build stage
FROM golang:1.26-alpine AS build
WORKDIR /src
COPY go.mod go.sum ./
RUN go mod download
COPY . .
RUN CGO_ENABLED=0 go build -ldflags="-s -w" -o /out/fly-message-queue ./

# Runtime stage
FROM gcr.io/distroless/static-debian12
WORKDIR /app
COPY --from=build /out/fly-message-queue /app/fly-message-queue
COPY templates /app/templates
ENV DB_PATH=/data/queue.db
ENV LISTEN_ADDR=:8080
EXPOSE 8080
ENTRYPOINT ["/app/fly-message-queue"]
