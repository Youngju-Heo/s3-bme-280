package main

import (
	"os"

	"bme280-tool/internal/cli"
)

func main() {
	restore := enableUTF8Console()
	code := cli.Run(os.Args[1:], os.Stdout, os.Stderr, os.Stdin, nil)
	restore()
	os.Exit(code)
}
