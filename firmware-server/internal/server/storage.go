package server

import (
	"encoding/json"
	"errors"
	"fmt"
	"io"
	"os"
	"path/filepath"
	"sync"
	"time"
)

var ErrReleaseExists = errors.New("release already exists")

type Manifest struct {
	SchemaVersion int    `json:"schemaVersion"`
	Version       string `json:"version"`
	Hardware      string `json:"hardware"`
	URL           string `json:"url"`
	Size          int64  `json:"size"`
	SHA256        string `json:"sha256"`
	PublishedAt   string `json:"publishedAt"`
}

type Release struct {
	Hardware string
	Version  string
	Channel  string
	Size     int64
	SHA256   string
	TempFile string
	BaseURL  string
}

type Storage struct {
	root string
	mu   sync.Mutex
}

func NewStorage(root string) (*Storage, error) {
	for _, dir := range []string{"releases", "channels", "staging"} {
		if err := os.MkdirAll(filepath.Join(root, dir), 0o750); err != nil {
			return nil, fmt.Errorf("create storage directory: %w", err)
		}
	}
	return &Storage{root: root}, nil
}

func (s *Storage) Publish(release Release) (Manifest, error) {
	s.mu.Lock()
	defer s.mu.Unlock()

	releaseDir := filepath.Join(s.root, "releases", release.Hardware, release.Version)
	if _, err := os.Stat(releaseDir); err == nil {
		return Manifest{}, ErrReleaseExists
	} else if !errors.Is(err, os.ErrNotExist) {
		return Manifest{}, fmt.Errorf("check release: %w", err)
	}

	stageDir, err := os.MkdirTemp(filepath.Join(s.root, "staging"), "release-")
	if err != nil {
		return Manifest{}, fmt.Errorf("create release staging directory: %w", err)
	}
	defer os.RemoveAll(stageDir)

	if err := moveFile(release.TempFile, filepath.Join(stageDir, "firmware.bin")); err != nil {
		return Manifest{}, err
	}

	manifest := Manifest{
		SchemaVersion: 1,
		Version:       release.Version,
		Hardware:      release.Hardware,
		URL:           release.BaseURL + "/api/v1/releases/" + release.Hardware + "/" + release.Version + "/firmware.bin",
		Size:          release.Size,
		SHA256:        release.SHA256,
		PublishedAt:   time.Now().UTC().Format(time.RFC3339),
	}
	manifestData, err := json.MarshalIndent(manifest, "", "  ")
	if err != nil {
		return Manifest{}, fmt.Errorf("encode manifest: %w", err)
	}
	manifestData = append(manifestData, '\n')
	if err := os.WriteFile(filepath.Join(stageDir, "manifest.json"), manifestData, 0o640); err != nil {
		return Manifest{}, fmt.Errorf("write manifest: %w", err)
	}

	if err := os.MkdirAll(filepath.Dir(releaseDir), 0o750); err != nil {
		return Manifest{}, fmt.Errorf("create hardware release directory: %w", err)
	}
	if err := os.Rename(stageDir, releaseDir); err != nil {
		return Manifest{}, fmt.Errorf("publish release: %w", err)
	}

	channelPath := filepath.Join(s.root, "channels", release.Hardware, release.Channel, "manifest.json")
	if err := writeAtomic(channelPath, manifestData); err != nil {
		_ = os.RemoveAll(releaseDir)
		return Manifest{}, fmt.Errorf("publish channel: %w", err)
	}

	return manifest, nil
}

func (s *Storage) ReleaseFile(hardware, version, name string) string {
	return filepath.Join(s.root, "releases", hardware, version, name)
}

func (s *Storage) ChannelManifest(hardware, channel string) string {
	return filepath.Join(s.root, "channels", hardware, channel, "manifest.json")
}

func moveFile(source, destination string) error {
	if err := os.Rename(source, destination); err == nil {
		return nil
	}

	in, err := os.Open(source)
	if err != nil {
		return fmt.Errorf("open staged firmware: %w", err)
	}
	defer in.Close()
	out, err := os.OpenFile(destination, os.O_CREATE|os.O_EXCL|os.O_WRONLY, 0o640)
	if err != nil {
		return fmt.Errorf("create release firmware: %w", err)
	}
	if _, err := io.Copy(out, in); err != nil {
		out.Close()
		return fmt.Errorf("copy release firmware: %w", err)
	}
	if err := out.Close(); err != nil {
		return fmt.Errorf("close release firmware: %w", err)
	}
	return os.Remove(source)
}

func writeAtomic(path string, data []byte) error {
	if err := os.MkdirAll(filepath.Dir(path), 0o750); err != nil {
		return err
	}
	temp, err := os.CreateTemp(filepath.Dir(path), ".manifest-")
	if err != nil {
		return err
	}
	tempName := temp.Name()
	defer os.Remove(tempName)
	if err := temp.Chmod(0o640); err != nil {
		temp.Close()
		return err
	}
	if _, err := temp.Write(data); err != nil {
		temp.Close()
		return err
	}
	if err := temp.Sync(); err != nil {
		temp.Close()
		return err
	}
	if err := temp.Close(); err != nil {
		return err
	}
	return os.Rename(tempName, path)
}
