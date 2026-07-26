package server

import (
	"errors"
	"fmt"
	"os"
	"strconv"
	"strings"
)

const defaultMaxUploadBytes int64 = 4 * 1024 * 1024

type Config struct {
	Addr          string
	DataDir       string
	PathPrefix    string
	PublicBaseURL string
	Token         string
	MaxUploadSize int64
	TLSCertFile   string
	TLSKeyFile    string
}

func ConfigFromEnv() (Config, error) {
	cfg := Config{
		Addr:          valueOrDefault("FIRMWARE_SERVER_ADDR", ":8080"),
		DataDir:       valueOrDefault("FIRMWARE_SERVER_DATA_DIR", "/data"),
		PathPrefix:    valueOrDefault("FIRMWARE_SERVER_PATH_PREFIX", "/greensync/ota"),
		PublicBaseURL: strings.TrimRight(os.Getenv("FIRMWARE_SERVER_PUBLIC_BASE_URL"), "/"),
		MaxUploadSize: defaultMaxUploadBytes,
		TLSCertFile:   os.Getenv("FIRMWARE_SERVER_TLS_CERT_FILE"),
		TLSKeyFile:    os.Getenv("FIRMWARE_SERVER_TLS_KEY_FILE"),
	}

	if raw := os.Getenv("FIRMWARE_SERVER_MAX_UPLOAD_BYTES"); raw != "" {
		value, err := strconv.ParseInt(raw, 10, 64)
		if err != nil || value <= 0 {
			return Config{}, fmt.Errorf("invalid FIRMWARE_SERVER_MAX_UPLOAD_BYTES: %q", raw)
		}
		cfg.MaxUploadSize = value
	}

	token, err := loadToken()
	if err != nil {
		return Config{}, err
	}
	cfg.Token = token

	if !strings.HasPrefix(cfg.PathPrefix, "/") || strings.Contains(cfg.PathPrefix, "..") {
		return Config{}, errors.New("FIRMWARE_SERVER_PATH_PREFIX must be an absolute URL path")
	}
	cfg.PathPrefix = strings.TrimRight(cfg.PathPrefix, "/")
	if cfg.PathPrefix == "" {
		return Config{}, errors.New("FIRMWARE_SERVER_PATH_PREFIX must not be root")
	}

	if cfg.PublicBaseURL == "" {
		return Config{}, errors.New("FIRMWARE_SERVER_PUBLIC_BASE_URL is required")
	}
	if !strings.HasPrefix(cfg.PublicBaseURL, "https://") {
		return Config{}, errors.New("FIRMWARE_SERVER_PUBLIC_BASE_URL must use HTTPS")
	}
	if (cfg.TLSCertFile == "") != (cfg.TLSKeyFile == "") {
		return Config{}, errors.New("both TLS certificate and key files must be configured")
	}

	return cfg, nil
}

func loadToken() (string, error) {
	if path := os.Getenv("FIRMWARE_SERVER_TOKEN_FILE"); path != "" {
		data, err := os.ReadFile(path)
		if err != nil {
			return "", fmt.Errorf("read deployment token: %w", err)
		}
		if token := strings.TrimSpace(string(data)); token != "" {
			return validateToken(token)
		}
		return "", errors.New("deployment token file is empty")
	}

	if token := strings.TrimSpace(os.Getenv("FIRMWARE_SERVER_TOKEN")); token != "" {
		return validateToken(token)
	}
	return "", errors.New("FIRMWARE_SERVER_TOKEN_FILE or FIRMWARE_SERVER_TOKEN is required")
}

func validateToken(token string) (string, error) {
	if len(token) < 32 {
		return "", errors.New("deployment token must be at least 32 characters")
	}
	return token, nil
}

func valueOrDefault(name, fallback string) string {
	if value := os.Getenv(name); value != "" {
		return value
	}
	return fallback
}
