package server

import (
	"crypto/sha256"
	"crypto/subtle"
	"encoding/hex"
	"encoding/json"
	"errors"
	"fmt"
	"io"
	"log/slog"
	"mime/multipart"
	"net/http"
	"net/url"
	"os"
	"path/filepath"
	"regexp"
	"strconv"
	"strings"
)

var (
	identifierPattern = regexp.MustCompile(`^[a-z0-9][a-z0-9._-]*$`)
	versionPattern    = regexp.MustCompile(`^[0-9]+\.[0-9]+\.[0-9]+(?:[+-][0-9A-Za-z.-]+)?$`)
	sha256Pattern     = regexp.MustCompile(`^[0-9a-f]{64}$`)
)

type API struct {
	cfg     Config
	storage *Storage
	logger  *slog.Logger
	handler http.Handler
}

type uploadResponse struct {
	Status   string   `json:"status"`
	Manifest Manifest `json:"manifest"`
}

type errorResponse struct {
	Error string `json:"error"`
}

func New(cfg Config, logger *slog.Logger) (*API, error) {
	storage, err := NewStorage(cfg.DataDir)
	if err != nil {
		return nil, err
	}
	if logger == nil {
		logger = slog.Default()
	}

	api := &API{cfg: cfg, storage: storage, logger: logger}
	mux := http.NewServeMux()
	mux.HandleFunc("GET "+cfg.PathPrefix+"/health", api.health)
	mux.HandleFunc("GET "+cfg.PathPrefix+"/ready", api.ready)
	mux.HandleFunc("POST "+cfg.PathPrefix+"/admin/api/v1/releases", api.requireAuth(api.uploadRelease))
	mux.HandleFunc("GET "+cfg.PathPrefix+"/api/v1/releases/", api.getRelease)
	mux.HandleFunc("GET "+cfg.PathPrefix+"/api/v1/channels/", api.getChannel)
	api.handler = api.securityHeaders(mux)
	return api, nil
}

func (a *API) Handler() http.Handler { return a.handler }

func (a *API) health(w http.ResponseWriter, _ *http.Request) {
	writeJSON(w, http.StatusOK, map[string]string{"status": "ok"})
}

func (a *API) ready(w http.ResponseWriter, _ *http.Request) {
	testFile, err := os.CreateTemp(filepath.Join(a.cfg.DataDir, "staging"), ".ready-")
	if err != nil {
		writeError(w, http.StatusServiceUnavailable, "storage is not writable")
		return
	}
	name := testFile.Name()
	testFile.Close()
	os.Remove(name)
	writeJSON(w, http.StatusOK, map[string]string{"status": "ready"})
}

func (a *API) uploadRelease(w http.ResponseWriter, r *http.Request) {
	maxBody := a.cfg.MaxUploadSize + 1024*1024
	r.Body = http.MaxBytesReader(w, r.Body, maxBody)
	if err := r.ParseMultipartForm(1024 * 1024); err != nil {
		writeError(w, http.StatusBadRequest, "invalid or oversized multipart request")
		return
	}
	if r.MultipartForm != nil {
		defer r.MultipartForm.RemoveAll()
	}

	hardware := r.FormValue("hardware")
	version := r.FormValue("version")
	channel := r.FormValue("channel")
	artifactName := r.FormValue("artifactName")
	expectedSHA := strings.ToLower(r.FormValue("sha256"))
	expectedSize, err := strconv.ParseInt(r.FormValue("size"), 10, 64)
	if err != nil || expectedSize <= 0 || expectedSize > a.cfg.MaxUploadSize {
		writeError(w, http.StatusBadRequest, "invalid size")
		return
	}
	if !identifierPattern.MatchString(hardware) || !identifierPattern.MatchString(channel) {
		writeError(w, http.StatusBadRequest, "invalid hardware or channel")
		return
	}
	if !versionPattern.MatchString(version) {
		writeError(w, http.StatusBadRequest, "invalid version")
		return
	}
	if !sha256Pattern.MatchString(expectedSHA) {
		writeError(w, http.StatusBadRequest, "invalid sha256")
		return
	}
	if !validArtifactName(artifactName) {
		writeError(w, http.StatusBadRequest, "invalid application artifact name")
		return
	}

	file, header, err := r.FormFile("firmware")
	if err != nil {
		writeError(w, http.StatusBadRequest, "firmware file is required")
		return
	}
	defer file.Close()
	if header.Filename != artifactName {
		writeError(w, http.StatusBadRequest, "artifactName does not match uploaded filename")
		return
	}

	temp, actualSize, actualSHA, err := a.stageFirmware(file)
	if err != nil {
		writeError(w, http.StatusBadRequest, err.Error())
		return
	}
	defer os.Remove(temp)
	if actualSize != expectedSize {
		writeError(w, http.StatusBadRequest, "size does not match uploaded firmware")
		return
	}
	if actualSHA != expectedSHA {
		writeError(w, http.StatusBadRequest, "sha256 does not match uploaded firmware")
		return
	}

	baseURL := a.publicBaseURL(r)
	manifest, err := a.storage.Publish(Release{
		Hardware: hardware,
		Version:  version,
		Channel:  channel,
		Size:     actualSize,
		SHA256:   actualSHA,
		TempFile: temp,
		BaseURL:  baseURL,
	})
	if errors.Is(err, ErrReleaseExists) {
		writeError(w, http.StatusConflict, "release already exists")
		return
	}
	if err != nil {
		a.logger.Error("publish release", "error", err, "hardware", hardware, "version", version)
		writeError(w, http.StatusInternalServerError, "could not publish release")
		return
	}

	a.logger.Info("release published", "hardware", hardware, "version", version, "channel", channel, "sha256", actualSHA)
	writeJSON(w, http.StatusCreated, uploadResponse{Status: "published", Manifest: manifest})
}

func (a *API) stageFirmware(source multipart.File) (string, int64, string, error) {
	temp, err := os.CreateTemp(filepath.Join(a.cfg.DataDir, "staging"), "upload-")
	if err != nil {
		return "", 0, "", fmt.Errorf("create upload staging file")
	}
	name := temp.Name()
	remove := true
	defer func() {
		temp.Close()
		if remove {
			os.Remove(name)
		}
	}()

	hash := sha256.New()
	count, err := io.Copy(io.MultiWriter(temp, hash), io.LimitReader(source, a.cfg.MaxUploadSize+1))
	if err != nil {
		return "", 0, "", fmt.Errorf("read uploaded firmware")
	}
	if count > a.cfg.MaxUploadSize {
		return "", 0, "", fmt.Errorf("firmware exceeds maximum upload size")
	}
	if count == 0 {
		return "", 0, "", fmt.Errorf("firmware is empty")
	}
	if _, err := temp.Seek(0, io.SeekStart); err != nil {
		return "", 0, "", fmt.Errorf("inspect uploaded firmware")
	}
	magic := []byte{0}
	if _, err := io.ReadFull(temp, magic); err != nil || magic[0] != 0xE9 {
		return "", 0, "", fmt.Errorf("firmware is not an ESP application image")
	}
	if err := temp.Sync(); err != nil {
		return "", 0, "", fmt.Errorf("flush uploaded firmware")
	}
	if err := temp.Close(); err != nil {
		return "", 0, "", fmt.Errorf("close uploaded firmware")
	}
	remove = false
	return name, count, hex.EncodeToString(hash.Sum(nil)), nil
}

func (a *API) getRelease(w http.ResponseWriter, r *http.Request) {
	relative := strings.TrimPrefix(r.URL.Path, a.cfg.PathPrefix+"/api/v1/releases/")
	parts := strings.Split(relative, "/")
	if len(parts) != 3 || !identifierPattern.MatchString(parts[0]) || !versionPattern.MatchString(parts[1]) {
		http.NotFound(w, r)
		return
	}
	if parts[2] != "firmware.bin" && parts[2] != "manifest.json" {
		http.NotFound(w, r)
		return
	}
	w.Header().Set("Cache-Control", "public, max-age=31536000, immutable")
	serveFile(w, r, a.storage.ReleaseFile(parts[0], parts[1], parts[2]), parts[2])
}

func (a *API) getChannel(w http.ResponseWriter, r *http.Request) {
	relative := strings.TrimPrefix(r.URL.Path, a.cfg.PathPrefix+"/api/v1/channels/")
	parts := strings.Split(relative, "/")
	if len(parts) != 3 || parts[2] != "manifest.json" || !identifierPattern.MatchString(parts[0]) || !identifierPattern.MatchString(parts[1]) {
		http.NotFound(w, r)
		return
	}
	w.Header().Set("Cache-Control", "no-store")
	serveFile(w, r, a.storage.ChannelManifest(parts[0], parts[1]), parts[2])
}

func serveFile(w http.ResponseWriter, r *http.Request, path, name string) {
	if name == "manifest.json" {
		w.Header().Set("Content-Type", "application/json")
	} else {
		w.Header().Set("Content-Type", "application/octet-stream")
	}
	http.ServeFile(w, r, path)
}

func (a *API) requireAuth(next http.HandlerFunc) http.HandlerFunc {
	return func(w http.ResponseWriter, r *http.Request) {
		auth := r.Header.Get("Authorization")
		const prefix = "Bearer "
		if !strings.HasPrefix(auth, prefix) || subtle.ConstantTimeCompare([]byte(strings.TrimPrefix(auth, prefix)), []byte(a.cfg.Token)) != 1 {
			w.Header().Set("WWW-Authenticate", "Bearer")
			writeError(w, http.StatusUnauthorized, "unauthorized")
			return
		}
		next(w, r)
	}
}

func (a *API) securityHeaders(next http.Handler) http.Handler {
	return http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		w.Header().Set("X-Content-Type-Options", "nosniff")
		w.Header().Set("Referrer-Policy", "no-referrer")
		next.ServeHTTP(w, r)
	})
}

func (a *API) publicBaseURL(r *http.Request) string {
	if a.cfg.PublicBaseURL != "" {
		return a.cfg.PublicBaseURL
	}
	scheme := "http"
	if r.TLS != nil {
		scheme = "https"
	}
	return (&url.URL{Scheme: scheme, Host: r.Host, Path: a.cfg.PathPrefix}).String()
}

func validArtifactName(name string) bool {
	return filepath.Base(name) == name && strings.HasPrefix(name, "firmware") && strings.HasSuffix(name, ".bin") && name != "bootloader.bin" && name != "partitions.bin"
}

func writeJSON(w http.ResponseWriter, status int, value any) {
	w.Header().Set("Content-Type", "application/json")
	w.WriteHeader(status)
	_ = json.NewEncoder(w).Encode(value)
}

func writeError(w http.ResponseWriter, status int, message string) {
	writeJSON(w, status, errorResponse{Error: message})
}
