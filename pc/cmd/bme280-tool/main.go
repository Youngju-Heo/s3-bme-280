package main

import (
	"os"

	"bme280-tool/internal/cli"
)

func main() {
	enableUTF8Console()
	os.Exit(cli.Run(os.Args[1:], os.Stdout, os.Stderr, os.Stdin, nil))
}
