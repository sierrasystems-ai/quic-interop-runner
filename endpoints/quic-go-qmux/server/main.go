package main

import (
	"context"
	"crypto/tls"
	"fmt"
	"log"
	"net"
	"net/http"
	"os"
	"sync"

	"github.com/quic-go/quic-go"
	"github.com/quic-go/quic-go/http3"
	"github.com/quic-go/quic-go/interop/http09"
	"github.com/quic-go/quic-go/interop/utils"
)

const nextProtoH3QMux = "h3-qmux"

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

	testcase := os.Getenv("TESTCASE")

	quicConf := &quic.Config{
		Tracer: utils.NewQLOGConnectionTracer,
	}
	cert, err := tls.LoadX509KeyPair("/certs/cert.pem", "/certs/priv.key")
	if err != nil {
		fmt.Println(err)
		os.Exit(1)
	}
	tlsConf := &tls.Config{
		Certificates: []tls.Certificate{cert},
		KeyLogWriter: keyLog,
	}

	http.DefaultServeMux.Handle("/", http.FileServer(http.Dir("/www")))

	switch testcase {
	case "handshake", "transfer":
		tlsConf.NextProtos = []string{http09.NextProtoQMux}
		err = runHTTP09QMuxServer(tlsConf, quicConf)
	case "http3":
		tlsConf.NextProtos = []string{nextProtoH3QMux}
		err = runHTTP3QMuxServer(tlsConf, quicConf)
	default:
		fmt.Printf("unsupported test case: %s\n", testcase)
		os.Exit(127)
	}

	if err != nil {
		fmt.Printf("Error running server: %s\n", err.Error())
		os.Exit(1)
	}
}

func runHTTP09QMuxServer(tlsConf *tls.Config, quicConf *quic.Config) error {
	ln, err := net.Listen("tcp", ":443")
	if err != nil {
		return err
	}
	defer ln.Close()

	server := http09.Server{}
	return acceptQMuxLoop(ln, tlsConf, quicConf, func(conn *quic.Conn) {
		server.ServeQUICConn(conn)
	})
}

func runHTTP3QMuxServer(tlsConf *tls.Config, quicConf *quic.Config) error {
	ln, err := net.Listen("tcp", ":443")
	if err != nil {
		return err
	}
	defer ln.Close()

	server := &http3.Server{
		TLSConfig:  tlsConf,
		QUICConfig: quicConf,
		Handler:    http.DefaultServeMux,
	}
	return acceptQMuxLoop(ln, tlsConf, quicConf, func(conn *quic.Conn) {
		if err := server.ServeQUICConn(conn); err != nil {
			log.Printf("ServeQUICConn: %s", err)
		}
	})
}

func acceptQMuxLoop(ln net.Listener, tlsConf *tls.Config, quicConf *quic.Config, handle func(*quic.Conn)) error {
	var wg sync.WaitGroup
	defer wg.Wait()

	for {
		tcpConn, err := ln.Accept()
		if err != nil {
			return err
		}
		wg.Add(1)
		go func() {
			defer wg.Done()
			ctx := context.Background()
			conn, err := quic.AcceptQMux(ctx, tcpConn, tlsConf, quicConf)
			if err != nil {
				log.Printf("AcceptQMux failed: %s", err)
				tcpConn.Close()
				return
			}
			handle(conn)
		}()
	}
}
