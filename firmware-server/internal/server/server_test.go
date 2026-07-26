package server

import (
	"bytes"
	"crypto/sha256"
	"encoding/hex"
	"encoding/json"
	"io"
	"log/slog"
	"mime/multipart"
	"net/http"
	"net/http/httptest"
	"net/textproto"
	"strconv"
	"strings"
	"testing"
)

const testToken = "0123456789abcdef0123456789abcdef"

func TestUploadAndDownloadRelease(t *testing.T) {
	api := newTestAPI(t, 1024)
	firmware := append([]byte{0xE9}, bytes.Repeat([]byte{0x42}, 127)...)

	response := upload(t, api, firmware, uploadOptions{})
	if response.Code != http.StatusCreated {
		t.Fatalf("upload status = %d, body = %s", response.Code, response.Body.String())
	}

	var result uploadResponse
	decodeJSON(t, response.Body.Bytes(), &result)
	if result.Status != "published" {
		t.Fatalf("status = %q", result.Status)
	}
	if result.Manifest.Hardware != "m5stack-atoms3-lite" || result.Manifest.Version != "0.3.0" {
		t.Fatalf("unexpected manifest: %+v", result.Manifest)
	}
	if result.Manifest.URL != "https://ota.test/greensync/ota/api/v1/releases/m5stack-atoms3-lite/0.3.0/firmware.bin" {
		t.Fatalf("manifest URL = %q", result.Manifest.URL)
	}

	assertDownload(t, api, "/greensync/ota/api/v1/releases/m5stack-atoms3-lite/0.3.0/firmware.bin", firmware, "public, max-age=31536000, immutable")
	channel := assertDownload(t, api, "/greensync/ota/api/v1/channels/m5stack-atoms3-lite/stable/manifest.json", nil, "no-store")
	var channelManifest Manifest
	decodeJSON(t, channel, &channelManifest)
	if channelManifest.SHA256 != result.Manifest.SHA256 {
		t.Fatalf("channel sha256 = %q, release sha256 = %q", channelManifest.SHA256, result.Manifest.SHA256)
	}
}

func TestUploadRequiresAuthentication(t *testing.T) {
	api := newTestAPI(t, 1024)
	response := upload(t, api, []byte{0xE9, 0x01}, uploadOptions{token: "wrong-token"})
	if response.Code != http.StatusUnauthorized {
		t.Fatalf("status = %d", response.Code)
	}
}

func TestUploadRejectsHashMismatch(t *testing.T) {
	api := newTestAPI(t, 1024)
	response := upload(t, api, []byte{0xE9, 0x01}, uploadOptions{sha256: strings.Repeat("0", 64)})
	if response.Code != http.StatusBadRequest || !strings.Contains(response.Body.String(), "sha256") {
		t.Fatalf("status = %d, body = %s", response.Code, response.Body.String())
	}
}

func TestUploadRejectsNonESPImage(t *testing.T) {
	api := newTestAPI(t, 1024)
	response := upload(t, api, []byte{0x00, 0x01}, uploadOptions{})
	if response.Code != http.StatusBadRequest || !strings.Contains(response.Body.String(), "ESP application image") {
		t.Fatalf("status = %d, body = %s", response.Code, response.Body.String())
	}
}

func TestUploadRejectsOversizedImage(t *testing.T) {
	api := newTestAPI(t, 4)
	response := upload(t, api, []byte{0xE9, 1, 2, 3, 4}, uploadOptions{})
	if response.Code != http.StatusBadRequest {
		t.Fatalf("status = %d, body = %s", response.Code, response.Body.String())
	}
}

func TestReleaseIsImmutable(t *testing.T) {
	api := newTestAPI(t, 1024)
	firmware := []byte{0xE9, 0x01}
	if response := upload(t, api, firmware, uploadOptions{}); response.Code != http.StatusCreated {
		t.Fatalf("first upload status = %d", response.Code)
	}
	response := upload(t, api, firmware, uploadOptions{})
	if response.Code != http.StatusConflict {
		t.Fatalf("second upload status = %d, body = %s", response.Code, response.Body.String())
	}
}

func TestUploadRejectsInvalidHardwareAndFilename(t *testing.T) {
	api := newTestAPI(t, 1024)
	firmware := []byte{0xE9, 0x01}
	if response := upload(t, api, firmware, uploadOptions{hardware: "../escape"}); response.Code != http.StatusBadRequest {
		t.Fatalf("invalid hardware status = %d", response.Code)
	}
	if response := upload(t, api, firmware, uploadOptions{artifactName: "bootloader.bin"}); response.Code != http.StatusBadRequest {
		t.Fatalf("invalid artifact status = %d", response.Code)
	}
}

func TestHealthAndReadiness(t *testing.T) {
	api := newTestAPI(t, 1024)
	for _, path := range []string{"/greensync/ota/health", "/greensync/ota/ready"} {
		request := httptest.NewRequest(http.MethodGet, path, nil)
		response := httptest.NewRecorder()
		api.Handler().ServeHTTP(response, request)
		if response.Code != http.StatusOK {
			t.Fatalf("GET %s status = %d", path, response.Code)
		}
	}
}

type uploadOptions struct {
	token        string
	hardware     string
	version      string
	channel      string
	artifactName string
	sha256       string
}

func newTestAPI(t *testing.T, maxUpload int64) *API {
	t.Helper()
	api, err := New(Config{
		DataDir:       t.TempDir(),
		PathPrefix:    "/greensync/ota",
		PublicBaseURL: "https://ota.test/greensync/ota",
		Token:         testToken,
		MaxUploadSize: maxUpload,
	}, slog.New(slog.NewTextHandler(io.Discard, nil)))
	if err != nil {
		t.Fatal(err)
	}
	return api
}

func upload(t *testing.T, api *API, firmware []byte, options uploadOptions) *httptest.ResponseRecorder {
	t.Helper()
	if options.token == "" {
		options.token = testToken
	}
	if options.hardware == "" {
		options.hardware = "m5stack-atoms3-lite"
	}
	if options.version == "" {
		options.version = "0.3.0"
	}
	if options.channel == "" {
		options.channel = "stable"
	}
	if options.artifactName == "" {
		options.artifactName = "firmware.bin"
	}
	if options.sha256 == "" {
		hash := sha256.Sum256(firmware)
		options.sha256 = hex.EncodeToString(hash[:])
	}

	var body bytes.Buffer
	writer := multipart.NewWriter(&body)
	fields := map[string]string{
		"hardware":     options.hardware,
		"version":      options.version,
		"channel":      options.channel,
		"artifactName": options.artifactName,
		"size":         strconv.Itoa(len(firmware)),
		"sha256":       options.sha256,
	}
	for name, value := range fields {
		if err := writer.WriteField(name, value); err != nil {
			t.Fatal(err)
		}
	}
	header := make(textproto.MIMEHeader)
	header.Set("Content-Disposition", `form-data; name="firmware"; filename="`+options.artifactName+`"`)
	header.Set("Content-Type", "application/octet-stream")
	part, err := writer.CreatePart(header)
	if err != nil {
		t.Fatal(err)
	}
	if _, err := part.Write(firmware); err != nil {
		t.Fatal(err)
	}
	if err := writer.Close(); err != nil {
		t.Fatal(err)
	}

	request := httptest.NewRequest(http.MethodPost, "/greensync/ota/admin/api/v1/releases", &body)
	request.Header.Set("Content-Type", writer.FormDataContentType())
	request.Header.Set("Authorization", "Bearer "+options.token)
	response := httptest.NewRecorder()
	api.Handler().ServeHTTP(response, request)
	return response
}

func assertDownload(t *testing.T, api *API, path string, expected []byte, cacheControl string) []byte {
	t.Helper()
	request := httptest.NewRequest(http.MethodGet, path, nil)
	response := httptest.NewRecorder()
	api.Handler().ServeHTTP(response, request)
	if response.Code != http.StatusOK {
		t.Fatalf("GET %s status = %d, body = %s", path, response.Code, response.Body.String())
	}
	if response.Header().Get("Cache-Control") != cacheControl {
		t.Fatalf("GET %s Cache-Control = %q", path, response.Header().Get("Cache-Control"))
	}
	data := response.Body.Bytes()
	if expected != nil && !bytes.Equal(data, expected) {
		t.Fatalf("GET %s returned unexpected bytes", path)
	}
	return data
}

func decodeJSON(t *testing.T, data []byte, destination any) {
	t.Helper()
	if err := json.Unmarshal(data, destination); err != nil {
		t.Fatalf("decode JSON: %v\n%s", err, data)
	}
}
