// Package transport moves newline-delimited lines over the device's USB serial port.
package transport

import (
	"bytes"
	"io"
	"strings"
	"time"

	"go.bug.st/serial"
)

// Transport is the line-oriented link the client talks over; tests substitute Fake.
type Transport interface {
	Open() error
	Close() error
	SendLine(line string) error
	ReadLine() (string, error) // next line without terminator; ("", nil) on read timeout
}

// rawPort is the part of serial.Port the line reader needs.
type rawPort interface {
	io.ReadWriteCloser
}

type SerialTransport struct {
	name    string
	timeout time.Duration
	port    rawPort
	buf     []byte
}

func NewSerial(name string, timeout time.Duration) *SerialTransport {
	return &SerialTransport{name: name, timeout: timeout}
}

func newWithPort(p rawPort) *SerialTransport {
	return &SerialTransport{port: p}
}

// Open holds DTR and RTS low so opening the port does not reset the ESP32-S3 (USB Serial/JTAG
// uses those lines for reset/bootloader entry).
func (t *SerialTransport) Open() error {
	mode := &serial.Mode{
		BaudRate:          115200,
		InitialStatusBits: &serial.ModemOutputBits{RTS: false, DTR: false},
	}
	p, err := serial.Open(t.name, mode)
	if err != nil {
		return err
	}
	if err := p.SetReadTimeout(t.timeout); err != nil {
		p.Close()
		return err
	}
	_ = p.ResetInputBuffer()
	t.port = p
	t.buf = t.buf[:0]
	return nil
}

func (t *SerialTransport) Close() error {
	if t.port == nil {
		return nil
	}
	err := t.port.Close()
	t.port = nil
	return err
}

func (t *SerialTransport) SendLine(line string) error {
	_, err := io.WriteString(t.port, line+"\n")
	return err
}

func (t *SerialTransport) ReadLine() (string, error) {
	for {
		if i := bytes.IndexByte(t.buf, '\n'); i >= 0 {
			line := strings.TrimSpace(string(t.buf[:i]))
			t.buf = append(t.buf[:0], t.buf[i+1:]...)
			return line, nil
		}
		chunk := make([]byte, 4096)
		n, err := t.port.Read(chunk)
		if err != nil {
			return "", err
		}
		if n == 0 { // read timeout; keep any partial line for the next call
			return "", nil
		}
		t.buf = append(t.buf, chunk[:n]...)
	}
}
