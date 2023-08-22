use hello::ThreadPool;
use std::io::prelude::*;
use std::net::TcpListener;
use std::net::TcpStream;

fn main() {
    let listener = TcpListener::bind("127.0.0.1:7878").unwrap();
    //Creating a massive amount of threads so we can always have one ready to go.
    let mut pool = ThreadPool::new(128);

    for stream in listener.incoming() {
        let stream = stream.unwrap();
        //handle_connection(stream);
        pool.execute(handle_connection, stream);
    }

    println!("Shutting down.");
}

fn handle_connection(mut stream: TcpStream) {
    stream.set_nodelay(true).expect("set_nodelay call failed");
    loop{
        let mut buffer = [0; 1024];
        match stream.read(&mut buffer){
            Err(_)=>return,
            Ok(0)=>return,            
            Ok(_v)=>{},
        }
        
        let response_bytes = b"HTTP/1.1 200 OK\r\nContent-Length: 16\r\nConnection: keep-alive\r\n\r\nHELLO from RUST!";
        
        stream.write_all(response_bytes).unwrap();
    }
}
