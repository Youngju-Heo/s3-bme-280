package transport

import (
	"bytes"
	"testing"
)

// fakePort feeds scripted chunks to the line reader; an exhausted script acts like a read timeout.
type fakePort struct {
	reads   [][]byte
	written bytes.Buffer
}

func (p *fakePort) Read(b []byte) (int, error) {
	if len(p.reads) == 0 {
		return 0, nil
	}
	n := copy(b, p.reads[0])
	p.reads = p.reads[1:]
	return n, nil
}
func (p *fakePort) Write(b []byte) (int, error) { return p.written.Write(b) }
func (p *fakePort) Close() error                { return nil }

func TestReadLineAssemblesAcrossChunksAndStripsCRLF(t *testing.T) {
	port := &fakePort{reads: [][]byte{[]byte(`{"ok"`), []byte(":true}\r\n{\"a\":1}\n")}}
	tr := newWithPort(port)

	line, err := tr.ReadLine()
	if err != nil || line != `{"ok":true}` {
		t.Fatalf("first line = %q, %v", line, err)
	}
	line, err = tr.ReadLine()
	if err != nil || line != `{"a":1}` {
		t.Fatalf("second line = %q, %v", line, err)
	}
	line, err = tr.ReadLine()
	if err != nil || line != "" {
		t.Fatalf("timeout should give empty line, got %q, %v", line, err)
	}
}

func TestReadLineKeepsPartialLineUntilTerminator(t *testing.T) {
	port := &fakePort{reads: [][]byte{[]byte("partial")}}
	tr := newWithPort(port)
	if line, _ := tr.ReadLine(); line != "" {
		t.Fatalf("expected timeout with partial data buffered, got %q", line)
	}
	port.reads = [][]byte{[]byte(" done\n")}
	if line, _ := tr.ReadLine(); line != "partial done" {
		t.Fatalf("got %q", line)
	}
}

func TestSendLineAppendsNewline(t *testing.T) {
	port := &fakePort{}
	tr := newWithPort(port)
	if err := tr.SendLine(`{"cmd":"ping"}`); err != nil {
		t.Fatal(err)
	}
	if got := port.written.String(); got != "{\"cmd\":\"ping\"}\n" {
		t.Fatalf("got %q", got)
	}
}

func TestFakeRecordsSentAndServesResponses(t *testing.T) {
	f := NewFake("a", "b")
	_ = f.Open()
	_ = f.SendLine("x")
	l1, _ := f.ReadLine()
	l2, _ := f.ReadLine()
	l3, _ := f.ReadLine()
	if !f.Opened || f.Sent[0] != "x" || l1 != "a" || l2 != "b" || l3 != "" {
		t.Fatalf("fake misbehaved: opened=%v sent=%v lines=%q %q %q", f.Opened, f.Sent, l1, l2, l3)
	}
}
