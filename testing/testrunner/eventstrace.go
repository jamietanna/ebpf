// SPDX-License-Identifier: Elastic-2.0

/*
 * Copyright 2022 Elasticsearch B.V. and/or licensed to Elasticsearch B.V. under
 * one or more contributor license agreements. Licensed under the Elastic
 * License 2.0; you may not use this file except in compliance with the Elastic
 * License 2.0.
 */

package main

import (
	"bufio"
	"context"
	"fmt"
	"io"
	"os/exec"
	"time"
)

type EventsTraceInstance struct {
	Cmd        *exec.Cmd
	Stdout     io.ReadCloser
	Stderr     io.ReadCloser
	StdoutChan chan string
	StderrChan chan string
}

const eventsTraceBinPath = "/EventsTrace"

func (et *EventsTraceInstance) Start(ctx context.Context) {
	if err := et.Cmd.Start(); err != nil {
		fmt.Println("failed to start EventsTrace: ", err)
		TestFail()
	}

	readStreamFunc := func(streamCtx context.Context, c chan string, stream io.ReadCloser) {
		defer close(c)

		for {
			select {
			case <-streamCtx.Done():
				return
			default:
				scanner := bufio.NewScanner(stream)
				for scanner.Scan() {
					c <- scanner.Text()
				}

				if err := scanner.Err(); err != nil {
					fmt.Println("failed to read from EventsTrace stdout: ", err)
					return
				}
			}
		}
	}

	// Child contexts used to signal when goroutines should stop
	stdoutCtx, _ := context.WithCancel(ctx)
	stderrCtx, _ := context.WithCancel(ctx)

	et.StdoutChan = make(chan string, 100)
	et.StderrChan = make(chan string, 100)

	go readStreamFunc(stdoutCtx, et.StdoutChan, et.Stdout)
	go readStreamFunc(stderrCtx, et.StderrChan, et.Stderr)

	select {
	case <-et.StdoutChan:
		break
	case <-ctx.Done():
		et.DumpStderr()
		TestFail("timed out waiting for EventsTrace to get ready, dumped stderr above")
	}
}

func (et *EventsTraceInstance) DumpStderr() {
	fmt.Println("===== EventsTrace Stderr =====")
	for {
		select {
		case line, ok := <-et.StderrChan:
			if !ok {
				return
			}
			fmt.Println(line)
		}
	}
}

func (et *EventsTraceInstance) GetNextEventJson(types ...string) string {
	var line string
loop:
	for {
		select {
		case line = <-et.StdoutChan:
			eventType := getJsonEventType(line)

			for _, a := range types {
				if a == eventType {
					break loop
				}
			}
		case <-time.After(60 * time.Second):
			et.DumpStderr()
			TestFail("timed out waiting for EventsTrace output, dumped stderr above")
		}
	}

	return line
}

func (et *EventsTraceInstance) Stop() error {
	if err := et.Cmd.Process.Kill(); err != nil {
		return err
	}

	_, err := et.Cmd.Process.Wait()
	return err
}

func NewEventsTrace(ctx context.Context, args ...string) *EventsTraceInstance {
	var et EventsTraceInstance
	args = append(args, "--print-initialized", "--unbuffer-stdout", "--libbpf-verbose", "--set-bpf-tramp")
	et.Cmd = exec.CommandContext(ctx, eventsTraceBinPath, args...)

	stdout, err := et.Cmd.StdoutPipe()
	if err != nil {
		fmt.Println("failed to redirect stdout: ", err)
		TestFail()
	}
	et.Stdout = stdout

	stderr, err := et.Cmd.StderrPipe()
	if err != nil {
		fmt.Println("failed to redirect stderr: ", err)
		TestFail()
	}
	et.Stderr = stderr

	return &et
}
