package server

import (
	"os"
	"path/filepath"
	"testing"
)

func TestConfigFromEnvLoadsTokenFile(t *testing.T) {
	tokenFile := filepath.Join(t.TempDir(), "token")
	if err := os.WriteFile(tokenFile, []byte("0123456789abcdef0123456789abcdef\n"), 0o600); err != nil {
		t.Fatal(err)
	}
	t.Setenv("FIRMWARE_SERVER_TOKEN_FILE", tokenFile)
	t.Setenv("FIRMWARE_SERVER_TOKEN", "ignored-token")
	t.Setenv("FIRMWARE_SERVER_PUBLIC_BASE_URL", "https://ota.example.com/greensync/ota/")

	cfg, err := ConfigFromEnv()
	if err != nil {
		t.Fatal(err)
	}
	if cfg.Token != "0123456789abcdef0123456789abcdef" {
		t.Fatalf("token = %q", cfg.Token)
	}
	if cfg.PublicBaseURL != "https://ota.example.com/greensync/ota" {
		t.Fatalf("public base URL = %q", cfg.PublicBaseURL)
	}
}

func TestConfigFromEnvRejectsInsecurePublicURL(t *testing.T) {
	t.Setenv("FIRMWARE_SERVER_TOKEN_FILE", "")
	t.Setenv("FIRMWARE_SERVER_TOKEN", "0123456789abcdef0123456789abcdef")
	t.Setenv("FIRMWARE_SERVER_PUBLIC_BASE_URL", "http://ota.example.com/greensync/ota")
	if _, err := ConfigFromEnv(); err == nil {
		t.Fatal("expected insecure URL to be rejected")
	}
}

func TestConfigFromEnvRequiresPublicBaseURL(t *testing.T) {
	t.Setenv("FIRMWARE_SERVER_TOKEN_FILE", "")
	t.Setenv("FIRMWARE_SERVER_TOKEN", "0123456789abcdef0123456789abcdef")
	t.Setenv("FIRMWARE_SERVER_PUBLIC_BASE_URL", "")
	if _, err := ConfigFromEnv(); err == nil {
		t.Fatal("expected public base URL to be required")
	}
}
