package transport

// Fake is a scripted Transport: it serves Responses in order and records every sent line.
type Fake struct {
	Responses []string
	Sent      []string
	Opened    bool
}

func NewFake(responses ...string) *Fake {
	return &Fake{Responses: responses}
}

func (f *Fake) Open() error  { f.Opened = true; return nil }
func (f *Fake) Close() error { f.Opened = false; return nil }

func (f *Fake) SendLine(line string) error {
	f.Sent = append(f.Sent, line)
	return nil
}

func (f *Fake) ReadLine() (string, error) {
	if len(f.Responses) == 0 {
		return "", nil
	}
	line := f.Responses[0]
	f.Responses = f.Responses[1:]
	return line, nil
}
