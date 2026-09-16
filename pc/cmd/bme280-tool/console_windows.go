//go:build windows

package main

import "syscall"

// enableUTF8Console switches the console output code page to UTF-8 so multi-byte text renders
// correctly on a default cp949 console. The returned closure restores the previous code page.
func enableUTF8Console() func() {
	kernel32 := syscall.NewLazyDLL("kernel32.dll")
	getConsoleOutputCP := kernel32.NewProc("GetConsoleOutputCP")
	setConsoleOutputCP := kernel32.NewProc("SetConsoleOutputCP")

	previous, _, _ := getConsoleOutputCP.Call()
	_, _, _ = setConsoleOutputCP.Call(65001)
	return func() {
		_, _, _ = setConsoleOutputCP.Call(previous)
	}
}
