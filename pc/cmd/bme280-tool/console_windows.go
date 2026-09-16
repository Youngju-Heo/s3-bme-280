//go:build windows

package main

import "syscall"

// enableUTF8Console switches the console output code page to UTF-8 so Korean text renders
// on a default cp949 console.
func enableUTF8Console() {
	proc := syscall.NewLazyDLL("kernel32.dll").NewProc("SetConsoleOutputCP")
	_, _, _ = proc.Call(65001)
}
