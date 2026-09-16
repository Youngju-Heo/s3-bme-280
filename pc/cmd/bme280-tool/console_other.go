//go:build !windows

package main

func enableUTF8Console() func() { return func() {} }
