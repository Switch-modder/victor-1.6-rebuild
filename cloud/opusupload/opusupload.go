package main

import (
	"anki/config"
	"context"
	"flag"
	"fmt"
	"io/ioutil"

	"github.com/anki/sai-chipper-voice/client/chipper"
	pb "github.com/anki/sai-chipper-voice/proto/anki/chipperpb"
	"github.com/google/uuid"
)

func main() {
	fname := flag.String("file", "", "filename to open")
	ms := flag.Bool("ms", false, "use ms")
	flag.Parse()

	fmt.Println("Connecting")
	ctx := context.Background()
	conn, err := chipper.NewConn(ctx, config.DefaultURLs.Chipper,
		chipper.WithSessionID(uuid.New().String()[:16]))
	if err != nil {
		fmt.Println("Error starting chipper:", err)
		return
	}
	defer conn.Close()
	fmt.Println("Streaming")
	opts := chipper.IntentOpts{
		StreamOpts: chipper.StreamOpts{
			CompressOpts: chipper.CompressOpts{
				PreEncoded: true},
		}}
	if *ms {
		opts.Handler = pb.IntentService_BING_LUIS
	}
	stream, err := conn.NewIntentStream(ctx, opts)
	if err != nil {
		fmt.Println("Error creating stream:", err)
		return
	}
	defer stream.Close()

	fmt.Println("Reading file")
	data, err := ioutil.ReadFile(*fname)
	if err != nil {
		fmt.Println("Error reading file", *fname, err)
		return
	}
	fmt.Println("Read file with size", len(data))

	for len(data) > 0 {
		size := 1024
		if len(data) < size {
			size = len(data)
		}
		chunk := data[:size]
		data = data[size:]
		err = stream.SendAudio(chunk)
		if err != nil {
			fmt.Println("Send error:", err)
			return
		}
	}

	fmt.Println("Waiting for intent...")
	response, err := stream.WaitForResponse()
	if err != nil {
		fmt.Println("Intent error", err)
		return
	}

	result := response.(*chipper.IntentResult)
	fmt.Println("Result action:", result.Action)
	fmt.Println("Result\n", result)
}
