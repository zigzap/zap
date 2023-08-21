use hello::ThreadPool;
use std::io::prelude::*;
use std::net::TcpListener;
use std::net::TcpStream;

fn main() {
    let listener = TcpListener::bind("127.0.0.1:7878").unwrap();
    //Creating a massive amount of threads so we can always have one ready to go.
    let mut pool = ThreadPool::new(128);

    // for stream in listener.incoming().take(2) {
    for stream in listener.incoming() {
        let stream = stream.unwrap();
        //handle_connection(stream);
        pool.execute(handle_connection, stream);
    }

    println!("Shutting down.");
}

fn handle_connection(mut stream: TcpStream) {
    stream.set_nodelay(true).expect("set_nodelay call failed");
    let mut buffer = [0; 1024];
    let nbytes = stream.read(&mut buffer).unwrap();
    if nbytes == 0 {
        return;
    }

    let status_line = "HTTP/1.1 200 OK";

    let contents = "HELLO from RUST!";

    let response = format!(
        "{}\r\nContent-Length: {}\r\n\r\n{}",
        status_line,
        contents.len(),
        contents
    );

    stream.write_all(response.as_bytes()).unwrap();
}
