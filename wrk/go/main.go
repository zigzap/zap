package main

import (
	"fmt"
	"net/http"
)

func hello(w http.ResponseWriter, req *http.Request) {
	fmt.Fprintf(w, "hello from GO!!!\n")
}

func main() {
	print("listening on 0.0.0.0:8090\n")
	http.HandleFunc("/hello", hello)
	http.ListenAndServe(":8090", nil)
}
