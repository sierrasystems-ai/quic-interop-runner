package main

import (
	"context"
	"crypto/tls"
	"errors"
	"flag"
	"fmt"
	"io"
	"log"
	"net"
	"net/http"
	"os"
	"time"

	"golang.org/x/sync/errgroup"

	"github.com/quic-go/quic-go"
	"github.com/quic-go/quic-go/http3"
	"github.com/quic-go/quic-go/interop/http09"
	"github.com/quic-go/quic-go/interop/utils"
)

const nextProtoH3QMux = "h3-qmux"

var errUnsupported = errors.New("unsupported test case")

var tlsConf *tls.Config

func main() {
	logFile, err := os.Create("/logs/log.txt")
	if err != nil {
		fmt.Printf("Could not create log file: %s\n", err.Error())
		os.Exit(1)
	}
	defer logFile.Close()
	log.SetOutput(logFile)

	keyLog, err := utils.GetSSLKeyLog()
	if err != nil {
		fmt.Printf("Could not create key log: %s\n", err.Error())
		os.Exit(1)
	}
	if keyLog != nil {
		defer keyLog.Close()
	}

	tlsConf = &tls.Config{
		InsecureSkipVerify: true,
		KeyLogWriter:       keyLog,
	}
	testcase := os.Getenv("TESTCASE")
	if err := runTestcase(testcase); err != nil {
		if err == errUnsupported {
			fmt.Printf("unsupported test case: %s\n", testcase)
			os.Exit(127)
		}
		fmt.Printf("Downloading files failed: %s\n", err.Error())
		os.Exit(1)
	}
}

func runTestcase(testcase string) error {
	flag.Parse()
	urls := flag.Args()

	quicConf := &quic.Config{
		Tracer: utils.NewQLOGConnectionTracer,
		// Small windows so transfer exercises QMux flow control.
		InitialStreamReceiveWindow:     64 * 1024,
		InitialConnectionReceiveWindow: 128 * 1024,
		MaxStreamReceiveWindow:         6 * 1024 * 1024,
		MaxConnectionReceiveWindow:     15 * 1024 * 1024,
	}

	switch testcase {
	case "handshake", "transfer":
		r := &http09.RoundTripper{
			TLSClientConfig: tlsConf,
			QuicConfig:      quicConf,
			NextProtos:      []string{http09.NextProtoQMux},
			Dial:            dialQMux,
		}
		defer r.Close()
		return downloadFiles(r, urls)
	case "http3":
		r := &http3.Transport{
			TLSClientConfig: tlsConf,
			QUICConfig:      quicConf,
			Dial:            dialQMuxH3,
		}
		defer r.Close()
		return downloadFiles(r, urls)
	default:
		return errUnsupported
	}
}

func dialQMux(ctx context.Context, addr string, tlsCfg *tls.Config, conf *quic.Config) (*quic.Conn, error) {
	return dialQMuxWithALPN(ctx, addr, tlsCfg, conf, nil)
}

func dialQMuxH3(ctx context.Context, addr string, tlsCfg *tls.Config, conf *quic.Config) (*quic.Conn, error) {
	// http3.Transport overwrites NextProtos to "h3"; replace with the QMux mapping.
	return dialQMuxWithALPN(ctx, addr, tlsCfg, conf, []string{nextProtoH3QMux})
}

func dialQMuxWithALPN(ctx context.Context, addr string, tlsCfg *tls.Config, conf *quic.Config, nextProtos []string) (*quic.Conn, error) {
	cfg := tlsCfg.Clone()
	if len(nextProtos) > 0 {
		cfg.NextProtos = append([]string{}, nextProtos...)
	}
	d := net.Dialer{}
	tcpConn, err := d.DialContext(ctx, "tcp", addr)
	if err != nil {
		return nil, err
	}
	conn, err := quic.DialQMux(ctx, tcpConn, cfg, conf)
	if err != nil {
		tcpConn.Close()
		return nil, err
	}
	return conn, nil
}

func downloadFiles(cl http.RoundTripper, urls []string) error {
	var g errgroup.Group
	for _, u := range urls {
		url := u
		g.Go(func() error {
			return downloadFile(cl, url)
		})
	}
	return g.Wait()
}

func downloadFile(cl http.RoundTripper, url string) error {
	ctx, cancel := context.WithTimeout(context.Background(), 55*time.Second)
	defer cancel()
	req, err := http.NewRequestWithContext(ctx, http.MethodGet, url, nil)
	if err != nil {
		return err
	}
	rsp, err := cl.RoundTrip(req)
	if err != nil {
		return err
	}
	defer rsp.Body.Close()

	file, err := os.Create("/downloads" + req.URL.Path)
	if err != nil {
		return err
	}
	defer file.Close()
	_, err = io.Copy(file, rsp.Body)
	return err
}
